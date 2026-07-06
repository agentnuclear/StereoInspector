#include "BloomAnalyzer.h"
#include <opencv2/imgproc.hpp>

BloomAnalyzer::BloomAnalyzer() : BaseAnalyzer("Bloom", true) {}

void BloomAnalyzer::analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) {
    cv::Mat leftGray;
    if (!result.leftGray.empty()) {
        leftGray = result.leftGray;
    } else if (leftEye.channels() == 3) {
        cv::cvtColor(leftEye, leftGray, cv::COLOR_BGR2GRAY);
    } else {
        leftGray = leftEye.clone();
    }
    cv::Mat rightGray;
    if (!result.rightGray.empty()) {
        rightGray = result.rightGray;
    } else if (rightEye.channels() == 3) {
        cv::cvtColor(rightEye, rightGray, cv::COLOR_BGR2GRAY);
    } else {
        rightGray = rightEye.clone();
    }

    cv::Mat blurredL, blurredR;
    cv::GaussianBlur(leftGray, blurredL, cv::Size(21, 21), 10);
    cv::GaussianBlur(rightGray, blurredR, cv::Size(21, 21), 10);

    cv::Mat brightL, brightR;
    cv::threshold(blurredL, brightL, 200, 255, cv::THRESH_BINARY);
    cv::threshold(blurredR, brightR, 200, 255, cv::THRESH_BINARY);

    double bloomL = (double)cv::countNonZero(brightL) / (double)brightL.total();
    double bloomR = (double)cv::countNonZero(brightR) / (double)brightR.total();

    result.bloomDifference = std::abs(bloomL - bloomR);
}
