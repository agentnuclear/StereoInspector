#include "IStereoAnalyzer.h"

BaseAnalyzer::BaseAnalyzer(std::string name, bool enabled)
    : m_name(std::move(name)), m_enabled(enabled) {}

std::string BaseAnalyzer::name() const { return m_name; }
bool BaseAnalyzer::enabled() const { return m_enabled; }
void BaseAnalyzer::setEnabled(bool enabled) { m_enabled = enabled; }
