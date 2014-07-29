/*
environment.h
Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>
*/

/*
This file is part of Freeminer.

Freeminer is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

Freeminer  is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Freeminer.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef ENVIRONMENT_HEADER
#define ENVIRONMENT_HEADER

/*
	This class is the game's environment.
	It contains:
	- The map
	- Players
	- Other objects
	- The current time in the game
	- etc.
*/

#include <set>
#include <list>
#include <map>
#include "irr_v3d.h"
#include "activeobject.h"
#include "util/numeric.h"
#include "mapnode.h"
#include "mapblock.h"
#include "connection.h"
#include "fmbitset.h"
#include "util/lock.h"

class ServerEnvironment;
class ActiveBlockModifier;
class ServerActiveObject;
class ITextureSource;
class IGameDef;
class Map;
class ServerMap;
class GameScripting;
class Player;
class Circuit;
class KeyValueStorage;

class Environment
{
public:
	// Environment will delete the map passed to the constructor
	Environment();
	virtual ~Environment();

	/*
		Step everything in environment.
		- Move players
		- Step mobs
		- Run timers of map
	*/
	virtual void step(f32 dtime, float uptime, int max_cycle_ms) = 0;

	virtual Map & getMap() = 0;

	virtual void addPlayer(Player *player);
	//void removePlayer(u16 peer_id);
	//void removePlayer(const std::string &name);
	Player * getPlayer(u16 peer_id);
	Player * getPlayer(const std::string &name);
	std::list<Player*> getPlayers();
	std::list<Player*> getPlayers(bool ignore_disconnected);
	
	u32 getDayNightRatio();

	// 0-23999
	virtual void setTimeOfDay(u32 time)
	{
		m_time_of_day = time;
		m_time_of_day_f = (float)time / 24000.0;
	}

	u32 getTimeOfDay()
	{ return m_time_of_day; }

	float getTimeOfDayF()
	{ return m_time_of_day_f; }

	void stepTimeOfDay(float dtime);

	void setTimeOfDaySpeed(float speed);
	
	float getTimeOfDaySpeed();

	void setDayNightRatioOverride(bool enable, u32 value)
	{
		m_enable_day_night_ratio_override = enable;
		m_day_night_ratio_override = value;
	}

	// counter used internally when triggering ABMs
	u32 m_added_objects;

protected:
	// peer_ids in here should be unique, except that there may be many 0s
	std::list<Player*> m_players;
	// Time of day in milli-hours (0-23999); determines day and night
	std::atomic_int m_time_of_day;
	// Time of day in 0...1
	float m_time_of_day_f;
	float m_time_of_day_speed;
	// Used to buffer dtime for adding to m_time_of_day
	float m_time_counter;
	// Overriding the day-night ratio is useful for custom sky visuals
	bool m_enable_day_night_ratio_override;
	u32 m_day_night_ratio_override;
	
private:
	locker m_lock;

};

#endif

