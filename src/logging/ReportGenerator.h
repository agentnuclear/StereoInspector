#pragma once
#include "core/Types.h"
#include "config/Config.h"
#include <string>
#include <vector>

class ReportGenerator {
public:
    explicit ReportGenerator(const LogConfig& config);
    ~ReportGenerator();

    void generate(const std::vector<AnalysisResult>& results,
                  const std::vector<std::string>& screenshotPaths,
                  const std::string& outputPath = "");

private:
    std::string buildHtml(const std::vector<AnalysisResult>& results,
                          const std::vector<std::string>& screenshotPaths) const;
    std::string buildHealthChart(const std::vector<AnalysisResult>& results) const;
    std::string statusColor(const AnalysisResult& result) const;

    LogConfig m_config;
};
