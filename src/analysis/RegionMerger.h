#pragma once
#include "core/Types.h"
#include <vector>
#include <opencv2/core.hpp>

class RegionMerger {
public:
    RegionMerger() = default;
    ~RegionMerger() = default;

    struct Config {
        float mergeDistanceRatio = 0.3f;
        int minArea = 50;
        int maxArea = 0;
        float lensBorderMargin = 0.05f;
        float vignetteRadius = 0.35f;
        int smallRegionThreshold = 100;
    };

    void setConfig(const Config& cfg) { m_config = cfg; }
    const Config& config() const { return m_config; }

    std::vector<DetectedIssue> merge(std::vector<DetectedIssue> issues);
    std::vector<DetectedIssue> filterInvalid(const std::vector<DetectedIssue>& issues,
                                              int frameW, int frameH, const cv::Mat& disparityValid);
    std::vector<DetectedIssue> mergeOverlapping(const std::vector<DetectedIssue>& issues);

private:
    Config m_config;

    bool isOverlapping(const IssueRect& a, const IssueRect& b, float threshold) const;
    IssueRect mergeRects(const IssueRect& a, const IssueRect& b) const;
    bool isLensBorder(const IssueRect& r, int frameW, int frameH) const;
    bool isVignette(const IssueRect& r, int frameW, int frameH) const;
    bool isLowInformation(const IssueRect& r, const cv::Mat& gray) const;
};
