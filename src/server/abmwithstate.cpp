#include "server/abmwithstate.h"

#include "gamedef.h"
#include "nodedef.h"
#include "server/serverenvironment.h"
#include "server/activeblockmodifier.h"

ABMWithState::ABMWithState(ActiveBlockModifier *abm_, ServerEnvironment *senv):
	abm(abm_),
	timer(0),
	required_neighbors(CONTENT_ID_CAPACITY),
	required_neighbors_activate(CONTENT_ID_CAPACITY)
{
	auto ndef = senv->getGameDef()->ndef();
	// Initialize timer to random value to spread processing
	float itv = abm->getTriggerInterval();
	itv = MYMAX(0.001, itv); // No less than 1ms
	int minval = MYMAX(-0.51*itv, -60); // Clamp to
	int maxval = MYMIN(0.51*itv, 60);   // +-60 seconds
	timer = myrand_range(minval, maxval);

	for (auto & i : abm->getRequiredNeighbors(0))
		ndef->getIds(i, required_neighbors);

	for (auto & i : abm->getRequiredNeighbors(1))
		ndef->getIds(i, required_neighbors_activate);

	for (auto & i : abm->getTriggerContents())
		ndef->getIds(i, trigger_ids);
}