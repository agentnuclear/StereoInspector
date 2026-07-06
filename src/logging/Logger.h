#pragma once
#include "core/Types.h"
#include "config/Config.h"
#include <string>
#include <fstream>
#include <mutex>
#include <vector>
#include <atomic>

class StereoLogger {
public:
    explicit StereoLogger(const LogConfig& config);
    ~StereoLogger();

    void start();
    void stop();
    bool isRunning() const;
    void logFrame(const AnalysisResult& result, const std::string& screenshotPath = "");
    void captureScreenshot(const cv::Mat& frame, const AnalysisResult& result);

    std::vector<AnalysisResult> getSessionResults() const;

private:
    void writeCsvHeader();
    void writeCsvRow(const AnalysisResult& result, const std::string& screenshotPath);
    void writeJsonEntry(const AnalysisResult& result, const std::string& screenshotPath);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    LogConfig m_config;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_jsonFirstWrite{true};
    mutable std::mutex m_mutex;
    std::ofstream m_csvStream;
    std::vector<AnalysisResult> m_results;
    int m_screenshotCount = 0;
};
