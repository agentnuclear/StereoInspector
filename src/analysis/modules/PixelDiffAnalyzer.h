#pragma once
#include "analysis/IStereoAnalyzer.h"

class PixelDiffAnalyzer : public BaseAnalyzer {
public:
    PixelDiffAnalyzer();
    void analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) override;
};
