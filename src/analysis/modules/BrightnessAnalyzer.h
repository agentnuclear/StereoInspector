#pragma once
#include "analysis/IStereoAnalyzer.h"

class BrightnessAnalyzer : public BaseAnalyzer {
public:
    BrightnessAnalyzer();
    void analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) override;
};
