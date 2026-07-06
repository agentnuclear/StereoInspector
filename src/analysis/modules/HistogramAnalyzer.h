#pragma once
#include "analysis/IStereoAnalyzer.h"

class HistogramAnalyzer : public BaseAnalyzer {
public:
    HistogramAnalyzer();
    void analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) override;

private:
    int m_histSize = 256;
    float m_range[2] = {0, 256};
};
