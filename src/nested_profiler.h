#pragma once

#include <cstdint>
#include <string>

class NestedTimeTaker;

class NestedProfiler {
public:
	NestedProfiler();
	int getDepth();
	NestedTimeTaker* getLastTimeTaker();
	void newTimeTaker(NestedTimeTaker *timetaker);
	void dropTimeTaker();
private:
	int m_depth;
	NestedTimeTaker *m_last_timetaker;
};

class NestedTimeTaker {
public:
	NestedTimeTaker(NestedProfiler *profiler, std::string name);
	~NestedTimeTaker();
	void printHeader();
	void printFooter();
	void printAlign();
	bool isHeaderPrinted();
	NestedTimeTaker *getParent();
private:
	NestedProfiler *m_profiler;
	NestedTimeTaker *m_parent;
	std::string m_name;
	bool m_header_printed;
	uint32_t m_time_start;
};
