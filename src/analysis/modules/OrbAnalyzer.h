#pragma once
#include "analysis/IStereoAnalyzer.h"
#include <opencv2/features2d.hpp>

class OrbAnalyzer : public BaseAnalyzer {
public:
    OrbAnalyzer();
    void analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) override;

private:
    cv::Ptr<cv::DescriptorMatcher> m_matcher;
};
