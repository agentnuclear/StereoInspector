#pragma once
#include "analysis/IStereoAnalyzer.h"

class BloomAnalyzer : public BaseAnalyzer {
public:
    BloomAnalyzer();
    void analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) override;
};
