#include "EdgeAnalyzer.h"
#include <opencv2/imgproc.hpp>

EdgeAnalyzer::EdgeAnalyzer() : BaseAnalyzer("Edge", true) {}

void EdgeAnalyzer::analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) {
    cv::Mat leftGray, rightGray;

    if (leftEye.channels() == 3) {
        cv::cvtColor(leftEye, leftGray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(rightEye, rightGray, cv::COLOR_BGR2GRAY);
    } else {
        leftGray = leftEye.clone();
        rightGray = rightEye.clone();
    }

    cv::Mat edgesL, edgesR;
    cv::Canny(leftGray, edgesL, m_lowThreshold, m_highThreshold);
    cv::Canny(rightGray, edgesR, m_lowThreshold, m_highThreshold);

    cv::Mat and_edges, or_edges;
    cv::bitwise_and(edgesL, edgesR, and_edges);
    cv::bitwise_or(edgesL, edgesR, or_edges);

    double intersection = (double)cv::countNonZero(and_edges);
    double union_ = (double)cv::countNonZero(or_edges);

    if (union_ > 0) {
        result.edgeSimilarity = intersection / union_;
    } else {
        result.edgeSimilarity = 1.0;
    }
}
