#pragma once

#include <string>

class NestedTimeTaker;

class NestedProfiler {
public:
	NestedProfiler();
	int getDepth();
private:
	int m_depth;
	NestedTimeTaker *m_last_timetaker;
};

class NestedTimeTaker {
public:
	NestedTimeTaker(NestedProfiler *profiler, std::string &name);
	~NestedTimeTaker();
	void printHeader();
	void printFooter();
	void printAlign();
private:
	NestedProfiler *m_profiler;
	std::string m_name;
};
