#include "ContrastAnalyzer.h"
#include <opencv2/imgproc.hpp>

ContrastAnalyzer::ContrastAnalyzer() : BaseAnalyzer("Contrast", true) {}

void ContrastAnalyzer::analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) {
    cv::Mat leftGray = (!result.leftGray.empty()) ? result.leftGray
        : (leftEye.channels() == 3 ? (cv::cvtColor(leftEye, leftGray, cv::COLOR_BGR2GRAY), leftGray) : leftEye.clone());
    cv::Mat rightGray = (!result.rightGray.empty()) ? result.rightGray
        : (rightEye.channels() == 3 ? (cv::cvtColor(rightEye, rightGray, cv::COLOR_BGR2GRAY), rightGray) : rightEye.clone());

    cv::Scalar meanL, stdL, meanR, stdR;
    cv::meanStdDev(leftGray, meanL, stdL);
    cv::meanStdDev(rightGray, meanR, stdR);

    double contrastL = stdL[0] / 255.0;
    double contrastR = stdR[0] / 255.0;

    result.contrastDelta = std::abs(contrastL - contrastR);
}
