#pragma once

#include <list>

#include "common/environment.h"
#include "server/activeblocklist.h"
#include "server/abmwithstate.h"


class ABMHandler;
/*
	The server-side environment.

	This is not thread-safe. Server uses an environment mutex.
*/

class ServerEnvironment : public Environment
{
public:
	ServerEnvironment(ServerMap *map, GameScripting *scriptIface,
			Circuit* circuit,
			IGameDef *gamedef, const std::string &path_world);
	~ServerEnvironment();

	Map & getMap();

	ServerMap & getServerMap();

	//TODO find way to remove this fct!
	GameScripting* getScriptIface()
		{ return m_script; }

	IGameDef *getGameDef()
		{ return m_gamedef; }

	float getSendRecommendedInterval()
		{ return m_recommended_send_interval; }

	//Player * getPlayer(u16 peer_id) { return Environment::getPlayer(peer_id); };
	//Player * getPlayer(const std::string &name);

	KeyValueStorage *getKeyValueStorage();

	// Save players
	void saveLoadedPlayers();
	void savePlayer(const std::string &playername);
	Player *loadPlayer(const std::string &playername);

	/*
		Save and load time of day and game timer
	*/
	void saveMeta();
	void loadMeta();

	/*
		External ActiveObject interface
		-------------------------------------------
	*/

	ServerActiveObject* getActiveObject(u16 id);

	/*
		Add an active object to the environment.
		Environment handles deletion of object.
		Object may be deleted by environment immediately.
		If id of object is 0, assigns a free id to it.
		Returns the id of the object.
		Returns 0 if not added and thus deleted.
	*/
	u16 addActiveObject(ServerActiveObject *object);
	
	/*
		Add an active object as a static object to the corresponding
		MapBlock.
		Caller allocates memory, ServerEnvironment frees memory.
		Return value: true if succeeded, false if failed.
		(note:  not used, pending removal from engine)
	*/
	//bool addActiveObjectAsStatic(ServerActiveObject *object);
	
	/*
		Find out what new objects have been added to
		inside a radius around a position
	*/
	void getAddedActiveObjects(v3s16 pos, s16 radius,
			std::set<u16> &current_objects,
			std::set<u16> &added_objects);

	/*
		Find out what new objects have been removed from
		inside a radius around a position
	*/
	void getRemovedActiveObjects(v3s16 pos, s16 radius,
			std::set<u16> &current_objects,
			std::set<u16> &removed_objects);
	
	/*
		Get the next message emitted by some active object.
		Returns a message with id=0 if no messages are available.
	*/
	ActiveObjectMessage getActiveObjectMessage();

	/*
		Activate objects and dynamically modify for the dtime determined
		from timestamp and additional_dtime
	*/
	void activateBlock(MapBlock *block, u32 additional_dtime=0);

	/*
		ActiveBlockModifiers
		-------------------------------------------
	*/

	void addActiveBlockModifier(ActiveBlockModifier *abm);

	/*
		Other stuff
		-------------------------------------------
	*/

	// Script-aware node setters
	bool setNode(v3s16 p, const MapNode &n, s16 fast = 0);
	bool removeNode(v3s16 p, s16 fast = 0);
	bool swapNode(v3s16 p, const MapNode &n);
	
	// Find all active objects inside a radius around a point
	std::set<u16> getObjectsInsideRadius(v3f pos, float radius);
	
	// Clear all objects, loading and going through every MapBlock
	void clearAllObjects();
	
	// This makes stuff happen
	void step(f32 dtime, float uptime, int max_cycle_ms);
	
	//check if there's a line of sight between two positions
	bool line_of_sight(v3f pos1, v3f pos2, float stepsize=1.0, v3s16 *p=NULL);

	u32 getGameTime() { return m_game_time; }

	void reportMaxLagEstimate(float f) { m_max_lag_estimate = f; }
	float getMaxLagEstimate() { return m_max_lag_estimate; }
	
	// is weather active in this environment?
	bool m_use_weather;
	ABMHandler * m_abmhandler;

	std::set<v3s16>* getForceloadedBlocks() { return &m_active_blocks.m_forceloaded_list; };
	
	u32 m_game_time_start;

private:

	/*
		Internal ActiveObject interface
		-------------------------------------------
	*/

	/*
		Add an active object to the environment.

		Called by addActiveObject.

		Object may be deleted by environment immediately.
		If id of object is 0, assigns a free id to it.
		Returns the id of the object.
		Returns 0 if not added and thus deleted.
	*/
	u16 addActiveObjectRaw(ServerActiveObject *object, bool set_changed, u32 dtime_s);
	
	/*
		Remove all objects that satisfy (m_removed && m_known_by_count==0)
	*/
	void removeRemovedObjects();
	
	/*
		Convert stored objects from block to active
	*/
	void activateObjects(MapBlock *block, u32 dtime_s);
	
	/*
		Convert objects that are not in active blocks to static.

		If m_known_by_count != 0, active object is not deleted, but static
		data is still updated.

		If force_delete is set, active object is deleted nevertheless. It
		shall only be set so in the destructor of the environment.
	*/
	void deactivateFarObjects(bool force_delete);

	/*
		Member variables
	*/

	// The map
	ServerMap *m_map;
	// Lua state
	GameScripting* m_script;
	// Circuit manager
	Circuit* m_circuit;
	// Game definition
	IGameDef *m_gamedef;
	// Key-value storage
	KeyValueStorage *m_key_value_storage;
	KeyValueStorage *m_players_storage;
	// World path
	const std::string m_path_world;
	// Active object list
	std::map<u16, ServerActiveObject*> m_active_objects;
	// Outgoing network message buffer for active objects
	std::list<ActiveObjectMessage> m_active_object_messages;
	// Some timers
	float m_send_recommended_timer;
	IntervalLimiter m_object_management_interval;
	// List of active blocks
	ActiveBlockList m_active_blocks;
	IntervalLimiter m_active_blocks_management_interval;
	IntervalLimiter m_active_block_modifier_interval;
	IntervalLimiter m_active_blocks_nodemetadata_interval;
	//loop breakers
	u32 m_active_objects_last;
	u32 m_active_block_abm_last;
	float m_active_block_abm_dtime;
	u32 m_active_block_timer_last;
	std::set<v3s16> m_blocks_added;
	u32 m_blocks_added_last;
	// Time from the beginning of the game in seconds.
	// Incremented in step().
	std::atomic_uint m_game_time;
	// A helper variable for incrementing the latter
	float m_game_time_fraction_counter;
	std::list<ABMWithState> m_abms;
	// An interval for generally sending object positions and stuff
	float m_recommended_send_interval;
	// Estimate for general maximum lag as determined by server.
	// Can raise to high values like 15s with eg. map generation mods.
	float m_max_lag_estimate;
};