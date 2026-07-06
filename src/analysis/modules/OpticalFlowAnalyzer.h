#pragma once
#include "analysis/IStereoAnalyzer.h"
#include <opencv2/video.hpp>

class OpticalFlowAnalyzer : public BaseAnalyzer {
public:
    OpticalFlowAnalyzer();
    void analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) override;
};
