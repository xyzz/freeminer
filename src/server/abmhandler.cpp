#include "server/abmhandler.h"

#include "profiler.h"
#include "main.h"

ABMHandler::ABMHandler(std::list<ABMWithState> &abms, float dtime_s, ServerEnvironment *env,
		bool use_timers, bool activate = false):
	m_env(env),
	m_aabms_empty(true)
{
	m_aabms.resize(CONTENT_ID_CAPACITY);
	if(dtime_s < 0.001)
		return;

	//INodeDefManager *ndef = env->getGameDef()->ndef();
	for(auto & ai: abms){
		auto i = &ai;
		ActiveBlockModifier *abm = i->abm;
		float trigger_interval = abm->getTriggerInterval();
		if(trigger_interval < 0.001)
			trigger_interval = 0.001;
		float actual_interval = dtime_s;
		if(use_timers){
			i->timer += dtime_s;
			if(i->timer < trigger_interval)
				continue;
			i->timer -= trigger_interval;
			if (i->timer > trigger_interval*2)
				i->timer = 0;
			actual_interval = trigger_interval;
		}
		float intervals = actual_interval / trigger_interval;
		if(intervals == 0)
			continue;
		float chance = abm->getTriggerChance();
		if(chance == 0)
			chance = 1;
		ActiveABM aabm;
		aabm.abm = abm; //del, same as abmws
		aabm.abmws = i;
		aabm.chance = chance / intervals;
		if(aabm.chance == 0)
			aabm.chance = 1;

		// Trigger contents
			for (auto &c : i->trigger_ids)
			{
				if (!m_aabms[c]) {
					m_aabms[c] = new std::list<ActiveABM>;
					m_aabms_list.push_back(m_aabms[c]);
				}
				m_aabms[c]->push_back(aabm);
				m_aabms_empty = false;
			}
	}
}

ABMHandler::~ABMHandler() {
	for (std::list<std::list<ActiveABM>*>::iterator i = m_aabms_list.begin();
			i != m_aabms_list.end(); ++i)
		delete *i;
}

// Find out how many objects the given block and its neighbours contain.
// Returns the number of objects in the block, and also in 'wider' the
// number of objects in the block and all its neighbours. The latter
// may an estimate if any neighbours are unloaded.
u32 ABMHandler::countObjects(MapBlock *block, ServerMap * map, u32 &wider)
{
	wider = 0;
	u32 wider_unknown_count = 0;
	for(s16 x=-1; x<=1; x++)
	for(s16 y=-1; y<=1; y++)
	for(s16 z=-1; z<=1; z++)
	{
		MapBlock *block2 = map->getBlockNoCreateNoEx(
				block->getPos() + v3s16(x,y,z));
		if(block2==NULL){
			wider_unknown_count++;
			continue;
		}
		wider += block2->m_static_objects.m_active.size()
				+ block2->m_static_objects.m_stored.size();
	}
	// Extrapolate
	u32 active_object_count = block->m_static_objects.m_active.size();
	u32 wider_known_count = 3*3*3 - wider_unknown_count;
	wider += wider_unknown_count * wider / wider_known_count;
	return active_object_count;
}

void ABMHandler::apply(MapBlock *block, bool activate)
{
	if(m_aabms_empty)
		return;

	auto lock = block->lock_unique_rec(std::chrono::milliseconds(1));
	if (!lock->owns_lock())
		return;

	ScopeProfiler sp(g_profiler, "ABM apply", SPT_ADD);
	ServerMap *map = &m_env->getServerMap();

	u32 active_object_count_wider;
	u32 active_object_count = this->countObjects(block, map, active_object_count_wider);
	m_env->m_added_objects = 0;

	v3s16 p0;
	for(p0.X=0; p0.X<MAP_BLOCKSIZE; p0.X++)
	for(p0.Y=0; p0.Y<MAP_BLOCKSIZE; p0.Y++)
	for(p0.Z=0; p0.Z<MAP_BLOCKSIZE; p0.Z++)
	{
		MapNode n = block->getNodeNoEx(p0);
		content_t c = n.getContent();
		v3s16 p = p0 + block->getPosRelative();

		if (!m_aabms[c])
			continue;

		for(auto & ir: *(m_aabms[c])) {
			auto i = &ir;
			if(myrand() % i->chance != 0)
				continue;
			// Check neighbors
			MapNode neighbor;
			auto & required_neighbors = activate ? ir.abmws->required_neighbors_activate : ir.abmws->required_neighbors;
			if(required_neighbors.count() > 0)
			{
				v3s16 p1;
				int neighbors_range = i->abm->getNeighborsRange();
				for(p1.X = p.X - neighbors_range; p1.X <= p.X + neighbors_range; ++p1.X)
				for(p1.Y = p.Y - neighbors_range; p1.Y <= p.Y + neighbors_range; ++p1.Y)
				for(p1.Z = p.Z - neighbors_range; p1.Z <= p.Z + neighbors_range; ++p1.Z)
				{
					if(p1 == p)
						continue;
					MapNode n = map->getNodeNoLock(p1);
					content_t c = n.getContent();
					if(required_neighbors.get(c)){
						neighbor = n;
						goto neighbor_found;
					}
				}
				// No required neighbor found
				continue;
			}
neighbor_found:

			i->abm->trigger(m_env, p, n,
					active_object_count, active_object_count_wider, neighbor, activate);

			// Count surrounding objects again if the abms added any
			if(m_env->m_added_objects > 0) {
				active_object_count = countObjects(block, map, active_object_count_wider);
				m_env->m_added_objects = 0;
			}
		}
	}
}