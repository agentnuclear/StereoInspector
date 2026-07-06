#pragma once
#include "analysis/IStereoAnalyzer.h"

class BlurAnalyzer : public BaseAnalyzer {
public:
    BlurAnalyzer();
    void analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) override;

private:
    static double computeBlurVariance(const cv::Mat& img);
};
