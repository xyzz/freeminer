#pragma once

#include <set>
#include <string>

#include "irrlichttypes.h"
#include "irr_v3d.h"
#include "mapnode.h"

class ServerEnvironment;

/*
	Active block modifier interface.

	These are fed into ServerEnvironment at initialization time;
	ServerEnvironment handles deleting them.
*/

class ActiveBlockModifier
{
public:
	ActiveBlockModifier(){};
	virtual ~ActiveBlockModifier(){};
	
	// Set of contents to trigger on
	virtual std::set<std::string> getTriggerContents()=0;
	// Set of required neighbors (trigger doesn't happen if none are found)
	// Empty = do not check neighbors
	virtual std::set<std::string> getRequiredNeighbors(bool activate)
	{ return std::set<std::string>(); }
	// Maximum range to neighbors
	virtual u32 getNeighborsRange()
	{ return 1; };
	// Trigger interval in seconds
	virtual float getTriggerInterval() = 0;
	// Random chance of (1 / return value), 0 is disallowed
	virtual u32 getTriggerChance() = 0;
	// This is called usually at interval for 1/chance of the nodes
	//virtual void trigger(ServerEnvironment *env, v3s16 p, MapNode n){};
	//virtual void trigger(ServerEnvironment *env, v3s16 p, MapNode n, MapNode neighbor){};
	virtual void trigger(ServerEnvironment *env, v3s16 p, MapNode n,
			u32 active_object_count, u32 active_object_count_wider, MapNode neighbor, bool activate = false){};
};