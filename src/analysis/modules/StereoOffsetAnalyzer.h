#pragma once
#include "analysis/IStereoAnalyzer.h"
#include <opencv2/features2d.hpp>

class StereoOffsetAnalyzer : public BaseAnalyzer {
public:
    StereoOffsetAnalyzer();
    void analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) override;

private:
    cv::Ptr<cv::ORB> m_orb;
    cv::Ptr<cv::DescriptorMatcher> m_matcher;
};
