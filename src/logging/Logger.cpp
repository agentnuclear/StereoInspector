#include "Logger.h"
#include <opencv2/imgcodecs.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

struct StereoLogger::Impl {};

StereoLogger::StereoLogger(const LogConfig& config)
    : m_config(config), m_impl(std::make_unique<Impl>()) {}

StereoLogger::~StereoLogger() {
    stop();
}

void StereoLogger::start() {
    if (m_running.exchange(true)) return;

    try {
        if (!m_config.csvPath.empty()) {
            m_csvStream.open(m_config.csvPath);
            if (m_csvStream.is_open()) {
                writeCsvHeader();
                spdlog::info("CSV logging started: {}", m_config.csvPath);
            }
        }

        if (!m_config.screenshotDir.empty()) {
            fs::create_directories(m_config.screenshotDir);
        }

        spdlog::info("Stereo logging started");
    } catch (const std::exception& e) {
        spdlog::error("Failed to start logging: {}", e.what());
        m_running = false;
    }
}

void StereoLogger::stop() {
    m_running = false;

    if (m_csvStream.is_open()) {
        m_csvStream.close();
    }

    m_jsonFirstWrite = true;

    spdlog::info("Stereo logging stopped. {} frames logged.", m_results.size());
}

bool StereoLogger::isRunning() const {
    return m_running.load();
}

void StereoLogger::writeCsvHeader() {
    m_csvStream << "Timestamp,FrameNumber,SSIM,PixelDiff%,HistogramCorr,EdgeSim,"
                << "FeatureMatches,OpticalFlow,BlurDelta,BrightnessDelta,ContrastDelta,"
                << "BloomDiff,ShadowDiff,StereoOffset,"
                << "OcrMismatches,IntegrityScore,StereoStatus,BaselineActive,"
                << "BaselineFrame,Deviations,ScreenshotPath\n";
}

void StereoLogger::writeCsvRow(const AnalysisResult& result, const std::string& screenshotPath) {
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &tt);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);

    std::string devStr;
    for (size_t i = 0; i < std::min((size_t)3, result.deviations.size()); i++) {
        if (i > 0) devStr += "; ";
        devStr += result.deviations[i].metricName + ":" +
                  std::to_string(result.deviations[i].absDelta);
    }

    m_csvStream << buf << ","
                << result.frameNumber << ","
                << std::fixed << std::setprecision(6)
                << result.ssim << ","
                << result.pixelDiffPercent << ","
                << result.histogramCorrelation << ","
                << result.edgeSimilarity << ","
                << result.featureMatchCount << ","
                << result.opticalFlowMagnitude << ","
                << result.blurDelta << ","
                << result.brightnessDelta << ","
                << result.contrastDelta << ","
                << result.bloomDifference << ","
                << result.shadowDifference << ","
                << result.stereoOffset << ","
                << result.ocrTextMismatches << ","
                << result.stereoIntegrityScore << ","
                << StereoStatusName(result.stereoStatus) << ","
                << (result.stereoModel.active ? "1" : "0") << ","
                << (result.stereoModel.active ? std::to_string(result.stereoModel.captureFrameNumber) : "") << ","
                << "\"" << devStr << "\","
                << "\"" << screenshotPath << "\"\n";
}

void StereoLogger::writeJsonEntry(const AnalysisResult& result, const std::string& screenshotPath) {
    nlohmann::json entry;
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &tt);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);

    entry = {
        {"timestamp", buf},
        {"frameNumber", result.frameNumber},
        {"ssim", result.ssim},
        {"pixelDiffPercent", result.pixelDiffPercent},
        {"histogramCorrelation", result.histogramCorrelation},
        {"edgeSimilarity", result.edgeSimilarity},
        {"featureMatchCount", result.featureMatchCount},
        {"opticalFlowMagnitude", result.opticalFlowMagnitude},
        {"blurDelta", result.blurDelta},
        {"brightnessDelta", result.brightnessDelta},
        {"contrastDelta", result.contrastDelta},
        {"bloomDifference", result.bloomDifference},
        {"shadowDifference", result.shadowDifference},
        {"stereoOffset", result.stereoOffset},
        {"ocrTextMismatches", result.ocrTextMismatches},
        {"integrityScore", result.stereoIntegrityScore},
        {"stereoStatus", StereoStatusName(result.stereoStatus)},
        {"baselineActive", result.stereoModel.active},
        {"baselineFrame", result.stereoModel.active ?
            (uint64_t)result.stereoModel.captureFrameNumber : 0},
        {"issues", result.issues},
        {"screenshotPath", screenshotPath}
    };

    nlohmann::json devArray = nlohmann::json::array();
    for (const auto& d : result.deviations) {
        devArray.push_back({
            {"metric", d.metricName},
            {"baseline", d.baselineValue},
            {"current", d.currentValue},
            {"delta", d.delta},
            {"severity", d.severity},
            {"warning", d.warning},
            {"fail", d.fail}
        });
    }
    entry["deviations"] = devArray;

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_jsonFirstWrite.load()) {
        std::ofstream jf(m_config.jsonPath);
        jf << "[\n";
        jf << entry.dump(4);
        m_jsonFirstWrite = false;
    } else {
        std::string content;
        {
            std::ifstream jf(m_config.jsonPath);
            if (jf) {
                std::stringstream ss;
                ss << jf.rdbuf();
                content = ss.str();
            }
        }
        if (!content.empty() && content.back() == ']') {
            content.pop_back();
            content.back() = ',';
        }
        content += "\n" + entry.dump(4) + "\n]";
        std::ofstream jf(m_config.jsonPath);
        jf << content;
    }
}

void StereoLogger::logFrame(const AnalysisResult& result, const std::string& screenshotPath) {
    if (!m_running) return;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_results.push_back(result);
    }

    if (m_csvStream.is_open()) {
        writeCsvRow(result, screenshotPath);
    }

    writeJsonEntry(result, screenshotPath);
}

void StereoLogger::captureScreenshot(const cv::Mat& frame, const AnalysisResult& result) {
    if (!m_running) return;

    bool shouldCapture = false;
    StereoStatus captureReason = StereoStatus::SAFE;
    if (result.stereoModel.active) {
        if (result.stereoStatus == StereoStatus::DESYNC && m_config.autoScreenshotOnFail) {
            shouldCapture = true;
            captureReason = StereoStatus::DESYNC;
        }
        if (result.stereoStatus == StereoStatus::WARNING && m_config.autoScreenshotOnWarning) {
            shouldCapture = true;
            captureReason = StereoStatus::WARNING;
        }
    } else {
        if (result.status == FrameStatus::FAIL && m_config.autoScreenshotOnFail) {
            shouldCapture = true;
        }
        if (result.status == FrameStatus::WARNING && m_config.autoScreenshotOnWarning) {
            shouldCapture = true;
        }
    }

    if (!shouldCapture) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_screenshotCount >= m_config.maxScreenshots) return;

    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &tt);
    char buf[64];
    strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);

    std::string filename = m_config.screenshotDir + "/" +
                           std::string(buf) + "_frame" +
                           std::to_string(result.frameNumber) + ".png";
    cv::imwrite(filename, frame);
    m_screenshotCount++;

    spdlog::info("Screenshot saved: {} (status: {}, integrity: {:.1f})", filename,
                 StereoStatusName(result.stereoStatus), result.stereoIntegrityScore);
}

std::vector<AnalysisResult> StereoLogger::getSessionResults() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_results;
}
