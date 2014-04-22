#include "nested_profiler.h"

#include <iostream>

#include "porting.h"

NestedProfiler::NestedProfiler():
	m_tree_pointer(-1)
{
}

void NestedProfiler::newTimeTaker(const std::string &name) {
	TimeTakerLeaf leaf;
	leaf.parent = m_tree_pointer;
	leaf.name = name;
	int new_pointer = m_tree.size();
	m_tree.push_back(leaf);
	if (m_tree_pointer != -1)
		m_tree[m_tree_pointer].children.push_back(new_pointer);
	m_tree_pointer = new_pointer;
}

void NestedProfiler::dropTimeTaker(uint32_t time_passed) {
	m_tree[m_tree_pointer].time_passed = time_passed;
	m_tree_pointer = m_tree[m_tree_pointer].parent;
	if (m_tree_pointer == -1) {
		outputProfile(0, 0);
		m_tree.clear();
	}
}

void NestedProfiler::printAlignment(int depth) {
	for (int i = 0; i < depth; ++i)
		std::cout << "|";
}

void NestedProfiler::outputProfile(int current, int depth) {
	const TimeTakerLeaf &leaf = m_tree[current];
	if (leaf.time_passed < PROFILER_TIME_THRESHOLD)
		return;
	if (!leaf.children.empty()) {
		printAlignment(depth);
		std::cout << "/ " << leaf.name << std::endl;
		for (auto x : leaf.children)
			outputProfile(x, depth + 1);
		printAlignment(depth);
		std::cout << "\\ [" << leaf.time_passed << "] " << leaf.name << std::endl;
	} else {
		printAlignment(depth - 1);
		std::cout << "+ [" << leaf.time_passed << "] " << leaf.name << std::endl;
	}
}

NestedTimeTaker::NestedTimeTaker(NestedProfiler *profiler, std::string name):
	m_profiler(profiler),
	m_time_start(porting::getTimeMs())
{
	m_profiler->newTimeTaker(name);
}

NestedTimeTaker::~NestedTimeTaker() {
	m_profiler->dropTimeTaker(porting::getTimeMs() - m_time_start);
}
