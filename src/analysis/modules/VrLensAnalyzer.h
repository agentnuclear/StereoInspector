#pragma once
#include "analysis/IStereoAnalyzer.h"

class VrLensAnalyzer : public BaseAnalyzer {
public:
    VrLensAnalyzer();
    void analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) override;

private:
    double estimateLensDistortion(const cv::Mat& eye);
    double estimateFoveation(const cv::Mat& eye);
    double estimateGodRays(const cv::Mat& eye);
};
