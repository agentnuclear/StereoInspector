#pragma once
#include "analysis/IStereoAnalyzer.h"

class EdgeAnalyzer : public BaseAnalyzer {
public:
    EdgeAnalyzer();
    void analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) override;

private:
    double m_lowThreshold = 50.0;
    double m_highThreshold = 150.0;
};
