#pragma once
#include "analysis/IStereoAnalyzer.h"

class ShadowAnalyzer : public BaseAnalyzer {
public:
    ShadowAnalyzer();
    void analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) override;
};
