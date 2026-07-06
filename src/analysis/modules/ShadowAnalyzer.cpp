#include "ShadowAnalyzer.h"
#include <opencv2/imgproc.hpp>

ShadowAnalyzer::ShadowAnalyzer() : BaseAnalyzer("Shadow", true) {}

void ShadowAnalyzer::analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) {
    cv::Mat leftGray = (!result.leftGray.empty()) ? result.leftGray
        : (leftEye.channels() == 3 ? (cv::cvtColor(leftEye, leftGray, cv::COLOR_BGR2GRAY), leftGray) : leftEye.clone());
    cv::Mat rightGray = (!result.rightGray.empty()) ? result.rightGray
        : (rightEye.channels() == 3 ? (cv::cvtColor(rightEye, rightGray, cv::COLOR_BGR2GRAY), rightGray) : rightEye.clone());

    cv::Mat darkL, darkR;
    cv::threshold(leftGray, darkL, 50, 255, cv::THRESH_BINARY_INV);
    cv::threshold(rightGray, darkR, 50, 255, cv::THRESH_BINARY_INV);

    double shadowL = (double)cv::countNonZero(darkL) / (double)darkL.total();
    double shadowR = (double)cv::countNonZero(darkR) / (double)darkR.total();

    result.shadowDifference = std::abs(shadowL - shadowR);
}
