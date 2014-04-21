#include "nested_profiler.h"

#include <iostream>

#include "porting.h"

NestedProfiler::NestedProfiler():
	m_depth(0),
	m_last_timetaker(NULL)
{
}

int NestedProfiler::getDepth() {
	return m_depth;
}

void NestedProfiler::newTimeTaker(NestedTimeTaker *timetaker) {
	if (m_last_timetaker && !m_last_timetaker->isHeaderPrinted()) {
		m_last_timetaker->printHeader();
	}
	m_last_timetaker = timetaker;
	++m_depth;
}

void NestedProfiler::dropTimeTaker() {
	--m_depth;
	m_last_timetaker = m_last_timetaker->getParent();
}

NestedTimeTaker* NestedProfiler::getLastTimeTaker() {
	return m_last_timetaker;
}

NestedTimeTaker::NestedTimeTaker(NestedProfiler *profiler, std::string name):
	m_profiler(profiler),
	m_parent(m_profiler->getLastTimeTaker()),
	m_name(std::move(name)),
	m_header_printed(false),
	m_time_start(porting::getTimeMs())
{
	m_profiler->newTimeTaker(this);
}

void NestedTimeTaker::printAlign() {
	for (int i = 0; i < m_profiler->getDepth() - 1; ++i)
		std::cout << "| ";
}

void NestedTimeTaker::printHeader() {
	printAlign();
	std::cout << "/ " << m_name << std::endl;
	m_header_printed = true;
}

NestedTimeTaker* NestedTimeTaker::getParent() {
	return m_parent;
}

bool NestedTimeTaker::isHeaderPrinted() {
	return m_header_printed;
}

NestedTimeTaker::~NestedTimeTaker() {
	uint32_t passed = getTimeMs() - m_time_start;
	printAlign();
	if (m_header_printed) {
		std::cout << "\\ [" << passed << "] " << m_name << std::endl;
	} else {
		std::cout << "[" << passed << "] + " << m_name << std::endl;
	}
	m_profiler->dropTimeTaker();
}
