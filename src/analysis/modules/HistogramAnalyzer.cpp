#include "HistogramAnalyzer.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

HistogramAnalyzer::HistogramAnalyzer() : BaseAnalyzer("Histogram", true) {}

void HistogramAnalyzer::analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) {
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

    cv::Mat histL, histR;
    int channels[] = {0};
    int histSize[] = {m_histSize};
    const float* ranges[] = {m_range};

    cv::calcHist(&leftGray, 1, channels, cv::Mat(), histL, 1, histSize, ranges);
    cv::calcHist(&rightGray, 1, channels, cv::Mat(), histR, 1, histSize, ranges);

    cv::normalize(histL, histL, 1.0, 0.0, cv::NORM_L1);
    cv::normalize(histR, histR, 1.0, 0.0, cv::NORM_L1);

    result.histogramCorrelation = cv::compareHist(histL, histR, cv::HISTCMP_CORREL);
    if (result.histogramCorrelation < 0) result.histogramCorrelation = 0;
}
