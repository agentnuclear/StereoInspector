#include "TemporalAnalyzer.h"
#include <opencv2/imgproc.hpp>
#include <cmath>

TemporalAnalyzer::TemporalAnalyzer()
    : BaseAnalyzer("TemporalAnalyzer", true) {}

void TemporalAnalyzer::analyze(const cv::Mat& leftEye, const cv::Mat& rightEye,
                                AnalysisResult& result) {
    (void)rightEye;
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_history.empty()) {
        FrameSnapshot snap;
        snap.meanLuminance = cv::mean(leftEye)[0];
        snap.disparityMean = result.disparity.meanDisparity;
        snap.healthScore = result.stereoHealthScore;
        if (leftEye.channels() == 3) cv::cvtColor(leftEye, snap.prevLeftGray, cv::COLOR_BGR2GRAY);
        else snap.prevLeftGray = leftEye.clone();
        m_history.push_back(snap);
        return;
    }

    const auto& prev = m_history.back();

    // Flicker score: frame-to-frame luminance change
    double currLum = cv::mean(leftEye)[0];
    double lumDiff = std::abs(currLum - prev.meanLuminance) / 255.0;
    result.temporal.flickerScore = lumDiff;

    // Temporal SSIM on left eye between consecutive frames
    cv::Mat currGray;
    if (leftEye.channels() == 3) cv::cvtColor(leftEye, currGray, cv::COLOR_BGR2GRAY);
    else currGray = leftEye;

    if (prev.prevLeftGray.size() == currGray.size()) {
        cv::Mat diff;
        cv::absdiff(prev.prevLeftGray, currGray, diff);
        double meanDiff = cv::mean(diff)[0] / 255.0;
        result.temporal.temporalSSIM = 1.0 - meanDiff;
    }

    // Disparity stability
    double dispDiff = std::abs(result.disparity.meanDisparity - prev.disparityMean);
    double maxExpectedDisp = std::max(result.disparity.meanDisparity, prev.disparityMean);
    result.temporal.disparityStability = maxExpectedDisp > 1.0
        ? 1.0 - std::min(dispDiff / maxExpectedDisp, 1.0) : 1.0;

    // Detect instant transitions (scene cuts)
    if (lumDiff > 0.3 && result.temporal.temporalSSIM < 0.5) {
        result.temporal.instantTransitions += 1.0;
    }

    // Temporal stability: standard deviation of last N health scores
    if (m_history.size() > 1) {
        double sum = 0.0, sumSq = 0.0;
        size_t n = std::min(m_history.size(), (size_t)30);
        auto it = m_history.end() - n;
        for (; it != m_history.end(); ++it) {
            sum += it->healthScore;
            sumSq += it->healthScore * it->healthScore;
        }
        double mean = sum / n;
        double variance = sumSq / n - mean * mean;
        double stdDev = std::sqrt(std::max(0.0, variance));
        result.temporal.temporalStability = std::max(0.0, 1.0 - stdDev / 100.0);
    }

    // Store current frame snapshot
    FrameSnapshot snap;
    snap.meanLuminance = currLum;
    snap.disparityMean = result.disparity.meanDisparity;
    snap.healthScore = result.stereoHealthScore;
    snap.prevLeftGray = currGray.clone();
    m_history.push_back(snap);

    while (m_history.size() > MAX_FRAMES) m_history.pop_front();
}

void TemporalAnalyzer::pushFrame(const AnalysisResult& result) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_history.empty()) return;
    m_history.back().healthScore = result.stereoHealthScore;
}

void TemporalAnalyzer::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_history.clear();
}
