#pragma once
#include "analysis/IStereoAnalyzer.h"

class OcrAnalyzer : public BaseAnalyzer {
public:
    OcrAnalyzer();
    void analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) override;

private:
    void* m_tesseract = nullptr;
};
