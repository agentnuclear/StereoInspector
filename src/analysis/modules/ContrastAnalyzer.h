#pragma once
#include "analysis/IStereoAnalyzer.h"

class ContrastAnalyzer : public BaseAnalyzer {
public:
    ContrastAnalyzer();
    void analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) override;
};
