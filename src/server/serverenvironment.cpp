#include "server/serverenvironment.h"

#include "main.h"
#include "settings.h"
#include "key_value_storage.h"
#include "map.h"
#include "player.h"
#include "script/scripting_game.h"
#include "server/abmhandler.h"
#include "gamedef.h"
#include "nodedef.h"
#include "circuit.h"
#include "serverobject.h"
#include "content_object.h"
#include "util/timetaker.h"
#include "profiler.h"
#include "object_properties.h"

#define PP(x) "("<<(x).X<<","<<(x).Y<<","<<(x).Z<<")"

static void print_hexdump(std::ostream &o, const std::string &data)
{
	const int linelength = 16;
	for (int l = 0;; l++){
		int i0 = linelength * l;
		bool at_end = false;
		int thislinelength = linelength;
		if (i0 + thislinelength > (int)data.size()){
			thislinelength = data.size() - i0;
			at_end = true;
		}
		for (int di = 0; di<linelength; di++){
			int i = i0 + di;
			char buf[4];
			if (di<thislinelength)
				snprintf(buf, 4, "%.2x ", data[i]);
			else
				snprintf(buf, 4, "   ");
			o << buf;
		}
		o << " ";
		for (int di = 0; di<thislinelength; di++){
			int i = i0 + di;
			if (data[i] >= 32)
				o << data[i];
			else
				o << ".";
		}
		o << std::endl;
		if (at_end)
			break;
	}
}

static bool isFreeServerActiveObjectId(u16 id,
		std::map<u16, ServerActiveObject*> &objects)
{
	if(id == 0)
		return false;

	return objects.find(id) == objects.end();
}

static u16 getFreeServerActiveObjectId(
		std::map<u16, ServerActiveObject*> &objects)
{
	//try to reuse id's as late as possible
	static u16 last_used_id = 0;
	u16 startid = last_used_id;
	for(;;)
	{
		last_used_id ++;
		if(isFreeServerActiveObjectId(last_used_id, objects))
			return last_used_id;

		if(last_used_id == startid)
			return 0;
	}
}

ServerEnvironment::ServerEnvironment(ServerMap *map,
		GameScripting *scriptIface,
		Circuit* circuit,
		IGameDef *gamedef,
		const std::string &path_world) :
	m_abmhandler(NULL),
	m_game_time_start(0),
	m_map(map),
	m_script(scriptIface),
	m_circuit(circuit),
	m_gamedef(gamedef),
	m_path_world(path_world),
	m_send_recommended_timer(0),
	m_active_objects_last(0),
	m_active_block_abm_last(0),
	m_active_block_abm_dtime(0),
	m_active_block_timer_last(0),
	m_blocks_added_last(0),
	m_game_time_fraction_counter(0),
	m_recommended_send_interval(0.1),
	m_max_lag_estimate(0.1)
{
	m_game_time = 0;
	m_use_weather = g_settings->getBool("weather");
	try {
		m_key_value_storage = new KeyValueStorage(path_world, "key_value_storage");
		m_players_storage = new KeyValueStorage(path_world, "players");
	} catch(KeyValueStorageException &e) {
		errorstream << "Cant open KV database: "<< e.what() << std::endl;
	}
}

ServerEnvironment::~ServerEnvironment()
{
	// Clear active block list.
	// This makes the next one delete all active objects.
	m_active_blocks.clear();

	// Convert all objects to static and delete the active objects
	deactivateFarObjects(true);

	// Drop/delete map
	m_map->drop();

	// Delete ActiveBlockModifiers
	for(std::list<ABMWithState>::iterator
			i = m_abms.begin(); i != m_abms.end(); ++i){
		delete i->abm;
	}
	delete m_key_value_storage;
	delete m_players_storage;
}

Map & ServerEnvironment::getMap()
{
	return *m_map;
}

ServerMap & ServerEnvironment::getServerMap()
{
	return *m_map;
}

KeyValueStorage *ServerEnvironment::getKeyValueStorage()
{
	return m_key_value_storage;
}

bool ServerEnvironment::line_of_sight(v3f pos1, v3f pos2, float stepsize, v3s16 *p)
{
	float distance = pos1.getDistanceFrom(pos2);

	//calculate normalized direction vector
	v3f normalized_vector = v3f((pos2.X - pos1.X)/distance,
								(pos2.Y - pos1.Y)/distance,
								(pos2.Z - pos1.Z)/distance);

	//find out if there's a node on path between pos1 and pos2
	for (float i = 1; i < distance; i += stepsize) {
		v3s16 pos = floatToInt(v3f(normalized_vector.X * i,
				normalized_vector.Y * i,
				normalized_vector.Z * i) +pos1,BS);

		MapNode n = getMap().getNodeNoEx(pos);

		if(n.param0 != CONTENT_AIR) {
			if (p) {
				*p = pos;
			}
			return false;
		}
	}
	return true;
}

void ServerEnvironment::saveLoadedPlayers()
{
	auto i = m_players.begin();
	while (i != m_players.end())
	{
		auto *player = *i;
		savePlayer(player->getName());
		if(!player->peer_id && !player->getPlayerSAO() && player->refs <= 0) {
			delete player;
			i = m_players.erase(i);
		} else {
			++i;
		}
	}
}

void ServerEnvironment::savePlayer(const std::string &playername)
{
	auto *player = getPlayer(playername);
	if (!player)
		return;
	Json::Value player_json;
	player_json << *player;
	m_players_storage->put_json(std::string("p.") + player->getName(), player_json);
}

Player * ServerEnvironment::loadPlayer(const std::string &playername)
{
	auto *player = getPlayer(playername);
	bool newplayer = false;
	bool found = false;
	if (!player) {
		player = new RemotePlayer(m_gamedef);
		newplayer = true;
	}

	try {
		Json::Value player_json;
		m_players_storage->get_json(("p." + playername).c_str(), player_json);
		verbosestream<<"Reading kv player "<<playername<<std::endl;
		if (!player_json.empty()) {
			player_json >> *player;
			if (newplayer) {
				addPlayer(player);
			}
			return player;
		}
	} catch (...)  {
	}

	//TODO: REMOVE OLD SAVE TO FILE:

	if(!string_allowed(playername, PLAYERNAME_ALLOWED_CHARS) || !playername.size()) {
		infostream<<"Not loading player with invalid name: "<<playername<<std::endl;
		return nullptr;
	}

	std::string players_path = m_path_world + DIR_DELIM "players" DIR_DELIM;

	auto testplayer = new RemotePlayer(m_gamedef);
	std::string path = players_path + playername;
		// Open file and deserialize
		std::ifstream is(path.c_str(), std::ios_base::binary);
		if (!is.good()) {
			return NULL;
		}
		try {
		testplayer->deSerialize(is, path);
		} catch (SerializationError e) {
			errorstream<<e.what()<<std::endl;
			return nullptr;
		}
		is.close();
		if (testplayer->getName() == playername) {
			player = testplayer;
			found = true;
		}
	if (!found) {
		delete testplayer;
		infostream << "Player file for player " << playername
				<< " not found" << std::endl;
		return NULL;
	}
	if (newplayer) {
		addPlayer(player);
	}
	return player;
}

void ServerEnvironment::saveMeta()
{
	std::string path = m_path_world + DIR_DELIM "env_meta.txt";

	// Open file and serialize
	std::ostringstream ss(std::ios_base::binary);

	Settings args;
	args.setU64("game_time", m_game_time);
	args.setU64("time_of_day", getTimeOfDay());
	args.writeLines(ss);
	ss<<"EnvArgsEnd\n";

	if(!fs::safeWriteToFile(path, ss.str()))
	{
		errorstream<<"ServerEnvironment::saveMeta(): Failed to write "
				<<path<<std::endl;
	}
}

void ServerEnvironment::loadMeta()
{
	std::string path = m_path_world + DIR_DELIM "env_meta.txt";

	// Open file and deserialize
	std::ifstream is(path.c_str(), std::ios_base::binary);
	if(is.good() == false)
	{
		infostream<<"ServerEnvironment::loadMeta(): Failed to open "
				<<path<<std::endl;
		throw SerializationError("Couldn't load env meta");
	}

	Settings args;

	for(;;)
	{
		if(is.eof())
			return;
/*
			throw SerializationError
					("ServerEnvironment::loadMeta(): EnvArgsEnd not found");
*/
		std::string line;
		std::getline(is, line);
		std::string trimmedline = trim(line);
		if(trimmedline == "EnvArgsEnd")
			break;
		args.parseConfigLine(line);
	}

	try{
		m_game_time_start =
		m_game_time = args.getU64("game_time");
	}catch(SettingNotFoundException &e){
		// Getting this is crucial, otherwise timestamps are useless
		throw SerializationError("Couldn't load env meta game_time");
	}

	try{
		m_time_of_day = args.getU64("time_of_day");
	}catch(SettingNotFoundException &e){
		// This is not as important
		m_time_of_day = 9000;
	}
}

void ServerEnvironment::activateBlock(MapBlock *block, u32 additional_dtime)
{
	// Reset usage timer immediately, otherwise a block that becomes active
	// again at around the same time as it would normally be unloaded will
	// get unloaded incorrectly. (I think this still leaves a small possibility
	// of a race condition between this and server::AsyncRunStep, which only
	// some kind of synchronisation will fix, but it at least reduces the window
	// of opportunity for it to break from seconds to nanoseconds)
	block->resetUsageTimer();

	// Get time difference
	u32 dtime_s = 0;
	u32 stamp = block->getTimestamp();
	if(m_game_time > stamp && stamp != BLOCK_TIMESTAMP_UNDEFINED)
		dtime_s = m_game_time - block->getTimestamp();
	dtime_s += additional_dtime;

	/*infostream<<"ServerEnvironment::activateBlock(): block timestamp: "
			<<stamp<<", game time: "<<m_game_time<<std::endl;*/

	// Set current time as timestamp
	block->setTimestampNoChangedFlag(m_game_time);

	/*infostream<<"ServerEnvironment::activateBlock(): block is "
			<<dtime_s<<" seconds old."<<std::endl;*/

	// Activate stored objects
	activateObjects(block, dtime_s);

//	// Calculate weather conditions
//	m_map->updateBlockHeat(this, block->getPos() *  MAP_BLOCKSIZE, block);

	// Run node timers
	std::map<v3s16, NodeTimer> elapsed_timers =
		block->m_node_timers.step((float)dtime_s);
	if(!elapsed_timers.empty()){
		MapNode n;
		for(std::map<v3s16, NodeTimer>::iterator
				i = elapsed_timers.begin();
				i != elapsed_timers.end(); i++){
			n = block->getNodeNoEx(i->first);
			v3s16 p = i->first + block->getPosRelative();
			if(m_script->node_on_timer(p,n,i->second.elapsed))
				block->setNodeTimer(i->first,NodeTimer(i->second.timeout,0));
		}
	}

	/* Handle ActiveBlockModifiers */
	ABMHandler abmhandler(m_abms, dtime_s, this, false, true);
	abmhandler.apply(block, true);
}

void ServerEnvironment::addActiveBlockModifier(ActiveBlockModifier *abm)
{
	m_abms.push_back(ABMWithState(abm, this));
}

bool ServerEnvironment::setNode(v3s16 p, const MapNode &n, s16 fast)
{
	INodeDefManager *ndef = m_gamedef->ndef();
	MapNode n_old = m_map->getNodeNoEx(p);
	// Call destructor
	if(ndef->get(n_old).has_on_destruct)
		m_script->node_on_destruct(p, n_old);
	// Replace node

	if (fast) {
		try {
			MapNode nn = n;
			if (fast == 2)
				nn.param1 = n_old.param1;
			m_map->setNode(p, nn);
		} catch(InvalidPositionException &e) { }
	} else {
	bool succeeded = m_map->addNodeWithEvent(p, n);
	if(!succeeded)
		return false;
	}

	if(ndef->get(n).is_wire) {
		m_circuit->addWire(getMap(), ndef, p);
	}
	// Call circuit update
	if(ndef->get(n).is_circuit_element) {
		m_circuit->addElement(getMap(), ndef, p, ndef->get(n).circuit_element_states);
	}

	// Call post-destructor
	if(ndef->get(n_old).has_after_destruct)
		m_script->node_after_destruct(p, n_old);
	// Call constructor
	if(ndef->get(n).has_on_construct)
		m_script->node_on_construct(p, n);
	return true;
}

bool ServerEnvironment::removeNode(v3s16 p, s16 fast)
{
	INodeDefManager *ndef = m_gamedef->ndef();
	MapNode n_old = m_map->getNodeNoEx(p);
	// Call destructor
	if(ndef->get(n_old).has_on_destruct)
		m_script->node_on_destruct(p, n_old);
	// Replace with air
	// This is slightly optimized compared to addNodeWithEvent(air)
	if (fast) {
		MapNode n;
		try {
			if (fast == 2)
				n.param1 = n_old.param1;
			m_map->setNode(p, n);
		} catch(InvalidPositionException &e) { }
	} else {
	bool succeeded = m_map->removeNodeWithEvent(p);
	if(!succeeded)
		return false;
	}
	if(ndef->get(n_old).is_wire) {
		m_circuit->removeWire(*m_map, ndef, p, n_old);
	}
	if(ndef->get(n_old).is_circuit_element) {
		m_circuit->removeElement(p);
	}

	// Call post-destructor
	if(ndef->get(n_old).has_after_destruct)
		m_script->node_after_destruct(p, n_old);
	// Air doesn't require constructor
	return true;
}

bool ServerEnvironment::swapNode(v3s16 p, const MapNode &n)
{
	INodeDefManager *ndef = m_gamedef->ndef();
	MapNode n_old = m_map->getNodeNoEx(p);
	bool succeeded = m_map->addNodeWithEvent(p, n, false);
	if(succeeded) {
		MapNode n_new = n;
		if(ndef->get(n_new).is_circuit_element) {
			if(ndef->get(n_old).is_circuit_element) {
				m_circuit->updateElement(n_new, p, ndef, ndef->get(n_new).circuit_element_states);
			} else {
				if(ndef->get(n_old).is_wire) {
					m_circuit->removeWire(*m_map, ndef, p, n_old);
				}
				m_circuit->addElement(*m_map, ndef, p, ndef->get(n_new).circuit_element_states);
			}
		} else {
			if(ndef->get(n_old).is_circuit_element) {
				m_circuit->removeElement(p);
			} else if(ndef->get(n_old).is_wire) {
				m_circuit->removeWire(*m_map, ndef, p, n_old);
			}
			if(ndef->get(n_new).is_wire) {
				m_circuit->addWire(*m_map, ndef, p);
			}
		}
	}
	return succeeded;
}

std::set<u16> ServerEnvironment::getObjectsInsideRadius(v3f pos, float radius)
{
	std::set<u16> objects;
	for(std::map<u16, ServerActiveObject*>::iterator
			i = m_active_objects.begin();
			i != m_active_objects.end(); ++i)
	{
		ServerActiveObject* obj = i->second;
		u16 id = i->first;
		v3f objectpos = obj->getBasePosition();
		if(objectpos.getDistanceFrom(pos) > radius)
			continue;
		objects.insert(id);
	}
	return objects;
}

void ServerEnvironment::clearAllObjects()
{
	infostream<<"ServerEnvironment::clearAllObjects(): "
			<<"Removing all active objects"<<std::endl;
	std::list<u16> objects_to_remove;
	for(std::map<u16, ServerActiveObject*>::iterator
			i = m_active_objects.begin();
			i != m_active_objects.end(); ++i)
	{
		ServerActiveObject* obj = i->second;
		if(obj->getType() == ACTIVEOBJECT_TYPE_PLAYER)
			continue;
		u16 id = i->first;
		// Delete static object if block is loaded
		if(obj->m_static_exists){
			MapBlock *block = m_map->getBlockNoCreateNoEx(obj->m_static_block);
			if(block){
				block->m_static_objects.remove(id);
				block->raiseModified(MOD_STATE_WRITE_NEEDED,
						"clearAllObjects");
				obj->m_static_exists = false;
			}
		}
		// If known by some client, don't delete immediately
		if(obj->m_known_by_count > 0){
			obj->m_pending_deactivation = true;
			obj->m_removed = true;
			continue;
		}

		// Tell the object about removal
		obj->removingFromEnvironment();
		// Deregister in scripting api
		m_script->removeObjectReference(obj);

		// Delete active object
		if(obj->environmentDeletes())
			delete obj;
		// Id to be removed from m_active_objects
		objects_to_remove.push_back(id);
	}
	// Remove references from m_active_objects
	for(std::list<u16>::iterator i = objects_to_remove.begin();
			i != objects_to_remove.end(); ++i)
	{
		m_active_objects.erase(*i);
	}

	// Get list of loaded blocks
	std::list<v3s16> loaded_blocks;
	infostream<<"ServerEnvironment::clearAllObjects(): "
			<<"Listing all loaded blocks"<<std::endl;
	m_map->listAllLoadedBlocks(loaded_blocks);
	infostream<<"ServerEnvironment::clearAllObjects(): "
			<<"Done listing all loaded blocks: "
			<<loaded_blocks.size()<<std::endl;

	// Get list of loadable blocks
	std::list<v3s16> loadable_blocks;
	infostream<<"ServerEnvironment::clearAllObjects(): "
			<<"Listing all loadable blocks"<<std::endl;
	m_map->listAllLoadableBlocks(loadable_blocks);
	infostream<<"ServerEnvironment::clearAllObjects(): "
			<<"Done listing all loadable blocks: "
			<<loadable_blocks.size()
			<<", now clearing"<<std::endl;

	// Grab a reference on each loaded block to avoid unloading it
	for(std::list<v3s16>::iterator i = loaded_blocks.begin();
			i != loaded_blocks.end(); ++i)
	{
		v3s16 p = *i;
		MapBlock *block = m_map->getBlockNoCreateNoEx(p);
		assert(block);
		block->refGrab();
	}

	// Remove objects in all loadable blocks
	u32 unload_interval = g_settings->getS32("max_clearobjects_extra_loaded_blocks");
	unload_interval = MYMAX(unload_interval, 1);
	u32 report_interval = loadable_blocks.size() / 10;
	u32 num_blocks_checked = 0;
	u32 num_blocks_cleared = 0;
	u32 num_objs_cleared = 0;
	for(std::list<v3s16>::iterator i = loadable_blocks.begin();
			i != loadable_blocks.end(); ++i)
	{
		v3s16 p = *i;
		MapBlock *block = m_map->emergeBlock(p, false);
		if(!block){
			errorstream<<"ServerEnvironment::clearAllObjects(): "
					<<"Failed to emerge block "<<PP(p)<<std::endl;
			continue;
		}
		u32 num_stored = block->m_static_objects.m_stored.size();
		u32 num_active = block->m_static_objects.m_active.size();
		if(num_stored != 0 || num_active != 0){
			block->m_static_objects.m_stored.clear();
			block->m_static_objects.m_active.clear();
			block->raiseModified(MOD_STATE_WRITE_NEEDED,
					"clearAllObjects");
			num_objs_cleared += num_stored + num_active;
			num_blocks_cleared++;
		}
		num_blocks_checked++;

		if(report_interval != 0 &&
				num_blocks_checked % report_interval == 0){
			float percent = 100.0 * (float)num_blocks_checked /
					loadable_blocks.size();
			infostream<<"ServerEnvironment::clearAllObjects(): "
					<<"Cleared "<<num_objs_cleared<<" objects"
					<<" in "<<num_blocks_cleared<<" blocks ("
					<<percent<<"%)"<<std::endl;
		}
		if(num_blocks_checked % unload_interval == 0){
			m_map->unloadUnreferencedBlocks();
		}
	}
	m_map->unloadUnreferencedBlocks();

	// Drop references that were added above
	for(std::list<v3s16>::iterator i = loaded_blocks.begin();
			i != loaded_blocks.end(); ++i)
	{
		v3s16 p = *i;
		MapBlock *block = m_map->getBlockNoCreateNoEx(p);
		assert(block);
		block->refDrop();
	}

	infostream<<"ServerEnvironment::clearAllObjects(): "
			<<"Finished: Cleared "<<num_objs_cleared<<" objects"
			<<" in "<<num_blocks_cleared<<" blocks"<<std::endl;
}

void ServerEnvironment::step(float dtime, float uptime, int max_cycle_ms)
{
	DSTACK(__FUNCTION_NAME);

	//TimeTaker timer("ServerEnv step");

	/* Step time of day */
	stepTimeOfDay(dtime);

	// Update this one
	// NOTE: This is kind of funny on a singleplayer game, but doesn't
	// really matter that much.
	m_recommended_send_interval = g_settings->getFloat("dedicated_server_step");

	/*
		Increment game time
	*/
	{
		m_game_time_fraction_counter += dtime;
		u32 inc_i = (u32)m_game_time_fraction_counter;
		m_game_time += inc_i;
		m_game_time_fraction_counter -= (float)inc_i;
	}

	TimeTaker timer_step("Environment step");
	g_profiler->add("SMap: Blocks", getMap().m_blocks.size());

	/*
		Handle players
	*/
	{
		//TimeTaker timer_step_player("player step");
		//ScopeProfiler sp(g_profiler, "SEnv: handle players avg", SPT_AVG);
		for(std::list<Player*>::iterator i = m_players.begin();
				i != m_players.end(); ++i)
		{
			Player *player = *i;

			// Ignore disconnected players
			if(player->peer_id == 0)
				continue;

			// Move
			player->move(dtime, this, 100*BS);
		}
	}

	/*
	 * Update circuit
	 */
	m_circuit -> update(dtime, *m_map, m_gamedef->ndef());

	/*
		Manage active block list
	*/
	if(m_blocks_added_last || m_active_blocks_management_interval.step(dtime, 2.0))
	{
		//TimeTaker timer_s1("Manage active block list");
		ScopeProfiler sp(g_profiler, "SEnv: manage act. block list avg /2s", SPT_AVG);
		if (!m_blocks_added_last) {
		/*
			Get player block positions
		*/
		std::list<v3s16> players_blockpos;
		for(std::list<Player*>::iterator
				i = m_players.begin();
				i != m_players.end(); ++i)
		{
			Player *player = *i;
			// Ignore disconnected players
			if(player->peer_id == 0)
				continue;
			v3s16 blockpos = getNodeBlockPos(
					floatToInt(player->getPosition(), BS));
			players_blockpos.push_back(blockpos);
		}
		if (!m_blocks_added_last && g_settings->getBool("enable_force_load")) {
			//TimeTaker timer_s2("force load");
			for(std::map<u16, ServerActiveObject*>::iterator
				i = m_active_objects.begin();
				i != m_active_objects.end(); ++i)
			{
				ServerActiveObject* obj = i->second;
				if(obj->getType() == ACTIVEOBJECT_TYPE_PLAYER)
					continue;
				ObjectProperties* props = obj->accessObjectProperties();
				if(props->force_load){
					v3f objectpos = obj->getBasePosition();
					v3s16 blockpos = getNodeBlockPos(
					floatToInt(objectpos, BS));
					players_blockpos.push_back(blockpos);
				}
			}
		}

		/*
			Update list of active blocks, collecting changes
		*/
		const s16 active_block_range = g_settings->getS16("active_block_range");
		std::set<v3s16> blocks_removed;
		m_active_blocks.update(players_blockpos, active_block_range,
				blocks_removed, m_blocks_added);

		/*
			Handle removed blocks
		*/

		// Convert active objects that are no more in active blocks to static
		deactivateFarObjects(false);
		}

		/*
			Handle added blocks
		*/

		u32 n = 0, end_ms = porting::getTimeMs() + max_cycle_ms;
		m_blocks_added_last = 0;
		auto i = m_blocks_added.begin();
		for(; i != m_blocks_added.end(); ++i) {
			++n;
			v3s16 p = *i;
			MapBlock *block = m_map->getBlockOrEmerge(p);
			if(block==NULL){
				m_active_blocks.m_list.erase(p);
				continue;
			}

			activateBlock(block);
			/* infostream<<"Server: Block " << PP(p)
				<< " became active"<<std::endl; */
			if (porting::getTimeMs() > end_ms) {
				m_blocks_added_last = n;
				break;
			}
		}
		m_blocks_added.erase(m_blocks_added.begin(), i);
	}

	/*
		Mess around in active blocks
	*/
	if(m_active_block_timer_last || m_active_blocks_nodemetadata_interval.step(dtime, 1.0))
	{
		//TimeTaker timer_s1("Mess around in active blocks");
		//ScopeProfiler sp(g_profiler, "SEnv: mess in act. blocks avg /1s", SPT_AVG);

		//float dtime = 1.0;

		u32 n = 0, calls = 0, end_ms = porting::getTimeMs() + max_cycle_ms;
		for(std::set<v3s16>::iterator
				i = m_active_blocks.m_list.begin();
				i != m_active_blocks.m_list.end(); ++i)
		{
			if (n++ < m_active_block_timer_last)
				continue;
			else
				m_active_block_timer_last = 0;
			++calls;

			v3s16 p = *i;

			/*infostream<<"Server: Block ("<<p.X<<","<<p.Y<<","<<p.Z
					<<") being handled"<<std::endl;*/

			MapBlock *block = m_map->getBlockNoCreateNoEx(p);
			if(block==NULL)
				continue;

			// Reset block usage timer
			block->resetUsageTimer();

			// Set current time as timestamp
			block->setTimestampNoChangedFlag(m_game_time);
			// If time has changed much from the one on disk,
			// set block to be saved when it is unloaded
/*
			if(block->getTimestamp() > block->getDiskTimestamp() + 60)
				block->raiseModified(MOD_STATE_WRITE_AT_UNLOAD,
						"Timestamp older than 60s (step)");
*/

			// Run node timers
			if (!block->m_node_timers.m_uptime_last)  // not very good place, but minimum modifications
				block->m_node_timers.m_uptime_last = uptime - dtime;
			std::map<v3s16, NodeTimer> elapsed_timers =
				block->m_node_timers.step(uptime - block->m_node_timers.m_uptime_last);
			block->m_node_timers.m_uptime_last = uptime;
			if(!elapsed_timers.empty()){
				MapNode n;
				for(std::map<v3s16, NodeTimer>::iterator
						i = elapsed_timers.begin();
						i != elapsed_timers.end(); i++){
					n = block->getNodeNoEx(i->first);
					p = i->first + block->getPosRelative();
					if(m_script->node_on_timer(p,n,i->second.elapsed))
						block->setNodeTimer(i->first,NodeTimer(i->second.timeout,0));
				}
			}

			if (porting::getTimeMs() > end_ms) {
				m_active_block_timer_last = n;
				break;
		}
	}
		if (!calls)
			m_active_block_timer_last = 0;
	}

	g_profiler->add("SMap: Blocks: Active:", m_active_blocks.m_list.size());
	m_active_block_abm_dtime += dtime;
	const float abm_interval = 1.0;
	if(m_active_block_abm_last || m_active_block_modifier_interval.step(dtime, abm_interval))
	{
		ScopeProfiler sp(g_profiler, "SEnv: modify in blocks avg /1s", SPT_AVG);
		TimeTaker timer("modify in active blocks");

		// Initialize handling of ActiveBlockModifiers
		if (!m_active_block_abm_last || !m_abmhandler) {
			if (m_abmhandler)
				delete m_abmhandler;
			m_abmhandler = new ABMHandler(m_abms, m_active_block_abm_dtime, this, true, false);
		}
/*
		ABMHandler abmhandler(m_abms, m_active_block_abm_dtime, this, true);
*/
		u32 n = 0, calls = 0, end_ms = porting::getTimeMs() + max_cycle_ms;

		for(std::set<v3s16>::iterator
				i = m_active_blocks.m_list.begin();
				i != m_active_blocks.m_list.end(); ++i)
		{
			if (n++ < m_active_block_abm_last)
				continue;
			else
				m_active_block_abm_last = 0;
			++calls;

			ScopeProfiler sp(g_profiler, "SEnv: ABM one block avg", SPT_AVG);

			v3s16 p = *i;

			/*infostream<<"Server: Block ("<<p.X<<","<<p.Y<<","<<p.Z
					<<") being handled"<<std::endl;*/

			MapBlock *block = m_map->getBlockNoCreateNoEx(p);
			if(block==NULL)
				continue;

			auto lock = block->lock_unique_rec(std::chrono::milliseconds(1));
			if (!lock->owns_lock())
				continue;
			// Set current time as timestamp
			block->setTimestampNoChangedFlag(m_game_time);

			/* Handle ActiveBlockModifiers */
			m_abmhandler->apply(block);

			if (porting::getTimeMs() > end_ms) {
				m_active_block_abm_last = n;
				break;
			}
		}
		if (!calls)
			m_active_block_abm_last = 0;

/*
		if(m_active_block_abm_last) {
			infostream<<"WARNING: active block modifiers ("
					<<calls<<"/"<<m_active_blocks.m_list.size()<<" to "<<m_active_block_abm_last<<") took "
					<<porting::getTimeMs()-end_ms + u32(1000 * m_recommended_send_interval)<<"ms "
					<<std::endl;
		}
*/
		if (!m_active_block_abm_last)
			m_active_block_abm_dtime = 0;
	}

	/*
		Step script environment (run global on_step())
	*/
	{
	ScopeProfiler sp(g_profiler, "SEnv: environment_Step AVG", SPT_AVG);
	TimeTaker timer("environment_Step");
	m_script->environment_Step(dtime);
	}
	/*
		Step active objects
	*/
	{
		//ScopeProfiler sp(g_profiler, "SEnv: step act. objs avg", SPT_AVG);
		//TimeTaker timer("Step active objects");

		g_profiler->add("SEnv: Objects:", m_active_objects.size());

		// This helps the objects to send data at the same time
		bool send_recommended = false;
		m_send_recommended_timer += dtime;
		if(m_send_recommended_timer > getSendRecommendedInterval())
		{
			m_send_recommended_timer -= getSendRecommendedInterval();
			if (m_send_recommended_timer > getSendRecommendedInterval() * 2) {
				m_send_recommended_timer = 0;
			}
			send_recommended = true;
		}
		bool only_peaceful_mobs = g_settings->getBool("only_peaceful_mobs");
		u32 n = 0, calls = 0, end_ms = porting::getTimeMs() + max_cycle_ms;
		for(std::map<u16, ServerActiveObject*>::iterator
				i = m_active_objects.begin();
				i != m_active_objects.end(); ++i)
		{
			if (n++ < m_active_objects_last)
				continue;
			else
				m_active_objects_last = 0;
			++calls;
			ServerActiveObject* obj = i->second;
			// Remove non-peaceful mobs on peaceful mode
			if(only_peaceful_mobs){
				if(!obj->isPeaceful())
					obj->m_removed = true;
			}
			// Don't step if is to be removed or stored statically
			if(obj->m_removed || obj->m_pending_deactivation)
				continue;
			// Step object
			if (!obj->m_uptime_last)  // not very good place, but minimum modifications
				obj->m_uptime_last = uptime - dtime;
			obj->step(uptime - obj->m_uptime_last, send_recommended);
			obj->m_uptime_last = uptime;
			// Read messages from object
			while(!obj->m_messages_out.empty())
			{
				m_active_object_messages.push_back(
						obj->m_messages_out.pop_front());
			}

			if (porting::getTimeMs() > end_ms) {
				m_active_objects_last = n;
				break;
			}
		}
		if (!calls)
			m_active_objects_last = 0;
	}

	/*
		Manage active objects
	*/
	if(m_object_management_interval.step(dtime, 0.5))
	{
		//TimeTaker timer("Manage active objects");
		//ScopeProfiler sp(g_profiler, "SEnv: remove removed objs avg /.5s", SPT_AVG);
		/*
			Remove objects that satisfy (m_removed && m_known_by_count==0)
		*/
		removeRemovedObjects();
	}
}

ServerActiveObject* ServerEnvironment::getActiveObject(u16 id)
{
	std::map<u16, ServerActiveObject*>::iterator n;
	n = m_active_objects.find(id);
	if(n == m_active_objects.end())
		return NULL;
	return n->second;
}

u16 ServerEnvironment::addActiveObject(ServerActiveObject *object)
{
	assert(object);
	m_added_objects++;
	u16 id = addActiveObjectRaw(object, true, 0);
	return id;
}

/*
	Finds out what new objects have been added to
	inside a radius around a position
*/
void ServerEnvironment::getAddedActiveObjects(v3s16 pos, s16 radius,
		std::set<u16> &current_objects,
		std::set<u16> &added_objects)
{
	v3f pos_f = intToFloat(pos, BS);
	f32 radius_f = radius * BS;
	/*
		Go through the object list,
		- discard m_removed objects,
		- discard objects that are too far away,
		- discard objects that are found in current_objects.
		- add remaining objects to added_objects
	*/
	for(std::map<u16, ServerActiveObject*>::iterator
			i = m_active_objects.begin();
			i != m_active_objects.end(); ++i)
	{
		u16 id = i->first;
		// Get object
		ServerActiveObject *object = i->second;
		if(object == NULL)
			continue;
		// Discard if removed or deactivating
		if(object->m_removed || object->m_pending_deactivation)
			continue;
		if(object->unlimitedTransferDistance() == false){
			// Discard if too far
			f32 distance_f = object->getBasePosition().getDistanceFrom(pos_f);
			if(distance_f > radius_f)
				continue;
		}
		// Discard if already on current_objects
		std::set<u16>::iterator n;
		n = current_objects.find(id);
		if(n != current_objects.end())
			continue;
		// Add to added_objects
		added_objects.insert(id);
	}
}

/*
	Finds out what objects have been removed from
	inside a radius around a position
*/
void ServerEnvironment::getRemovedActiveObjects(v3s16 pos, s16 radius,
		std::set<u16> &current_objects,
		std::set<u16> &removed_objects)
{
	v3f pos_f = intToFloat(pos, BS);
	f32 radius_f = radius * BS;
	/*
		Go through current_objects; object is removed if:
		- object is not found in m_active_objects (this is actually an
		  error condition; objects should be set m_removed=true and removed
		  only after all clients have been informed about removal), or
		- object has m_removed=true, or
		- object is too far away
	*/
	for(std::set<u16>::iterator
			i = current_objects.begin();
			i != current_objects.end(); ++i)
	{
		u16 id = *i;
		ServerActiveObject *object = getActiveObject(id);

		if(object == NULL){
			infostream<<"ServerEnvironment::getRemovedActiveObjects():"
					<<" object in current_objects is NULL"<<std::endl;
			removed_objects.insert(id);
			continue;
		}

		if(object->m_removed || object->m_pending_deactivation)
		{
			removed_objects.insert(id);
			continue;
		}

		// If transfer distance is unlimited, don't remove
		if(object->unlimitedTransferDistance())
			continue;

		f32 distance_f = object->getBasePosition().getDistanceFrom(pos_f);

		if(distance_f >= radius_f)
		{
			removed_objects.insert(id);
			continue;
		}

		// Not removed
	}
}

ActiveObjectMessage ServerEnvironment::getActiveObjectMessage()
{
	if(m_active_object_messages.empty())
		return ActiveObjectMessage(0);

	ActiveObjectMessage message = m_active_object_messages.front();
	m_active_object_messages.pop_front();
	return message;
}

/*
	************ Private methods *************
*/

u16 ServerEnvironment::addActiveObjectRaw(ServerActiveObject *object,
		bool set_changed, u32 dtime_s)
{
	assert(object);
	if(object->getId() == 0){
		u16 new_id = getFreeServerActiveObjectId(m_active_objects);
		if(new_id == 0)
		{
			errorstream<<"ServerEnvironment::addActiveObjectRaw(): "
					<<"no free ids available"<<std::endl;
			if(object->environmentDeletes())
				delete object;
			return 0;
		}
		object->setId(new_id);
	}
	else{
		verbosestream<<"ServerEnvironment::addActiveObjectRaw(): "
				<<"supplied with id "<<object->getId()<<std::endl;
	}
	if(isFreeServerActiveObjectId(object->getId(), m_active_objects) == false)
	{
		errorstream<<"ServerEnvironment::addActiveObjectRaw(): "
				<<"id is not free ("<<object->getId()<<")"<<std::endl;
		if(object->environmentDeletes())
			delete object;
		return 0;
	}
	/*infostream<<"ServerEnvironment::addActiveObjectRaw(): "
			<<"added (id="<<object->getId()<<")"<<std::endl;*/

	m_active_objects[object->getId()] = object;

/*
	verbosestream<<"ServerEnvironment::addActiveObjectRaw(): "
			<<"Added id="<<object->getId()<<"; there are now "
			<<m_active_objects.size()<<" active objects."
			<<std::endl;
*/

	// Register reference in scripting api (must be done before post-init)
	m_script->addObjectReference(object);
	// Post-initialize object
	object->addedToEnvironment(dtime_s);

	// Add static data to block
	if(object->isStaticAllowed())
	{
		// Add static object to active static list of the block
		v3f objectpos = object->getBasePosition();
		std::string staticdata = object->getStaticData();
		StaticObject s_obj(object->getType(), objectpos, staticdata);
		// Add to the block where the object is located in
		v3s16 blockpos = getNodeBlockPos(floatToInt(objectpos, BS));
		MapBlock *block = m_map->emergeBlock(blockpos);
		if(block){
			block->m_static_objects.m_active.set(object->getId(), s_obj);
			object->m_static_exists = true;
			object->m_static_block = blockpos;

			if(set_changed)
				block->raiseModified(MOD_STATE_WRITE_NEEDED,
						"addActiveObjectRaw");
		} else {
			v3s16 p = floatToInt(objectpos, BS);
			errorstream<<"ServerEnvironment::addActiveObjectRaw(): "
					<<"could not emerge block for storing id="<<object->getId()
					<<" statically (pos="<<PP(p)<<")"<<std::endl;
		}
	}

	return object->getId();
}

/*
	Remove objects that satisfy (m_removed && m_known_by_count==0)
*/
void ServerEnvironment::removeRemovedObjects()
{
	TimeTaker timer("ServerEnvironment::removeRemovedObjects()");
	std::list<u16> objects_to_remove;
	for(std::map<u16, ServerActiveObject*>::iterator
			i = m_active_objects.begin();
			i != m_active_objects.end(); ++i)
	{
		u16 id = i->first;
		ServerActiveObject* obj = i->second;
		// This shouldn't happen but check it
		if(obj == NULL)
		{
			infostream<<"NULL object found in ServerEnvironment"
					<<" while finding removed objects. id="<<id<<std::endl;
			// Id to be removed from m_active_objects
			objects_to_remove.push_back(id);
			continue;
		}

		/*
			We will delete objects that are marked as removed or thatare
			waiting for deletion after deactivation
		*/
		if(obj->m_removed == false && obj->m_pending_deactivation == false)
			continue;

		/*
			Delete static data from block if is marked as removed
		*/
		if(obj->m_static_exists && obj->m_removed)
		{
			MapBlock *block = m_map->emergeBlock(obj->m_static_block, false);
			if (block) {
				block->m_static_objects.remove(id);
				block->raiseModified(MOD_STATE_WRITE_NEEDED,
						"removeRemovedObjects/remove");
				obj->m_static_exists = false;
			} else {
				infostream<<"Failed to emerge block from which an object to "
						<<"be removed was loaded from. id="<<id<<std::endl;
			}
		}

		// If m_known_by_count > 0, don't actually remove. On some future
		// invocation this will be 0, which is when removal will continue.
		if(obj->m_known_by_count > 0)
			continue;

		/*
			Move static data from active to stored if not marked as removed
		*/
		if(obj->m_static_exists && !obj->m_removed){
			MapBlock *block = m_map->emergeBlock(obj->m_static_block, false);
			if (block) {
				std::map<u16, StaticObject>::iterator i =
						block->m_static_objects.m_active.find(id);
				if(i != block->m_static_objects.m_active.end()){
					block->m_static_objects.m_stored.push_back(i->second);
					block->m_static_objects.m_active.erase(id);
					block->raiseModified(MOD_STATE_WRITE_NEEDED,
							"removeRemovedObjects/deactivate");
				}
			} else {
				infostream<<"Failed to emerge block from which an object to "
						<<"be deactivated was loaded from. id="<<id<<std::endl;
			}
		}

		// Tell the object about removal
		obj->removingFromEnvironment();
		// Deregister in scripting api
		m_script->removeObjectReference(obj);

		// Delete
		if(obj->environmentDeletes())
			delete obj;
		// Id to be removed from m_active_objects
		objects_to_remove.push_back(id);
	}
	// Remove references from m_active_objects
	for(std::list<u16>::iterator i = objects_to_remove.begin();
			i != objects_to_remove.end(); ++i)
	{
		m_active_objects.erase(*i);
	}
}

/*
	Convert stored objects from blocks near the players to active.
*/
void ServerEnvironment::activateObjects(MapBlock *block, u32 dtime_s)
{
	if(block==NULL)
		return;
	// Ignore if no stored objects (to not set changed flag)
	if(block->m_static_objects.m_stored.size() == 0)
		return;
/*
	verbosestream<<"ServerEnvironment::activateObjects(): "
			<<"activating objects of block "<<PP(block->getPos())
			<<" ("<<block->m_static_objects.m_stored.size()
			<<" objects)"<<std::endl;
*/
	bool large_amount = (block->m_static_objects.m_stored.size() > g_settings->getU16("max_objects_per_block"));
	if(large_amount){
		errorstream<<"suspiciously large amount of objects detected: "
				<<block->m_static_objects.m_stored.size()<<" in "
				<<PP(block->getPos())
				<<"; removing all of them."<<std::endl;
		// Clear stored list
		block->m_static_objects.m_stored.clear();
		block->raiseModified(MOD_STATE_WRITE_NEEDED,
				"stored list cleared in activateObjects due to "
				"large amount of objects");
		return;
	}

	// Activate stored objects
	std::list<StaticObject> new_stored;
	for(std::list<StaticObject>::iterator
			i = block->m_static_objects.m_stored.begin();
			i != block->m_static_objects.m_stored.end(); ++i)
	{
		/*infostream<<"Server: Creating an active object from "
				<<"static data"<<std::endl;*/
		StaticObject &s_obj = *i;
		// Create an active object from the data
		ServerActiveObject *obj = ServerActiveObject::create
				(s_obj.type, this, 0, s_obj.pos, s_obj.data);
		// If couldn't create object, store static data back.
		if(obj==NULL)
		{
			errorstream<<"ServerEnvironment::activateObjects(): "
					<<"failed to create active object from static object "
					<<"in block "<<PP(s_obj.pos/BS)
					<<" type="<<(int)s_obj.type<<" data:"<<std::endl;
			print_hexdump(verbosestream, s_obj.data);

			new_stored.push_back(s_obj);
			continue;
		}
/*
		verbosestream<<"ServerEnvironment::activateObjects(): "
				<<"activated static object pos="<<PP(s_obj.pos/BS)
				<<" type="<<(int)s_obj.type<<std::endl;
*/
		// This will also add the object to the active static list
		addActiveObjectRaw(obj, false, dtime_s);
	}
	// Clear stored list
	block->m_static_objects.m_stored.clear();
	// Add leftover failed stuff to stored list
	for(std::list<StaticObject>::iterator
			i = new_stored.begin();
			i != new_stored.end(); ++i)
	{
		StaticObject &s_obj = *i;
		block->m_static_objects.m_stored.push_back(s_obj);
	}

	// Turn the active counterparts of activated objects not pending for
	// deactivation
	for(std::map<u16, StaticObject>::iterator
			i = block->m_static_objects.m_active.begin();
			i != block->m_static_objects.m_active.end(); ++i)
	{
		u16 id = i->first;
		ServerActiveObject *object = getActiveObject(id);
		if (!object)
			continue;
		object->m_pending_deactivation = false;
	}

	/*
		Note: Block hasn't really been modified here.
		The objects have just been activated and moved from the stored
		static list to the active static list.
		As such, the block is essentially the same.
		Thus, do not call block->raiseModified(MOD_STATE_WRITE_NEEDED).
		Otherwise there would be a huge amount of unnecessary I/O.
	*/
}

/*
	Convert objects that are not standing inside active blocks to static.

	If m_known_by_count != 0, active object is not deleted, but static
	data is still updated.

	If force_delete is set, active object is deleted nevertheless. It
	shall only be set so in the destructor of the environment.

	If block wasn't generated (not in memory or on disk),
*/
void ServerEnvironment::deactivateFarObjects(bool force_delete)
{
	//ScopeProfiler sp(g_profiler, "SEnv: deactivateFarObjects");

	std::list<u16> objects_to_remove;
	for(std::map<u16, ServerActiveObject*>::iterator
			i = m_active_objects.begin();
			i != m_active_objects.end(); ++i)
	{
		ServerActiveObject* obj = i->second;
		if (!obj)
			continue;

		// Do not deactivate if static data creation not allowed
		if(!force_delete && !obj->isStaticAllowed())
			continue;

		// If pending deactivation, let removeRemovedObjects() do it
		if(!force_delete && obj->m_pending_deactivation)
			continue;

		u16 id = i->first;
		v3f objectpos = obj->getBasePosition();

		// The block in which the object resides in
		v3s16 blockpos_o = getNodeBlockPos(floatToInt(objectpos, BS));

		// If object's static data is stored in a deactivated block and object
		// is actually located in an active block, re-save to the block in
		// which the object is actually located in.
		if(!force_delete &&
				obj->m_static_exists &&
				!m_active_blocks.contains(obj->m_static_block) &&
				 m_active_blocks.contains(blockpos_o))
		{
			v3s16 old_static_block = obj->m_static_block;

			// Save to block where object is located
			MapBlock *block = m_map->emergeBlock(blockpos_o, false);
			if(!block){
				errorstream<<"ServerEnvironment::deactivateFarObjects(): "
						<<"Could not save object id="<<id
						<<" to it's current block "<<PP(blockpos_o)
						<<std::endl;
				continue;
			}
			std::string staticdata_new = obj->getStaticData();
			StaticObject s_obj(obj->getType(), objectpos, staticdata_new);
			block->m_static_objects.insert(id, s_obj);
			obj->m_static_block = blockpos_o;
			block->raiseModified(MOD_STATE_WRITE_NEEDED,
					"deactivateFarObjects: Static data moved in");

			// Delete from block where object was located
			block = m_map->emergeBlock(old_static_block, false);
			if(!block){
				errorstream<<"ServerEnvironment::deactivateFarObjects(): "
						<<"Could not delete object id="<<id
						<<" from it's previous block "<<PP(old_static_block)
						<<std::endl;
				continue;
			}
			block->m_static_objects.remove(id);
			block->raiseModified(MOD_STATE_WRITE_NEEDED,
					"deactivateFarObjects: Static data moved out");
			continue;
		}

		// If block is active, don't remove
		if(!force_delete && m_active_blocks.contains(blockpos_o))
			continue;

/*
		verbosestream<<"ServerEnvironment::deactivateFarObjects(): "
				<<"deactivating object id="<<id<<" on inactive block "
				<<PP(blockpos_o)<<std::endl;
*/

		// If known by some client, don't immediately delete.
		bool pending_delete = (obj->m_known_by_count > 0 && !force_delete);

		/*
			Update the static data
		*/

		if(obj->isStaticAllowed())
		{
			// Create new static object
			std::string staticdata_new = obj->getStaticData();
			StaticObject s_obj(obj->getType(), objectpos, staticdata_new);

			bool stays_in_same_block = false;
			bool data_changed = true;

			if(obj->m_static_exists){
				if(obj->m_static_block == blockpos_o)
					stays_in_same_block = true;

				MapBlock *block = m_map->emergeBlock(obj->m_static_block, false);
				if (!block)
					continue;

				std::map<u16, StaticObject>::iterator n =
						block->m_static_objects.m_active.find(id);
				if(n != block->m_static_objects.m_active.end()){
					StaticObject static_old = n->second;

					float save_movem = obj->getMinimumSavedMovement();

					if(static_old.data == staticdata_new &&
							(static_old.pos - objectpos).getLength() < save_movem)
						data_changed = false;
				} else {
					errorstream<<"ServerEnvironment::deactivateFarObjects(): "
							<<"id="<<id<<" m_static_exists=true but "
							<<"static data doesn't actually exist in "
							<<PP(obj->m_static_block)<<std::endl;
				}
			}

			bool shall_be_written = (!stays_in_same_block || data_changed);

			// Delete old static object
			if(obj->m_static_exists)
			{
				MapBlock *block = m_map->emergeBlock(obj->m_static_block, false);
				if(block)
				{
					block->m_static_objects.remove(id);
					obj->m_static_exists = false;
					// Only mark block as modified if data changed considerably
					if(shall_be_written)
						block->raiseModified(MOD_STATE_WRITE_NEEDED,
								"deactivateFarObjects: Static data "
								"changed considerably");
				}
			}

			// Add to the block where the object is located in
			v3s16 blockpos = getNodeBlockPos(floatToInt(objectpos, BS));
			// Get or generate the block
			MapBlock *block = NULL;
			try{
				block = m_map->emergeBlock(blockpos);
			} catch(InvalidPositionException &e){
				// Handled via NULL pointer
				// NOTE: emergeBlock's failure is usually determined by it
				//       actually returning NULL
			}

			if(block)
			{
				if(block->m_static_objects.m_stored.size() >= g_settings->getU16("max_objects_per_block")){
					errorstream<<"ServerEnv: Trying to store id="<<obj->getId()
							<<" statically but block "<<PP(blockpos)
							<<" already contains "
							<<block->m_static_objects.m_stored.size()
							<<" objects."
							<<" Forcing delete."<<std::endl;
					force_delete = true;
				} else {
					// If static counterpart already exists in target block,
					// remove it first.
					// This shouldn't happen because the object is removed from
					// the previous block before this according to
					// obj->m_static_block, but happens rarely for some unknown
					// reason. Unsuccessful attempts have been made to find
					// said reason.
					if(id && block->m_static_objects.m_active.find(id) != block->m_static_objects.m_active.end()){
						infostream<<"ServerEnv: WARNING: Performing hack #83274"
								<<std::endl;
						block->m_static_objects.remove(id);
					}
					// Store static data
					u16 store_id = pending_delete ? id : 0;
					block->m_static_objects.insert(store_id, s_obj);

					// Only mark block as modified if data changed considerably
					if(shall_be_written)
						block->raiseModified(MOD_STATE_WRITE_NEEDED,
								"deactivateFarObjects: Static data "
								"changed considerably");

					obj->m_static_exists = true;
					obj->m_static_block = block->getPos();
				}
			}
			else{
				if(!force_delete){
					v3s16 p = floatToInt(objectpos, BS);
					errorstream<<"ServerEnv: Could not find or generate "
							<<"a block for storing id="<<obj->getId()
							<<" statically (pos="<<PP(p)<<")"<<std::endl;
					continue;
				}
			}
		}

		/*
			If known by some client, set pending deactivation.
			Otherwise delete it immediately.
		*/

		if(pending_delete && !force_delete)
		{
			verbosestream<<"ServerEnvironment::deactivateFarObjects(): "
					<<"object id="<<id<<" is known by clients"
					<<"; not deleting yet"<<std::endl;

			obj->m_pending_deactivation = true;
			continue;
		}

/*
		verbosestream<<"ServerEnvironment::deactivateFarObjects(): "
				<<"object id="<<id<<" is not known by clients"
				<<"; deleting"<<std::endl;
*/

		// Tell the object about removal
		obj->removingFromEnvironment();
		// Deregister in scripting api
		m_script->removeObjectReference(obj);

		// Delete active object
		if(obj->environmentDeletes())
			delete obj;
		// Id to be removed from m_active_objects
		objects_to_remove.push_back(id);
	}

	//if(m_active_objects.size()) verbosestream<<"ServerEnvironment::deactivateFarObjects(): deactivated="<<objects_to_remove.size()<< " from="<<m_active_objects.size()<<std::endl;

	// Remove references from m_active_objects
	for(std::list<u16>::iterator i = objects_to_remove.begin();
			i != objects_to_remove.end(); ++i)
	{
		m_active_objects.erase(*i);
	}
}