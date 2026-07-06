#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

struct StereoRegion {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool autoSplit = true;
};

struct AnalysisThresholds {
    double ssimWarning = 0.85;
    double ssimFail = 0.70;
    double pixelDiffWarning = 5.0;
    double pixelDiffFail = 15.0;
    double histogramWarning = 0.80;
    double histogramFail = 0.60;
    double edgeWarning = 0.80;
    double edgeFail = 0.60;
    double blurDeltaWarning = 3.0;
    double blurDeltaFail = 8.0;
    double brightnessDeltaWarning = 0.10;
    double brightnessDeltaFail = 0.25;
    double contrastDeltaWarning = 0.15;
    double contrastDeltaFail = 0.30;
    double bloomWarning = 0.10;
    double bloomFail = 0.25;
    double shadowWarning = 0.10;
    double shadowFail = 0.25;
    double stereoOffsetWarning = 10.0;
    double stereoOffsetFail = 30.0;
    int minFeatureMatches = 20;
    double opticalFlowWarning = 5.0;
    double opticalFlowFail = 15.0;
    int ocrMismatchWarning = 2;
    int ocrMismatchFail = 5;
};

struct LogConfig {
    std::string csvPath = "stereo_inspector_log.csv";
    std::string jsonPath = "stereo_inspector_log.json";
    std::string screenshotDir = "screenshots";
    std::string reportPath = "stereo_inspector_report.html";
    bool autoScreenshotOnFail = true;
    bool autoScreenshotOnWarning = false;
    bool logTimestamps = true;
    int maxScreenshots = 1000;
};

struct AppConfig {
    StereoRegion stereoRegion;
    AnalysisThresholds thresholds;
    LogConfig logging;
    int targetFps = 90;
    int captureAdapter = 0;
    int captureOutput = 0;
    bool enableOcr = false;
    bool startMinimized = false;
    std::string language = "en";
    double sceneConfidenceThreshold = 0.35;

    nlohmann::json toJson() const;
    static AppConfig fromJson(const nlohmann::json& j);
    static AppConfig loadFromFile(const std::string& path);
    void saveToFile(const std::string& path) const;

private:
    static AppConfig defaults();
};
