#include "PixelDiffAnalyzer.h"
#include <opencv2/imgproc.hpp>

PixelDiffAnalyzer::PixelDiffAnalyzer() : BaseAnalyzer("PixelDiff", true) {}

void PixelDiffAnalyzer::analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) {
    cv::Mat diff;
    cv::absdiff(leftEye, rightEye, diff);

    cv::Mat gray = diff.channels() == 3 ? (cv::cvtColor(diff, gray, cv::COLOR_BGR2GRAY), gray) : diff;

    // Weight by disparity valid mask: only count pixels with valid correspondence
    cv::Mat validMask;
    if (!result.disparity.validMap.empty()) {
        cv::resize(result.disparity.validMap, validMask, gray.size(), 0, 0, cv::INTER_NEAREST);
    }

    double totalPixels = validMask.empty() ? (double)gray.total() : (double)cv::countNonZero(validMask);
    if (totalPixels < 1.0) {
        result.pixelDiffPercent = 0.0;
        return;
    }

    cv::Mat diffMask = gray > 10;
    double diffPixels = validMask.empty()
        ? (double)cv::countNonZero(diffMask)
        : (double)cv::countNonZero(diffMask & validMask);
    result.pixelDiffPercent = (diffPixels / totalPixels) * 100.0;
}
