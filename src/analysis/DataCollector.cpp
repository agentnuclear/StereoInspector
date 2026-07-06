#include "DataCollector.h"
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

DataCollector::DataCollector(const std::string& outputDir, int maxFrames)
    : m_outputDir(outputDir), m_maxFrames(maxFrames) {
    fs::create_directories(m_outputDir);
    std::string jsonlPath = (fs::path(m_outputDir) / "dataset.jsonl").string();
    m_jsonl.open(jsonlPath, std::ios::app);
    if (!m_jsonl.is_open()) {
        spdlog::error("DataCollector: failed to open {}", jsonlPath);
    }
    spdlog::info("DataCollector: writing to {}", jsonlPath);
}

DataCollector::~DataCollector() {
    if (m_jsonl.is_open()) m_jsonl.close();
    spdlog::info("DataCollector: wrote {} frames to {}", m_frameCount, m_outputDir);
}

std::string DataCollector::padFrame(int n) const {
    std::ostringstream ss;
    ss << std::setw(4) << std::setfill('0') << n;
    return ss.str();
}

bool DataCollector::onFrame(const AnalysisResult& result,
                             const cv::Mat& leftEye, const cv::Mat& rightEye) {
    if (m_maxFrames > 0 && m_frameCount >= m_maxFrames) return false;

    m_frameCount++;
    std::string stem = "frame_" + padFrame(m_frameCount);
    std::string leftPath = stem + "_left.png";
    std::string rightPath = stem + "_right.png";

    cv::imwrite((fs::path(m_outputDir) / leftPath).string(), leftEye);
    cv::imwrite((fs::path(m_outputDir) / rightPath).string(), rightEye);

    writeJsonl(result, leftPath, rightPath);
    return true;
}

static json issueToJson(const DetectedIssue& issue) {
    json j;
    j["type"] = IssueTypeName(issue.type);
    j["confidence"] = issue.confidence;
    j["boundingBox"] = {{"x", issue.boundingBox.x},
                        {"y", issue.boundingBox.y},
                        {"width", issue.boundingBox.width},
                        {"height", issue.boundingBox.height}};
    j["areaPixels"] = issue.areaPixels;
    j["severity"] = issue.severity;
    j["centerX"] = issue.centerX;
    j["centerY"] = issue.centerY;
    j["reasoningText"] = issue.reasoningText;

    json alts = json::array();
    for (const auto& a : issue.alternatives) {
        alts.push_back({{"type", IssueTypeName(a.type)}, {"confidence", a.confidence}});
    }
    j["alternatives"] = alts;

    const auto& e = issue.evidence;
    j["evidence"] = {
        {"brightnessDiff", e.brightnessDiff},
        {"contrastDiff", e.contrastDiff},
        {"colorDiff", e.colorDiff},
        {"edgeSimilarity", e.edgeSimilarity},
        {"textureSimilarity", e.textureSimilarity},
        {"gradientConsistency", e.gradientConsistency},
        {"histogramSimilarity", e.histogramSimilarity},
        {"featureMatchDensity", e.featureMatchDensity},
        {"disparityConsistency", e.disparityConsistency},
        {"regionSize", e.regionSize},
        {"regionAspectRatio", e.regionAspectRatio},
        {"regionPosX", e.regionPosX},
        {"regionPosY", e.regionPosY},
        {"temporalStability", e.temporalStability},
        {"edgeRatio", e.edgeRatio},
        {"meanDiff", e.meanDiff},
        {"bloomRatio", e.bloomRatio},
        {"shadowRatio", e.shadowRatio},
        {"leftContentDensity", e.leftContentDensity},
        {"rightContentDensity", e.rightContentDensity},
        {"hasColor", e.hasColor},
        {"nearBorder", e.nearBorder},
        {"nearCenter", e.nearCenter}
    };
    return j;
}

void DataCollector::writeJsonl(const AnalysisResult& result,
                                const std::string& leftPath,
                                const std::string& rightPath) {
    json j;
    j["frame"] = m_frameCount;
    j["frameNumber"] = result.frameNumber;
    j["left"] = leftPath;
    j["right"] = rightPath;

    j["metrics"] = {
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
        {"stereoOffset", result.stereoOffset}
    };

    j["residual"] = {
        {"alignedSSIM", result.residual.alignedSSIM},
        {"alignedPixDiffPercent", result.residual.alignedPixDiffPercent},
        {"alignedBrightnessDelta", result.residual.alignedBrightnessDelta},
        {"alignedEdgeSimilarity", result.residual.alignedEdgeSimilarity},
        {"alignedHistogramCorrelation", result.residual.alignedHistogramCorrelation},
        {"occlusionRatio", result.residual.occlusionRatio}
    };

    j["disparity"] = {
        {"meanDisparity", result.disparity.meanDisparity},
        {"medianDisparity", result.disparity.medianDisparity},
        {"stdDisparity", result.disparity.stdDisparity},
        {"minDisparity", result.disparity.minDisparity},
        {"maxDisparity", result.disparity.maxDisparity},
        {"disparityRange", result.disparity.disparityRange},
        {"invalidRatio", result.disparity.invalidRatio},
        {"smoothness", result.disparity.smoothness},
        {"verticalAsymmetry", result.disparity.verticalAsymmetry}
    };

    j["matchQuality"] = {
        {"confidence", result.matchQuality.confidence},
        {"totalMatches", result.matchQuality.totalMatches},
        {"inlierCount", result.matchQuality.inlierCount},
        {"inlierRatio", result.matchQuality.inlierRatio}
    };

    j["asymmetry"] = {
        {"lightingAsymmetry", result.asymmetry.lightingAsymmetry},
        {"postProcessAsymmetry", result.asymmetry.postProcessAsymmetry},
        {"bloomAsymmetry", result.asymmetry.bloomAsymmetry},
        {"shadowAsymmetry", result.asymmetry.shadowAsymmetry},
        {"chromaticAsymmetry", result.asymmetry.chromaticAsymmetry},
        {"blurAsymmetry", result.asymmetry.blurAsymmetry},
        {"textureAsymmetry", result.asymmetry.textureAsymmetry},
        {"geometryMissing", result.asymmetry.geometryMissing},
        {"contrastAsymmetry", result.asymmetry.contrastAsymmetry}
    };

    j["sceneConfidence"] = {
        {"overall", result.sceneConfidence.overall},
        {"reliable", result.sceneConfidence.reliable},
        {"luminanceScore", result.sceneConfidence.luminanceScore},
        {"edgeDensityScore", result.sceneConfidence.edgeDensityScore},
        {"textureScore", result.sceneConfidence.textureScore},
        {"featureScore", result.sceneConfidence.featureScore},
        {"entropyScore", result.sceneConfidence.entropyScore}
    };

    j["integrity"] = {
        {"stereoIntegrityScore", result.stereoIntegrityScore},
        {"stereoHealthScore", result.stereoHealthScore},
        {"stereoStatus", StereoStatusName(result.stereoStatus)},
        {"frameStatus", result.status == FrameStatus::PASS ? "PASS" :
                        result.status == FrameStatus::WARNING ? "WARNING" : "FAIL"}
    };

    json issues = json::array();
    for (const auto& issue : result.detectedIssues) {
        issues.push_back(issueToJson(issue));
    }
    j["issues"] = issues;

    m_jsonl << j.dump() << "\n";
}
