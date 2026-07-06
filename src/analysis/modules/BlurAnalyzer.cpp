#include "BlurAnalyzer.h"
#include <opencv2/imgproc.hpp>

BlurAnalyzer::BlurAnalyzer() : BaseAnalyzer("Blur", true) {}

double BlurAnalyzer::computeBlurVariance(const cv::Mat& img) {
    cv::Mat gray;
    if (img.channels() == 3) {
        cv::cvtColor(img, gray, cv::COLOR_BGR2GRAY);
    } else {
        gray = img;
    }

    cv::Mat laplacian;
    cv::Laplacian(gray, laplacian, CV_64F);

    cv::Scalar mean, stddev;
    cv::meanStdDev(laplacian, mean, stddev);

    return stddev[0] * stddev[0];
}

void BlurAnalyzer::analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) {
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
    double blurL = computeBlurVariance(leftGray);
    double blurR = computeBlurVariance(rightGray);

    double maxBlur = std::max(blurL, blurR);
    if (maxBlur > 0) {
        result.blurDelta = std::abs(blurL - blurR) / maxBlur;
    } else {
        result.blurDelta = 0.0;
    }
}
