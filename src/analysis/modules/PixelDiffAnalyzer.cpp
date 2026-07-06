#include "PixelDiffAnalyzer.h"
#include <opencv2/imgproc.hpp>

PixelDiffAnalyzer::PixelDiffAnalyzer() : BaseAnalyzer("PixelDiff", true) {}

void PixelDiffAnalyzer::analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) {
    cv::Mat diff;
    cv::absdiff(leftEye, rightEye, diff);

    cv::Mat gray;
    if (diff.channels() == 3) {
        cv::cvtColor(diff, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = diff;
    }

    double totalPixels = (double)gray.total();
    double diffPixels = (double)cv::countNonZero(gray > 10);
    result.pixelDiffPercent = (diffPixels / totalPixels) * 100.0;
}
