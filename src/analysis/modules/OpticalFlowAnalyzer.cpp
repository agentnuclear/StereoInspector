#include "OpticalFlowAnalyzer.h"
#include <opencv2/imgproc.hpp>

OpticalFlowAnalyzer::OpticalFlowAnalyzer() : BaseAnalyzer("OpticalFlow", true) {}

void OpticalFlowAnalyzer::analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) {
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

    std::vector<cv::Point2f> corners;
    cv::goodFeaturesToTrack(leftGray, corners, 200, 0.01, 10);

    if (corners.size() < 10) {
        result.opticalFlowMagnitude = 0.0;
        return;
    }

    std::vector<cv::Point2f> nextPts;
    std::vector<uchar> status;
    std::vector<float> err;

    cv::calcOpticalFlowPyrLK(leftGray, rightGray, corners, nextPts, status, err);

    double totalMag = 0.0;
    int valid = 0;
    for (size_t i = 0; i < status.size(); i++) {
        if (status[i]) {
            double dx = nextPts[i].x - corners[i].x;
            double dy = nextPts[i].y - corners[i].y;
            totalMag += std::sqrt(dx * dx + dy * dy);
            valid++;
        }
    }

    if (valid > 0) {
        result.opticalFlowMagnitude = totalMag / valid;
    } else {
        result.opticalFlowMagnitude = 0.0;
    }
}
