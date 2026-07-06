#pragma once
#include "core/Types.h"
#include <opencv2/core.hpp>

struct StereoDetectionResult {
    StereoLayout layout = StereoLayout::SideBySide;
    int splitPoint = 0;         // column or row where the split is
    double confidence = 0.0;    // 0–1 detection confidence
    bool horizontalSplit = true; // true = left/right, false = top/bottom
    cv::Rect leftRect;
    cv::Rect rightRect;
};

class StereoDetector {
public:
    StereoDetector();

    StereoDetectionResult detect(const cv::Mat& fullFrame);

    bool isInitialSplitValid() const { return m_initialized; }

private:
    StereoDetectionResult detectHorizontal(const cv::Mat& frame, int candidateSplit);
    StereoDetectionResult detectVertical(const cv::Mat& frame, int candidateSplit);
    StereoDetectionResult findBestSplit(const cv::Mat& frame);

    double scoreSplitAt(const cv::Mat& frame, int splitX, bool horizontal);
    double compareRegions(const cv::Mat& region1, const cv::Mat& region2);

    StereoDetectionResult m_lastResult;
    bool m_initialized = false;
    int m_frameWidth = 0;
    int m_frameHeight = 0;
};
