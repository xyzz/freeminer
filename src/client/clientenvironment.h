#pragma once

#include "clientobject.h"
#include "common/environment.h"

class ClientSimpleObject;
class ClientMap;

/*
	The client-side environment.

	This is not thread-safe.
	Must be called from main (irrlicht) thread (uses the SceneManager)
	Client uses an environment mutex.
*/

enum ClientEnvEventType
{
	CEE_NONE,
	CEE_PLAYER_DAMAGE,
	CEE_PLAYER_BREATH
};

struct ClientEnvEvent
{
	ClientEnvEventType type;
	union {
		struct{
		} none;
		struct{
			u8 amount;
			bool send_to_server;
		} player_damage;
		struct{
			u16 amount;
		} player_breath;
	};
};

class ClientEnvironment : public Environment
{
public:
	ClientEnvironment(ClientMap *map, scene::ISceneManager *smgr,
			ITextureSource *texturesource, IGameDef *gamedef,
			IrrlichtDevice *device);
	~ClientEnvironment();

	Map & getMap();
	ClientMap & getClientMap();

	IGameDef *getGameDef()
	{ return m_gamedef; }

	void step(f32 dtime, float uptime, int max_cycle_ms);

	virtual void addPlayer(Player *player);
	LocalPlayer * getLocalPlayer();
	
	/*
		ClientSimpleObjects
	*/

	void addSimpleObject(ClientSimpleObject *simple);

	/*
		ActiveObjects
	*/
	
	ClientActiveObject* getActiveObject(u16 id);

	/*
		Adds an active object to the environment.
		Environment handles deletion of object.
		Object may be deleted by environment immediately.
		If id of object is 0, assigns a free id to it.
		Returns the id of the object.
		Returns 0 if not added and thus deleted.
	*/
	u16 addActiveObject(ClientActiveObject *object);

	void addActiveObject(u16 id, u8 type, const std::string &init_data);
	void removeActiveObject(u16 id);

	void processActiveObjectMessage(u16 id, const std::string &data);

	/*
		Callbacks for activeobjects
	*/

	void damageLocalPlayer(u8 damage, bool handle_hp=true);
	void updateLocalPlayerBreath(u16 breath);

	/*
		Client likes to call these
	*/
	
	// Get all nearby objects
	void getActiveObjects(v3f origin, f32 max_d,
			std::vector<DistanceSortedActiveObject> &dest);
	
	// Get event from queue. CEE_NONE is returned if queue is empty.
	ClientEnvEvent getClientEvent();

	u16 m_attachements[USHRT_MAX];

	std::list<std::string> getPlayerNames()
	{ return m_player_names; }
	void addPlayerName(std::string name)
	{ m_player_names.push_back(name); }
	void removePlayerName(std::string name)
	{ m_player_names.remove(name); }
	void updateCameraOffset(v3s16 camera_offset)
	{ m_camera_offset = camera_offset; }
	v3s16 getCameraOffset()
	{ return m_camera_offset; }
	
private:
	ClientMap *m_map;
	scene::ISceneManager *m_smgr;
	ITextureSource *m_texturesource;
	IGameDef *m_gamedef;
	IrrlichtDevice *m_irr;
	std::map<u16, ClientActiveObject*> m_active_objects;
	u32 m_active_objects_client_last;
	u32 m_move_max_loop;
	std::list<ClientSimpleObject*> m_simple_objects;
	std::list<ClientEnvEvent> m_client_event_queue;
	IntervalLimiter m_active_object_light_update_interval;
	IntervalLimiter m_lava_hurt_interval;
	IntervalLimiter m_drowning_interval;
	IntervalLimiter m_breathing_interval;
	std::list<std::string> m_player_names;
	v3s16 m_camera_offset;
};