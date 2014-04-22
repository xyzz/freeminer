#pragma once

#include <cstdint>
#include <string>
#include <vector>

const uint32_t PROFILER_TIME_THRESHOLD = 50;

class NestedTimeTaker;

struct TimeTakerLeaf {
	int parent;
	std::string name;
	uint32_t time_passed;
	std::vector<int> children;
};

class NestedProfiler {
public:
	NestedProfiler();
	/* Adds new NestedTimeTaker to the tree. */ 
	void newTimeTaker(const std::string &name);
	/* Should be called by ~NestedTimeTaker, moves pointer up the tree. */
	void dropTimeTaker(uint32_t time_passed);
private:
	void outputProfile(int current, int depth);
	void printAlignment(int depth);
	int m_tree_pointer;
	std::vector<TimeTakerLeaf> m_tree;
};

class NestedTimeTaker {
public:
	NestedTimeTaker(NestedProfiler *profiler, std::string name);
	~NestedTimeTaker();
private:
	NestedProfiler *m_profiler;
	uint32_t m_time_start;
};
