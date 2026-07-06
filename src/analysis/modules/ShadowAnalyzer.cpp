#include "ShadowAnalyzer.h"
#include <opencv2/imgproc.hpp>

ShadowAnalyzer::ShadowAnalyzer() : BaseAnalyzer("Shadow", true) {}

void ShadowAnalyzer::analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) {
    cv::Mat leftGray, rightGray;

    if (leftEye.channels() == 3) {
        cv::cvtColor(leftEye, leftGray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(rightEye, rightGray, cv::COLOR_BGR2GRAY);
    } else {
        leftGray = leftEye.clone();
        rightGray = rightEye.clone();
    }

    cv::Mat darkL, darkR;
    cv::threshold(leftGray, darkL, 50, 255, cv::THRESH_BINARY_INV);
    cv::threshold(rightGray, darkR, 50, 255, cv::THRESH_BINARY_INV);

    double shadowL = (double)cv::countNonZero(darkL) / (double)darkL.total();
    double shadowR = (double)cv::countNonZero(darkR) / (double)darkR.total();

    result.shadowDifference = std::abs(shadowL - shadowR);
}
