#include "client/clientenvironment.h"

#include "clientmap.h"
#include "main.h"
#include "settings.h"
#include "collision.h"
#include "nodedef.h"
#include "event.h"
#include "profiler.h"
#include "clientsimpleobject.h"

/*
	ClientEnvironment
*/

ClientEnvironment::ClientEnvironment(ClientMap *map, scene::ISceneManager *smgr,
		ITextureSource *texturesource, IGameDef *gamedef,
		IrrlichtDevice *irr):
	m_map(map),
	m_smgr(smgr),
	m_texturesource(texturesource),
	m_gamedef(gamedef),
	m_irr(irr)
	,m_active_objects_client_last(0),
	m_move_max_loop(10)
{
	char zero = 0;
	memset(m_attachements, zero, sizeof(m_attachements));
}

ClientEnvironment::~ClientEnvironment()
{
	// delete active objects
	for(std::map<u16, ClientActiveObject*>::iterator
			i = m_active_objects.begin();
			i != m_active_objects.end(); ++i)
	{
		delete i->second;
	}

	for(std::list<ClientSimpleObject*>::iterator
			i = m_simple_objects.begin(); i != m_simple_objects.end(); ++i)
	{
		delete *i;
	}

	// Drop/delete map
	m_map->drop();
}

Map & ClientEnvironment::getMap()
{
	return *m_map;
}

ClientMap & ClientEnvironment::getClientMap()
{
	return *m_map;
}

void ClientEnvironment::addPlayer(Player *player)
{
	DSTACK(__FUNCTION_NAME);
	/*
		It is a failure if player is local and there already is a local
		player
	*/
	assert(!(player->isLocal() == true && getLocalPlayer() != NULL));

	Environment::addPlayer(player);
}

LocalPlayer * ClientEnvironment::getLocalPlayer()
{
	for(std::list<Player*>::iterator i = m_players.begin();
			i != m_players.end(); ++i)
	{
		Player *player = *i;
		if(player->isLocal())
			return (LocalPlayer*)player;
	}
	return NULL;
}

void ClientEnvironment::step(float dtime, float uptime, int max_cycle_ms)
{
	DSTACK(__FUNCTION_NAME);

	/* Step time of day */
	stepTimeOfDay(dtime);

	// Get some settings
	bool fly_allowed = m_gamedef->checkLocalPrivilege("fly");
	bool free_move = fly_allowed && g_settings->getBool("free_move");

	// Get local player
	LocalPlayer *lplayer = getLocalPlayer();
	assert(lplayer);
	// collision info queue
	std::list<CollisionInfo> player_collisions;

	/*
		Get the speed the player is going
	*/
	bool is_climbing = lplayer->is_climbing;

	f32 player_speed = lplayer->getSpeed().getLength();
	v3f pf = lplayer->getPosition();

	/*
		Maximum position increment
	*/
	//f32 position_max_increment = 0.05*BS;
	f32 position_max_increment = 0.1*BS;

	// Maximum time increment (for collision detection etc)
	// time = distance / speed
	f32 dtime_max_increment = 1;
	if(player_speed > 0.001)
		dtime_max_increment = position_max_increment / player_speed;

	// Maximum time increment is 10ms or lower
	if(dtime_max_increment > 0.01)
		dtime_max_increment = 0.01;

	if(dtime_max_increment*m_move_max_loop < dtime)
		dtime_max_increment = dtime/m_move_max_loop;

	// Don't allow overly huge dtime
	if(dtime > 2)
		dtime = 2;

	f32 dtime_downcount = dtime;

	/*
		Stuff that has a maximum time increment
	*/

	u32 loopcount = 0;
	u32 breaked = 0, lend_ms = porting::getTimeMs() + max_cycle_ms;
	do
	{
		loopcount++;

		f32 dtime_part;
		if(dtime_downcount > dtime_max_increment)
		{
			dtime_part = dtime_max_increment;
			dtime_downcount -= dtime_part;
		}
		else
		{
			dtime_part = dtime_downcount;
			/*
				Setting this to 0 (no -=dtime_part) disables an infinite loop
				when dtime_part is so small that dtime_downcount -= dtime_part
				does nothing
			*/
			dtime_downcount = 0;
		}

		/*
			Handle local player
		*/

		{
			// Apply physics
			if(free_move == false && is_climbing == false)
			{
				f32 viscosity_factor = 0;
				// Gravity
				v3f speed = lplayer->getSpeed();
				if(lplayer->in_liquid == false) {
					speed.Y -= lplayer->movement_gravity * lplayer->physics_override_gravity * dtime_part * 2;
					viscosity_factor = 0.97; // todo maybe depend on speed; 0.96 = ~100 nps max
					viscosity_factor += (1.0-viscosity_factor) *
						(1-(MAP_GENERATION_LIMIT - pf.Y/BS)/
							MAP_GENERATION_LIMIT);
				}

				// Liquid floating / sinking
				if(lplayer->in_liquid && !lplayer->swimming_vertical)
					speed.Y -= lplayer->movement_liquid_sink * dtime_part * 2;

				if(lplayer->in_liquid_stable || lplayer->in_liquid)
				{
					viscosity_factor = 0.3; // todo: must depend on speed^2
				}
				// Liquid resistance
				if(viscosity_factor)
				{
					// How much the node's viscosity blocks movement, ranges between 0 and 1
					// Should match the scale at which viscosity increase affects other liquid attributes

					v3f d_wanted = -speed / lplayer->movement_liquid_fluidity;
					f32 dl = d_wanted.getLength();
					if(dl > lplayer->movement_liquid_fluidity_smooth)
						dl = lplayer->movement_liquid_fluidity_smooth;
					dl *= (lplayer->liquid_viscosity * viscosity_factor) + (1 - viscosity_factor);

					v3f d = d_wanted.normalize() * dl;
					speed += d;

#if 0 // old code
					if(speed.X > lplayer->movement_liquid_fluidity + lplayer->movement_liquid_fluidity_smooth)	speed.X -= lplayer->movement_liquid_fluidity_smooth;
					if(speed.X < -lplayer->movement_liquid_fluidity - lplayer->movement_liquid_fluidity_smooth)	speed.X += lplayer->movement_liquid_fluidity_smooth;
					if(speed.Y > lplayer->movement_liquid_fluidity + lplayer->movement_liquid_fluidity_smooth)	speed.Y -= lplayer->movement_liquid_fluidity_smooth;
					if(speed.Y < -lplayer->movement_liquid_fluidity - lplayer->movement_liquid_fluidity_smooth)	speed.Y += lplayer->movement_liquid_fluidity_smooth;
					if(speed.Z > lplayer->movement_liquid_fluidity + lplayer->movement_liquid_fluidity_smooth)	speed.Z -= lplayer->movement_liquid_fluidity_smooth;
					if(speed.Z < -lplayer->movement_liquid_fluidity - lplayer->movement_liquid_fluidity_smooth)	speed.Z += lplayer->movement_liquid_fluidity_smooth;
#endif
				}

				lplayer->setSpeed(speed);
			}

			/*
				Move the lplayer.
				This also does collision detection.
			*/
			lplayer->move(dtime_part, this, position_max_increment,
					&player_collisions);
		}
		if (porting::getTimeMs() >= lend_ms) {
			breaked = loopcount;
			break;
		}

	}
	while(dtime_downcount > 0.001);

	//infostream<<"loop "<<loopcount<<"/"<<m_move_max_loop<<" breaked="<<breaked<<std::endl;

	if (breaked && m_move_max_loop > loopcount)
		--m_move_max_loop;
	if (!breaked && m_move_max_loop < 50)
		++m_move_max_loop;

	for(std::list<CollisionInfo>::iterator
			i = player_collisions.begin();
			i != player_collisions.end(); ++i)
	{
		CollisionInfo &info = *i;
		v3f speed_diff = info.new_speed - info.old_speed;
		// Handle only fall damage
		// (because otherwise walking against something in fast_move kills you)
		if((speed_diff.Y < 0 || info.old_speed.Y >= 0) &&
			speed_diff.getLength() <= lplayer->movement_speed_fast * 1.1) {
			continue;
		}
		f32 pre_factor = 1; // 1 hp per node/s
		f32 tolerance = BS*14; // 5 without damage
		f32 post_factor = 1; // 1 hp per node/s
		if(info.type == COLLISION_NODE)
		{
			const ContentFeatures &f = m_gamedef->ndef()->
					get(m_map->getNodeNoEx(info.node_p));
			// Determine fall damage multiplier
			int addp = itemgroup_get(f.groups, "fall_damage_add_percent");
			pre_factor = 1.0 + (float)addp/100.0;
		}
		float speed = pre_factor * speed_diff.getLength();
		if(speed > tolerance)
		{
			f32 damage_f = (speed - tolerance)/BS * post_factor;
			u16 damage = (u16)(damage_f+0.5);
			if(damage != 0){
				damageLocalPlayer(damage, true);
				MtEvent *e = new SimpleTriggerEvent("PlayerFallingDamage");
				m_gamedef->event()->put(e);
			}
		}
	}

	/*
		A quick draft of lava damage
	*/
	if(m_lava_hurt_interval.step(dtime, 1.0))
	{
		// Feet, middle and head
		v3s16 p1 = floatToInt(pf + v3f(0, BS*0.1, 0), BS);
		MapNode n1 = m_map->getNodeNoEx(p1);
		v3s16 p2 = floatToInt(pf + v3f(0, BS*0.8, 0), BS);
		MapNode n2 = m_map->getNodeNoEx(p2);
		v3s16 p3 = floatToInt(pf + v3f(0, BS*1.6, 0), BS);
		MapNode n3 = m_map->getNodeNoEx(p3);

		u32 damage_per_second = 0;
		damage_per_second = MYMAX(damage_per_second,
				m_gamedef->ndef()->get(n1).damage_per_second);
		damage_per_second = MYMAX(damage_per_second,
				m_gamedef->ndef()->get(n2).damage_per_second);
		damage_per_second = MYMAX(damage_per_second,
				m_gamedef->ndef()->get(n3).damage_per_second);

		if(damage_per_second != 0)
		{
			damageLocalPlayer(damage_per_second, true);
		}
	}

	/*
		Drowning
	*/
	if(m_drowning_interval.step(dtime, 2.0))
	{
		v3f pf = lplayer->getPosition();

		// head
		v3s16 p = floatToInt(pf + v3f(0, BS*1.6, 0), BS);
		MapNode n = m_map->getNodeNoEx(p);
		ContentFeatures c = m_gamedef->ndef()->get(n);
		u8 drowning_damage = c.drowning;
		if(drowning_damage > 0 && lplayer->hp > 0){
			u16 breath = lplayer->getBreath();
			if(breath > 10){
				breath = 11;
			}
			if(breath > 0){
				breath -= 1;
			}
			lplayer->setBreath(breath);
			updateLocalPlayerBreath(breath);
		}

		if(lplayer->getBreath() == 0 && drowning_damage > 0){
			damageLocalPlayer(drowning_damage, true);
		}
	}
	if(m_breathing_interval.step(dtime, 0.5))
	{
		v3f pf = lplayer->getPosition();

		// head
		v3s16 p = floatToInt(pf + v3f(0, BS*1.6, 0), BS);
		MapNode n = m_map->getNodeNoEx(p);
		ContentFeatures c = m_gamedef->ndef()->get(n);
		if (!lplayer->hp){
			lplayer->setBreath(11);
		}
		else if(c.drowning == 0){
			u16 breath = lplayer->getBreath();
			if(breath <= 10){
				breath += 1;
				lplayer->setBreath(breath);
				updateLocalPlayerBreath(breath);
			}
		}
	}

	/*
		Stuff that can be done in an arbitarily large dtime
	*/
	for(std::list<Player*>::iterator i = m_players.begin();
			i != m_players.end(); ++i)
	{
		Player *player = *i;

		/*
			Handle non-local players
		*/
		if(player->isLocal() == false)
		{
			// Move
			player->move(dtime, this, 100*BS);

		}

		// Update lighting on all players on client
		float light = 1.0;
		try{
			// Get node at head
			v3s16 p = player->getLightPosition();
			MapNode n = m_map->getNode(p);
			light = n.getLightBlendF1((float)getDayNightRatio()/1000, m_gamedef->ndef());
		}
		catch(InvalidPositionException &e){
			light = blend_light_f1((float)getDayNightRatio()/1000, LIGHT_SUN, 0);
		}
		player->light = light;
	}

	/*
		Step active objects and update lighting of them
	*/

	g_profiler->avg("CEnv: num of objects", m_active_objects.size());
	bool update_lighting = m_active_object_light_update_interval.step(dtime, 0.21);
	u32 n = 0, calls = 0, end_ms = porting::getTimeMs() + u32(500/g_settings->getFloat("wanted_fps"));
	for(std::map<u16, ClientActiveObject*>::iterator
			i = m_active_objects.begin();
			i != m_active_objects.end(); ++i)
	{

		if (n++ < m_active_objects_client_last)
			continue;
		else
			m_active_objects_client_last = 0;
		++calls;

		ClientActiveObject* obj = i->second;
		// Step object
		obj->step(dtime, this);

		if(update_lighting)
		{
			// Update lighting
			u8 light = 0;
			try{
				// Get node at head
				v3s16 p = obj->getLightPosition();
				MapNode n = m_map->getNode(p);
				light = n.getLightBlend(getDayNightRatio(), m_gamedef->ndef());
			}
			catch(InvalidPositionException &e){
				light = blend_light(getDayNightRatio(), LIGHT_SUN, 0);
			}
			obj->updateLight(light);
		}
		if (porting::getTimeMs() > end_ms) {
			m_active_objects_client_last = n;
			break;
		}
	}
	if (!calls)
		m_active_objects_client_last = 0;

	/*
		Step and handle simple objects
	*/

	g_profiler->avg("CEnv: num of simple objects", m_simple_objects.size());
	for(std::list<ClientSimpleObject*>::iterator
			i = m_simple_objects.begin(); i != m_simple_objects.end();)
	{
		ClientSimpleObject *simple = *i;
		std::list<ClientSimpleObject*>::iterator cur = i;
		++i;
		simple->step(dtime);
		if(simple->m_to_be_removed){
			delete simple;
			m_simple_objects.erase(cur);
		}
	}
}

void ClientEnvironment::addSimpleObject(ClientSimpleObject *simple)
{
	m_simple_objects.push_back(simple);
}

ClientActiveObject* ClientEnvironment::getActiveObject(u16 id)
{
	std::map<u16, ClientActiveObject*>::iterator n;
	n = m_active_objects.find(id);
	if(n == m_active_objects.end())
		return NULL;
	return n->second;
}

static bool isFreeClientActiveObjectId(u16 id,
	std::map<u16, ClientActiveObject*> &objects)
{
	if (id == 0)
		return false;

	return objects.find(id) == objects.end();
}

static u16 getFreeClientActiveObjectId(
	std::map<u16, ClientActiveObject*> &objects)
{
	//try to reuse id's as late as possible
	static u16 last_used_id = 0;
	u16 startid = last_used_id;
	for (;;)
	{
		last_used_id++;
		if (isFreeClientActiveObjectId(last_used_id, objects))
			return last_used_id;

		if (last_used_id == startid)
			return 0;
	}
}

u16 ClientEnvironment::addActiveObject(ClientActiveObject *object)
{
	assert(object);
	if(object->getId() == 0)
	{
		u16 new_id = getFreeClientActiveObjectId(m_active_objects);
		if(new_id == 0)
		{
			infostream<<"ClientEnvironment::addActiveObject(): "
					<<"no free ids available"<<std::endl;
			delete object;
			return 0;
		}
		object->setId(new_id);
	}
	if(isFreeClientActiveObjectId(object->getId(), m_active_objects) == false)
	{
		infostream<<"ClientEnvironment::addActiveObject(): "
				<<"id is not free ("<<object->getId()<<")"<<std::endl;
		delete object;
		return 0;
	}
/*
	infostream<<"ClientEnvironment::addActiveObject(): "
			<<"added (id="<<object->getId()<<")"<<std::endl;
*/
	m_active_objects[object->getId()] = object;
	object->addToScene(m_smgr, m_texturesource, m_irr);
	{ // Update lighting immediately
		u8 light = 0;
		try{
			// Get node at head
			v3s16 p = object->getLightPosition();
			MapNode n = m_map->getNode(p);
			light = n.getLightBlend(getDayNightRatio(), m_gamedef->ndef());
		}
		catch(InvalidPositionException &e){
			light = blend_light(getDayNightRatio(), LIGHT_SUN, 0);
		}
		object->updateLight(light);
	}
	return object->getId();
}

void ClientEnvironment::addActiveObject(u16 id, u8 type,
		const std::string &init_data)
{
	ClientActiveObject* obj =
			ClientActiveObject::create(type, m_gamedef, this);
	if(obj == NULL)
	{
		infostream<<"ClientEnvironment::addActiveObject(): "
				<<"id="<<id<<" type="<<type<<": Couldn't create object"
				<<std::endl;
		return;
	}

	obj->setId(id);

	try
	{
		obj->initialize(init_data);
	}
	catch(SerializationError &e)
	{
		errorstream<<"ClientEnvironment::addActiveObject():"
				<<" id="<<id<<" type="<<type
				<<": SerializationError in initialize(): "
				<<e.what()
				<<": init_data="<<serializeJsonString(init_data)
				<<std::endl;
	}

	addActiveObject(obj);
}

void ClientEnvironment::removeActiveObject(u16 id)
{
/*
	verbosestream<<"ClientEnvironment::removeActiveObject(): "
			<<"id="<<id<<std::endl;
*/
	ClientActiveObject* obj = getActiveObject(id);
	if(obj == NULL)
	{
		infostream<<"ClientEnvironment::removeActiveObject(): "
				<<"id="<<id<<" not found"<<std::endl;
		return;
	}
	obj->removeFromScene(true);
	delete obj;
	m_active_objects.erase(id);
}

void ClientEnvironment::processActiveObjectMessage(u16 id,
		const std::string &data)
{
	ClientActiveObject* obj = getActiveObject(id);
	if(obj == NULL)
	{
		infostream<<"ClientEnvironment::processActiveObjectMessage():"
				<<" got message for id="<<id<<", which doesn't exist."
				<<std::endl;
		return;
	}
	try
	{
		obj->processMessage(data);
	}
	catch(SerializationError &e)
	{
		errorstream<<"ClientEnvironment::processActiveObjectMessage():"
				<<" id="<<id<<" type="<<obj->getType()
				<<" SerializationError in processMessage(),"
				<<" message="<<serializeJsonString(data)
				<<std::endl;
	}
}

/*
	Callbacks for activeobjects
*/

void ClientEnvironment::damageLocalPlayer(u8 damage, bool handle_hp)
{
	LocalPlayer *lplayer = getLocalPlayer();
	assert(lplayer);

	if(handle_hp){
		if(lplayer->hp > damage)
			lplayer->hp -= damage;
		else
			lplayer->hp = 0;
	}

	ClientEnvEvent event;
	event.type = CEE_PLAYER_DAMAGE;
	event.player_damage.amount = damage;
	event.player_damage.send_to_server = handle_hp;
	m_client_event_queue.push_back(event);
}

void ClientEnvironment::updateLocalPlayerBreath(u16 breath)
{
	ClientEnvEvent event;
	event.type = CEE_PLAYER_BREATH;
	event.player_breath.amount = breath;
	m_client_event_queue.push_back(event);
}

/*
	Client likes to call these
*/

void ClientEnvironment::getActiveObjects(v3f origin, f32 max_d,
		std::vector<DistanceSortedActiveObject> &dest)
{
	for(std::map<u16, ClientActiveObject*>::iterator
			i = m_active_objects.begin();
			i != m_active_objects.end(); ++i)
	{
		ClientActiveObject* obj = i->second;

		f32 d = (obj->getPosition() - origin).getLength();

		if(d > max_d)
			continue;

		DistanceSortedActiveObject dso(obj, d);

		dest.push_back(dso);
	}
}

ClientEnvEvent ClientEnvironment::getClientEvent()
{
	ClientEnvEvent event;
	if(m_client_event_queue.empty())
		event.type = CEE_NONE;
	else {
		event = m_client_event_queue.front();
		m_client_event_queue.pop_front();
	}
	return event;
}