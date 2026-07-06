#pragma once
#include "IStereoAnalyzer.h"
#include "core/Types.h"
#include "core/Frame.h"
#include "config/Config.h"
#include "analysis/modules/StereoCorrespondence.h"
#include "analysis/IssueClassifier.h"
#include "analysis/RegionMerger.h"
#include <memory>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <functional>

class TemporalAnalyzer;

class AnalyzerPipeline {
public:
    explicit AnalyzerPipeline(const AppConfig& config);
    ~AnalyzerPipeline();

    void registerAnalyzer(std::unique_ptr<IStereoAnalyzer> analyzer);
    void start(LatestFrameBuffer& frameBuffer);
    void stop();
    void waitForFrame();

    AnalysisResult getLatestResult() const;
    FrameTime getFrameTime() const;
    bool isRunning() const;
    void setConfig(const AppConfig& config);
    AppConfig getConfig() const;
    void setCheckToggles(const CheckToggles& toggles);
    std::vector<IStereoAnalyzer*> analyzers() const;
    MetricHistory getHistory() const;

    void setBaseline(const cv::Mat& leftEye, const cv::Mat& rightEye);
    void clearBaseline();
    bool hasBaseline() const;
    StereoModelBaseline getBaselineInfo() const;

    using BaselineCapturedCallback = std::function<void(uint64_t frameNumber, float confidence, const std::string& timestamp)>;
    void setOnBaselineCaptured(BaselineCapturedCallback cb);

    using BaselineRefusedCallback = std::function<void(const std::string& reason)>;
    void setOnBaselineRefused(BaselineRefusedCallback cb);

    IssueClassifier& classifier() { return m_classifier; }
    RegionMerger& merger() { return m_merger; }

private:
    void processLoop(LatestFrameBuffer& frameBuffer);
    AnalysisResult analyzeFrame(const cv::Mat& left, const cv::Mat& right, int64_t frameNum);
    void computeDisparityMetrics(const CorrespondenceResult& corr, AnalysisResult& result);
    void computeMatchQuality(const CorrespondenceResult& corr, AnalysisResult& result);
    void computeAsymmetry(const cv::Mat& left, const cv::Mat& warpedRight,
                          const cv::Mat& occlusionMask, AnalysisResult& result);
    SceneConfidence computeSceneConfidence(const cv::Mat& left, const cv::Mat& right);
    void captureBaselineFromResult(const AnalysisResult& result);
    void compareWithBaseline(AnalysisResult& result);
    void computeHealthScore(AnalysisResult& result);
    void detectIssues(AnalysisResult& result, const cv::Mat& left,
                      const cv::Mat& warpedRight, const cv::Mat& occlusionMask);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    StereoCorrespondence m_correspondence;
    IssueClassifier m_classifier;
    RegionMerger m_merger;
};
