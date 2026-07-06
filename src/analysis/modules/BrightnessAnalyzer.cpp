#include "BrightnessAnalyzer.h"
#include <opencv2/imgproc.hpp>

BrightnessAnalyzer::BrightnessAnalyzer() : BaseAnalyzer("Brightness", true) {}

void BrightnessAnalyzer::analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) {
    cv::Mat leftGray = (!result.leftGray.empty()) ? result.leftGray
        : (leftEye.channels() == 3 ? (cv::cvtColor(leftEye, leftGray, cv::COLOR_BGR2GRAY), leftGray) : leftEye.clone());
    cv::Mat rightGray = (!result.rightGray.empty()) ? result.rightGray
        : (rightEye.channels() == 3 ? (cv::cvtColor(rightEye, rightGray, cv::COLOR_BGR2GRAY), rightGray) : rightEye.clone());

    cv::Scalar meanL = cv::mean(leftGray);
    cv::Scalar meanR = cv::mean(rightGray);

    double brightnessL = meanL[0] / 255.0;
    double brightnessR = meanR[0] / 255.0;

    result.brightnessDelta = std::abs(brightnessL - brightnessR);
}
