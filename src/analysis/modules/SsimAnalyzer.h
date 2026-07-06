#pragma once
#include "analysis/IStereoAnalyzer.h"

class SsimAnalyzer : public BaseAnalyzer {
public:
    SsimAnalyzer();
    void analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) override;

private:
    static double computeSSIM(const cv::Mat& img1, const cv::Mat& img2);
};
