/*
environment.cpp
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

#include "environment.h"
#include "filesys.h"
#include "porting.h"
#include "collision.h"
#include "content_mapnode.h"
#include "mapblock.h"
#include "serverobject.h"
#include "content_sao.h"
#include "settings.h"
#include "log.h"
#include "profiler.h"
#include "scripting_game.h"
#include "nodedef.h"
#include "nodemetadata.h"
#include "main.h" // For g_settings, g_profiler
#include "gamedef.h"
#ifndef SERVER
#include "clientmap.h"
#include "localplayer.h"
#include "event.h"
#endif
#include "daynightratio.h"
#include "map.h"
#include "emerge.h"
#include "util/serialize.h"
#include "fmbitset.h"
#include "circuit.h"
#include "key_value_storage.h"

#define PP(x) "("<<(x).X<<","<<(x).Y<<","<<(x).Z<<")"

Environment::Environment():
	m_time_of_day_f(9000./24000),
	m_time_of_day_speed(0),
	m_time_counter(0),
	m_enable_day_night_ratio_override(false),
	m_day_night_ratio_override(0.0f)
{
	m_time_of_day = 9000;
}

Environment::~Environment()
{
	// Deallocate players
	for(std::list<Player*>::iterator i = m_players.begin();
			i != m_players.end(); ++i)
	{
		delete (*i);
	}
}

void Environment::addPlayer(Player *player)
{
	DSTACK(__FUNCTION_NAME);
	/*
		Check that peer_ids are unique.
		Also check that names are unique.
		Exception: there can be multiple players with peer_id=0
	*/
	// If peer id is non-zero, it has to be unique.
	if(player->peer_id != 0)
		assert(getPlayer(player->peer_id) == NULL);
	// Name has to be unique.
	assert(getPlayer(player->getName()) == NULL);
	// Add.
	m_players.push_back(player);
}

Player * Environment::getPlayer(u16 peer_id)
{
	for(std::list<Player*>::iterator i = m_players.begin();
			i != m_players.end(); ++i)
	{
		Player *player = *i;
		if(player->peer_id == peer_id)
			return player;
	}
	return NULL;
}

Player * Environment::getPlayer(const std::string &name)
{
	for(auto &player : m_players) {
 		if(player->getName() == name)
			return player;
	}
	return NULL;
}

std::list<Player*> Environment::getPlayers()
{
	return m_players;
}

std::list<Player*> Environment::getPlayers(bool ignore_disconnected)
{
	std::list<Player*> newlist;
	for(std::list<Player*>::iterator
			i = m_players.begin();
			i != m_players.end(); ++i)
	{
		Player *player = *i;

		if(ignore_disconnected)
		{
			// Ignore disconnected players
			if(player->peer_id == 0)
				continue;
		}

		newlist.push_back(player);
	}
	return newlist;
}

u32 Environment::getDayNightRatio()
{
	if(m_enable_day_night_ratio_override)
		return m_day_night_ratio_override;
	bool smooth = g_settings->getBool("enable_shaders");
	return time_to_daynight_ratio(m_time_of_day_f*24000, smooth);
}

void Environment::setTimeOfDaySpeed(float speed)
{
	auto lock = m_lock.lock_unique();
	m_time_of_day_speed = speed;
}

float Environment::getTimeOfDaySpeed()
{
	auto lock = m_lock.lock_shared();
	float retval = m_time_of_day_speed;
	return retval;
}

void Environment::stepTimeOfDay(float dtime)
{
	float day_speed = getTimeOfDaySpeed();
	
	m_time_counter += dtime;
	f32 speed = day_speed * 24000./(24.*3600);
	u32 units = (u32)(m_time_counter*speed);
	bool sync_f = false;
	if(units > 0){
		// Sync at overflow
		if(m_time_of_day + units >= 24000)
			sync_f = true;
		m_time_of_day = (m_time_of_day + units) % 24000;
		if(sync_f)
			m_time_of_day_f = (float)m_time_of_day / 24000.0;
	}
	if (speed > 0) {
		m_time_counter -= (f32)units / speed;
	}
	if(!sync_f){
		m_time_of_day_f += day_speed/24/3600*dtime;
		if(m_time_of_day_f > 1.0)
			m_time_of_day_f -= 1.0;
		if(m_time_of_day_f < 0.0)
			m_time_of_day_f += 1.0;
	}
}
