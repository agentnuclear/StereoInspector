#pragma once
#include "core/Types.h"
#include "analysis/IssueClassifier.h"
#include <opencv2/core.hpp>
#include <string>
#include <fstream>

class DataCollector {
public:
    DataCollector(const std::string& outputDir, int maxFrames = -1);
    ~DataCollector();

    bool onFrame(const AnalysisResult& result,
                 const cv::Mat& leftEye, const cv::Mat& rightEye);

    int framesWritten() const { return m_frameCount; }

private:
    void writeJsonl(const AnalysisResult& result,
                    const std::string& leftPath, const std::string& rightPath);
    std::string padFrame(int n) const;

    std::string m_outputDir;
    int m_maxFrames;
    int m_frameCount = 0;
    std::ofstream m_jsonl;
};
