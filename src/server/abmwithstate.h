#pragma once

#include <set>

#include "fmbitset.h"
#include "mapnode.h"

class ServerEnvironment;
class ActiveBlockModifier;

struct ABMWithState
{
	ActiveBlockModifier *abm;
	float timer;
	std::set<content_t> trigger_ids;
	FMBitset required_neighbors, required_neighbors_activate;

	ABMWithState(ActiveBlockModifier *abm_, ServerEnvironment *senv);
};