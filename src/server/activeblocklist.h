#pragma once

#include <list>
#include <set>

#include "irr_v3d.h"

/*
List of active blocks, used by ServerEnvironment
*/

class ActiveBlockList
{
public:
	void update(std::list<v3s16> &active_positions,
		s16 radius,
		std::set<v3s16> &blocks_removed,
		std::set<v3s16> &blocks_added);

	bool contains(v3s16 p){
		return (m_list.find(p) != m_list.end());
	}

	void clear(){
		m_list.clear();
	}

	std::set<v3s16> m_list;
	std::set<v3s16> m_forceloaded_list;

private:
};
