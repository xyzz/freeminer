#include "nested_profiler.h"

#include <iostream>

NestedProfiler::NestedProfiler():
	m_last_timetaker(NULL)
{
}

int NestedProfiler::getDepth() {
	return m_depth;
}

NestedProfiler::newTimeTaker(NestedTimeTaker *timetaker) {
	if (m_last_timetaker) {
		m_last_timetaker->printHeader();
	}
	m_last_timetaker = timetaker;
	++m_depth;
}

NestedTimeTaker::NestedTimeTaker(NestedProfiler *profiler, std::string &name):
	m_profiler(profiler),
	m_name(std::move(name))
{
	m_profiler->newTimeTaker(this);
}

void NestedTimeTaker::printAlign() {
	for (int i = 0; i < m_profiler->getDepth(); ++i)
		std::cout << "| ";
}

void NestedTimeTaker::printHeader() {
	printAlign();
	std::cout << "/ " << m_name << std::endl;
}

NestedTimeTaker::~NestedTimeTaker() {
	printAlign();
	std::cout << "\\ " << m_name << std::endl;
}
