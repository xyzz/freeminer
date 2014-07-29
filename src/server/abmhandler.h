#pragma once

#include "server/abmwithstate.h"
#include "server/activeblockmodifier.h"
#include "server/serverenvironment.h"
#include "mapblock.h"
#include "map.h"

struct ActiveABM
{
	ActiveABM()
	{}
	ABMWithState *abmws;
	ActiveBlockModifier *abm; //delete me, abm in ws ^
	int chance;
};


class ABMHandler
{
private:
	ServerEnvironment *m_env;
	std::vector<std::list<ActiveABM> *> m_aabms;
	std::list<std::list<ActiveABM>*> m_aabms_list;
	bool m_aabms_empty;
public:
	ABMHandler(std::list<ABMWithState> &abms,
			float dtime_s, ServerEnvironment *env,
			bool use_timers, bool activate);
	~ABMHandler();
	u32 countObjects(MapBlock *block, ServerMap * map, u32 &wider);
	void apply(MapBlock *block, bool activate = false);

};
