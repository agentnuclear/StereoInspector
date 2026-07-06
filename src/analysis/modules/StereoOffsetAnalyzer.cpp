#include "StereoOffsetAnalyzer.h"
#include "analysis/FeatureCache.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>

StereoOffsetAnalyzer::StereoOffsetAnalyzer() : BaseAnalyzer("StereoOffset", true) {
    m_matcher = cv::DescriptorMatcher::create(cv::DescriptorMatcher::BRUTEFORCE_HAMMING);
}

void StereoOffsetAnalyzer::analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) {
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

    FeatureCache::instance().getOrb(leftGray, rightGray, kpL, kpR, descL, descR, 300);

    if (descL.empty() || descR.empty()) {
        result.stereoOffset = 0.0;
        return;
    }

    std::vector<cv::DMatch> matches;
    m_matcher->match(descL, descR, matches);

    if (matches.empty()) {
        result.stereoOffset = 0.0;
        return;
    }

    double minDist = std::numeric_limits<double>::max();
    for (const auto& m : matches) {
        if (m.distance < minDist) minDist = m.distance;
    }

    std::vector<cv::DMatch> goodMatches;
    for (const auto& m : matches) {
        if (m.distance <= std::max(2.0 * minDist, 30.0)) {
            goodMatches.push_back(m);
        }
    }

    if (goodMatches.size() < 4) {
        result.stereoOffset = 0.0;
        return;
    }

    double totalOffset = 0.0;
    for (const auto& m : goodMatches) {
        double dx = kpL[m.queryIdx].pt.x - kpR[m.trainIdx].pt.x;
        totalOffset += std::abs(dx);
    }

    result.stereoOffset = totalOffset / goodMatches.size();
}
