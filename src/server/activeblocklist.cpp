#include "server/activeblocklist.h"

#include <set>
#include <list>

#include "irr_v3d.h"

static void fillRadiusBlock(v3s16 p0, s16 r, std::set<v3s16> &list)
{
	v3s16 p;
	for(p.X=p0.X-r; p.X<=p0.X+r; p.X++)
	for(p.Y=p0.Y-r; p.Y<=p0.Y+r; p.Y++)
	for(p.Z=p0.Z-r; p.Z<=p0.Z+r; p.Z++)
	{
		// Set in list
		list.insert(p);
	}
}

void ActiveBlockList::update(std::list<v3s16> &active_positions,
	s16 radius,
	std::set<v3s16> &blocks_removed,
	std::set<v3s16> &blocks_added)
{
	/*
	Create the new list
	*/
	std::set<v3s16> newlist = m_forceloaded_list;
	for (std::list<v3s16>::iterator i = active_positions.begin();
		i != active_positions.end(); ++i)
	{
		fillRadiusBlock(*i, radius, newlist);
	}

	/*
	Find out which blocks on the old list are not on the new list
	*/
	// Go through old list
	for (std::set<v3s16>::iterator i = m_list.begin();
		i != m_list.end(); ++i)
	{
		v3s16 p = *i;
		// If not on new list, it's been removed
		if (newlist.find(p) == newlist.end())
			blocks_removed.insert(p);
	}

	/*
	Find out which blocks on the new list are not on the old list
	*/
	// Go through new list
	for (std::set<v3s16>::iterator i = newlist.begin();
		i != newlist.end(); ++i)
	{
		v3s16 p = *i;
		// If not on old list, it's been added
		if (m_list.find(p) == m_list.end())
			blocks_added.insert(p);
	}

	/*
	Update m_list
	*/
	m_list.clear();
	for (std::set<v3s16>::iterator i = newlist.begin();
		i != newlist.end(); ++i)
	{
		v3s16 p = *i;
		m_list.insert(p);
	}
}