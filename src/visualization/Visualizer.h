#pragma once
#include "core/Types.h"
#include <opencv2/core.hpp>

class Visualizer {
public:
    Visualizer();
    ~Visualizer();

    cv::Mat render(const cv::Mat& frame, const cv::Mat& leftEye, const cv::Mat& rightEye,
                   const AnalysisResult& result, VisualizationMode mode,
                   const MetricHistory* history = nullptr,
                   int selectedIssueIndex = -1) const;

private:
    cv::Mat renderNormal(const cv::Mat& frame, const AnalysisResult* result = nullptr,
                          const MetricHistory* history = nullptr) const;
    cv::Mat renderDifferenceHeatmap(const cv::Mat& left, const cv::Mat& right,
                                     const cv::Mat& alignedDiff = cv::Mat()) const;
    cv::Mat renderStereoDifferenceOverlay(const cv::Mat& left, const cv::Mat& right,
                                           const AnalysisResult& result) const;
    cv::Mat renderEdgeComparison(const cv::Mat& left, const cv::Mat& right) const;
    cv::Mat renderFeatureMatch(const cv::Mat& left, const cv::Mat& right) const;
    cv::Mat renderHistogramView(const cv::Mat& left, const cv::Mat& right) const;
    cv::Mat renderBlurMap(const cv::Mat& left, const cv::Mat& right) const;
    cv::Mat renderBlink(const cv::Mat& left, const cv::Mat& right, bool showLeft) const;
    cv::Mat renderDisparityHeatmap(const AnalysisResult& result) const;

    void overlayMiniGraph(cv::Mat& frame, const HistoryBuffer& data,
                          int x, int y, int w, int h,
                          double minVal, double maxVal,
                          const cv::Scalar& color,
                          const std::string& label) const;
    void overlaySelectedIssue(cv::Mat& image, const AnalysisResult& result,
                              int selectedIndex) const;
};
