#include "OrbAnalyzer.h"
#include <opencv2/imgproc.hpp>

OrbAnalyzer::OrbAnalyzer() : BaseAnalyzer("ORB", true) {
    m_orb = cv::ORB::create(500);
    m_matcher = cv::DescriptorMatcher::create(cv::DescriptorMatcher::BRUTEFORCE_HAMMING);
}

void OrbAnalyzer::analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) {
    cv::Mat leftGray, rightGray;

    if (leftEye.channels() == 3) {
        cv::cvtColor(leftEye, leftGray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(rightEye, rightGray, cv::COLOR_BGR2GRAY);
    } else {
        leftGray = leftEye.clone();
        rightGray = rightEye.clone();
    }

    std::vector<cv::KeyPoint> kpL, kpR;
    cv::Mat descL, descR;

    m_orb->detectAndCompute(leftGray, cv::Mat(), kpL, descL);
    m_orb->detectAndCompute(rightGray, cv::Mat(), kpR, descR);

    if (descL.empty() || descR.empty()) {
        result.featureMatchCount = 0;
        return;
    }

    std::vector<cv::DMatch> matches;
    m_matcher->match(descL, descR, matches);

    if (matches.empty()) {
        result.featureMatchCount = 0;
        return;
    }

    double minDist = std::numeric_limits<double>::max();
    double maxDist = 0;
    for (const auto& m : matches) {
        if (m.distance < minDist) minDist = m.distance;
        if (m.distance > maxDist) maxDist = m.distance;
    }

    int goodCount = 0;
    for (const auto& m : matches) {
        if (m.distance <= std::max(2.0 * minDist, 30.0)) {
            goodCount++;
        }
    }

    result.featureMatchCount = goodCount;
}
