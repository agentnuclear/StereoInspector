#include "Config.h"
#include <fstream>
#include <spdlog/spdlog.h>

nlohmann::json CheckToggles::toJson() const {
    return {
        {"correspondence", correspondence},
        {"ssim", ssim},
        {"pixelDiff", pixelDiff},
        {"histogram", histogram},
        {"edge", edge},
        {"orb", orb},
        {"opticalFlow", opticalFlow},
        {"blur", blur},
        {"brightness", brightness},
        {"contrast", contrast},
        {"bloom", bloom},
        {"shadow", shadow},
        {"stereoOffset", stereoOffset},
        {"ocr", ocr},
        {"disparityMetrics", disparityMetrics},
        {"matchQuality", matchQuality},
        {"asymmetry", asymmetry},
        {"lightingAsym", lightingAsym},
        {"bloomAsym", bloomAsym},
        {"shadowAsym", shadowAsym},
        {"postProcessAsym", postProcessAsym},
        {"textureAsym", textureAsym},
        {"blurAsym", blurAsym},
        {"chromaticAsym", chromaticAsym},
        {"contrastAsym", contrastAsym},
        {"geometryMissing", geometryMissing},
        {"detectIssues", detectIssues},
        {"issueClassification", issueClassification},
        {"issueMerging", issueMerging},
        {"temporal", temporal},
        {"sceneConfidence", sceneConfidence},
        {"healthScore", healthScore},
        {"baselineComparison", baselineComparison}
    };
}

CheckToggles CheckToggles::fromJson(const nlohmann::json& j) {
    CheckToggles c;
    c.correspondence = j.value("correspondence", c.correspondence);
    c.ssim = j.value("ssim", c.ssim);
    c.pixelDiff = j.value("pixelDiff", c.pixelDiff);
    c.histogram = j.value("histogram", c.histogram);
    c.edge = j.value("edge", c.edge);
    c.orb = j.value("orb", c.orb);
    c.opticalFlow = j.value("opticalFlow", c.opticalFlow);
    c.blur = j.value("blur", c.blur);
    c.brightness = j.value("brightness", c.brightness);
    c.contrast = j.value("contrast", c.contrast);
    c.bloom = j.value("bloom", c.bloom);
    c.shadow = j.value("shadow", c.shadow);
    c.stereoOffset = j.value("stereoOffset", c.stereoOffset);
    c.ocr = j.value("ocr", c.ocr);
    c.disparityMetrics = j.value("disparityMetrics", c.disparityMetrics);
    c.matchQuality = j.value("matchQuality", c.matchQuality);
    c.asymmetry = j.value("asymmetry", c.asymmetry);
    c.lightingAsym = j.value("lightingAsym", c.lightingAsym);
    c.bloomAsym = j.value("bloomAsym", c.bloomAsym);
    c.shadowAsym = j.value("shadowAsym", c.shadowAsym);
    c.postProcessAsym = j.value("postProcessAsym", c.postProcessAsym);
    c.textureAsym = j.value("textureAsym", c.textureAsym);
    c.blurAsym = j.value("blurAsym", c.blurAsym);
    c.chromaticAsym = j.value("chromaticAsym", c.chromaticAsym);
    c.contrastAsym = j.value("contrastAsym", c.contrastAsym);
    c.geometryMissing = j.value("geometryMissing", c.geometryMissing);
    c.detectIssues = j.value("detectIssues", c.detectIssues);
    c.issueClassification = j.value("issueClassification", c.issueClassification);
    c.issueMerging = j.value("issueMerging", c.issueMerging);
    c.temporal = j.value("temporal", c.temporal);
    c.sceneConfidence = j.value("sceneConfidence", c.sceneConfidence);
    c.healthScore = j.value("healthScore", c.healthScore);
    c.baselineComparison = j.value("baselineComparison", c.baselineComparison);
    return c;
}

AppConfig AppConfig::defaults() {
    return AppConfig{};
}

nlohmann::json AppConfig::toJson() const {
    return {
        {"checks", checks.toJson()},
        {"stereoRegion", {
            {"x", stereoRegion.x},
            {"y", stereoRegion.y},
            {"width", stereoRegion.width},
            {"height", stereoRegion.height},
            {"autoSplit", stereoRegion.autoSplit}
        }},
        {"thresholds", {
            {"ssimWarning", thresholds.ssimWarning},
            {"ssimFail", thresholds.ssimFail},
            {"pixelDiffWarning", thresholds.pixelDiffWarning},
            {"pixelDiffFail", thresholds.pixelDiffFail},
            {"histogramWarning", thresholds.histogramWarning},
            {"histogramFail", thresholds.histogramFail},
            {"edgeWarning", thresholds.edgeWarning},
            {"edgeFail", thresholds.edgeFail},
            {"blurDeltaWarning", thresholds.blurDeltaWarning},
            {"blurDeltaFail", thresholds.blurDeltaFail},
            {"brightnessDeltaWarning", thresholds.brightnessDeltaWarning},
            {"brightnessDeltaFail", thresholds.brightnessDeltaFail},
            {"contrastDeltaWarning", thresholds.contrastDeltaWarning},
            {"contrastDeltaFail", thresholds.contrastDeltaFail},
            {"bloomWarning", thresholds.bloomWarning},
            {"bloomFail", thresholds.bloomFail},
            {"shadowWarning", thresholds.shadowWarning},
            {"shadowFail", thresholds.shadowFail},
            {"stereoOffsetWarning", thresholds.stereoOffsetWarning},
            {"stereoOffsetFail", thresholds.stereoOffsetFail},
            {"minFeatureMatches", thresholds.minFeatureMatches},
            {"opticalFlowWarning", thresholds.opticalFlowWarning},
            {"opticalFlowFail", thresholds.opticalFlowFail},
            {"ocrMismatchWarning", thresholds.ocrMismatchWarning},
            {"ocrMismatchFail", thresholds.ocrMismatchFail}
        }},
        {"logging", {
            {"csvPath", logging.csvPath},
            {"jsonPath", logging.jsonPath},
            {"screenshotDir", logging.screenshotDir},
            {"reportPath", logging.reportPath},
            {"autoScreenshotOnFail", logging.autoScreenshotOnFail},
            {"autoScreenshotOnWarning", logging.autoScreenshotOnWarning},
            {"logTimestamps", logging.logTimestamps},
            {"maxScreenshots", logging.maxScreenshots}
        }},
        {"targetFps", targetFps},
        {"captureAdapter", captureAdapter},
        {"captureOutput", captureOutput},
        {"enableOcr", enableOcr},
        {"startMinimized", startMinimized},
        {"language", language},
        {"sceneConfidenceThreshold", sceneConfidenceThreshold}
    };
}

AppConfig AppConfig::fromJson(const nlohmann::json& j) {
    AppConfig cfg;
    auto& sr = cfg.stereoRegion;
    if (j.contains("stereoRegion")) {
        const auto& r = j["stereoRegion"];
        sr.x = r.value("x", 0);
        sr.y = r.value("y", 0);
        sr.width = r.value("width", 0);
        sr.height = r.value("height", 0);
        sr.autoSplit = r.value("autoSplit", true);
    }
    if (j.contains("thresholds")) {
        const auto& t = j["thresholds"];
        auto& th = cfg.thresholds;
        th.ssimWarning = t.value("ssimWarning", th.ssimWarning);
        th.ssimFail = t.value("ssimFail", th.ssimFail);
        th.pixelDiffWarning = t.value("pixelDiffWarning", th.pixelDiffWarning);
        th.pixelDiffFail = t.value("pixelDiffFail", th.pixelDiffFail);
        th.histogramWarning = t.value("histogramWarning", th.histogramWarning);
        th.histogramFail = t.value("histogramFail", th.histogramFail);
        th.edgeWarning = t.value("edgeWarning", th.edgeWarning);
        th.edgeFail = t.value("edgeFail", th.edgeFail);
        th.blurDeltaWarning = t.value("blurDeltaWarning", th.blurDeltaWarning);
        th.blurDeltaFail = t.value("blurDeltaFail", th.blurDeltaFail);
        th.brightnessDeltaWarning = t.value("brightnessDeltaWarning", th.brightnessDeltaWarning);
        th.brightnessDeltaFail = t.value("brightnessDeltaFail", th.brightnessDeltaFail);
        th.contrastDeltaWarning = t.value("contrastDeltaWarning", th.contrastDeltaWarning);
        th.contrastDeltaFail = t.value("contrastDeltaFail", th.contrastDeltaFail);
        th.bloomWarning = t.value("bloomWarning", th.bloomWarning);
        th.bloomFail = t.value("bloomFail", th.bloomFail);
        th.shadowWarning = t.value("shadowWarning", th.shadowWarning);
        th.shadowFail = t.value("shadowFail", th.shadowFail);
        th.stereoOffsetWarning = t.value("stereoOffsetWarning", th.stereoOffsetWarning);
        th.stereoOffsetFail = t.value("stereoOffsetFail", th.stereoOffsetFail);
        th.minFeatureMatches = t.value("minFeatureMatches", th.minFeatureMatches);
        th.opticalFlowWarning = t.value("opticalFlowWarning", th.opticalFlowWarning);
        th.opticalFlowFail = t.value("opticalFlowFail", th.opticalFlowFail);
        th.ocrMismatchWarning = t.value("ocrMismatchWarning", th.ocrMismatchWarning);
        th.ocrMismatchFail = t.value("ocrMismatchFail", th.ocrMismatchFail);
    }
    if (j.contains("logging")) {
        const auto& l = j["logging"];
        auto& lg = cfg.logging;
        lg.csvPath = l.value("csvPath", lg.csvPath);
        lg.jsonPath = l.value("jsonPath", lg.jsonPath);
        lg.screenshotDir = l.value("screenshotDir", lg.screenshotDir);
        lg.reportPath = l.value("reportPath", lg.reportPath);
        lg.autoScreenshotOnFail = l.value("autoScreenshotOnFail", lg.autoScreenshotOnFail);
        lg.autoScreenshotOnWarning = l.value("autoScreenshotOnWarning", lg.autoScreenshotOnWarning);
        lg.logTimestamps = l.value("logTimestamps", lg.logTimestamps);
        lg.maxScreenshots = l.value("maxScreenshots", lg.maxScreenshots);
    }
    if (j.contains("checks")) {
        cfg.checks = CheckToggles::fromJson(j["checks"]);
    }
    cfg.targetFps = j.value("targetFps", cfg.targetFps);
    cfg.captureAdapter = j.value("captureAdapter", cfg.captureAdapter);
    cfg.captureOutput = j.value("captureOutput", cfg.captureOutput);
    cfg.enableOcr = j.value("enableOcr", cfg.enableOcr);
    cfg.startMinimized = j.value("startMinimized", cfg.startMinimized);
    cfg.language = j.value("language", cfg.language);
    cfg.sceneConfidenceThreshold = j.value("sceneConfidenceThreshold", cfg.sceneConfidenceThreshold);
    return cfg;
}

AppConfig AppConfig::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        spdlog::warn("Config file not found at {}, using defaults", path);
        return defaults();
    }
    try {
        nlohmann::json j;
        file >> j;
        return fromJson(j);
    } catch (const std::exception& e) {
        spdlog::error("Failed to parse config: {}", e.what());
        return defaults();
    }
}

void AppConfig::saveToFile(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        spdlog::error("Failed to write config to {}", path);
        return;
    }
    file << toJson().dump(4);
}
