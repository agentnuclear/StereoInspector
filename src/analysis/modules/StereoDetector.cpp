#include "StereoDetector.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>
#include <algorithm>
#include <cmath>

StereoDetector::StereoDetector() = default;

StereoDetectionResult StereoDetector::detect(const cv::Mat& fullFrame) {
    if (fullFrame.empty()) return {};

    m_frameWidth = fullFrame.cols;
    m_frameHeight = fullFrame.rows;

    // Try multiple candidate split positions and pick the best
    StereoDetectionResult result = findBestSplit(fullFrame);

    // If confidence is low but we've already detected once, prefer last layout
    if (result.confidence < 0.3 && m_initialized) {
        result = m_lastResult;
    } else if (result.confidence >= 0.3) {
        m_initialized = true;
        m_lastResult = result;
    } else {
        // Fallback: assume side-by-side at midpoint
        int mid = fullFrame.cols / 2;
        result.layout = StereoLayout::SideBySide;
        result.splitPoint = mid;
        result.horizontalSplit = true;
        result.leftRect = cv::Rect(0, 0, mid, fullFrame.rows);
        result.rightRect = cv::Rect(mid, 0, fullFrame.cols - mid, fullFrame.rows);
        result.confidence = 0.5;
    }

    return result;
}

double StereoDetector::compareRegions(const cv::Mat& region1, const cv::Mat& region2) {
    cv::Mat g1, g2;
    if (region1.channels() == 3) {
        cv::cvtColor(region1, g1, cv::COLOR_BGR2GRAY);
        cv::cvtColor(region2, g2, cv::COLOR_BGR2GRAY);
    } else {
        g1 = region1;
        g2 = region2;
    }

    // Resize larger region to match smaller
    if (g1.size() != g2.size()) {
        cv::resize(g2, g2, g1.size());
    }

    // Histogram correlation
    int histSize = 64;
    float range[] = {0, 256};
    const float* histRange = {range};
    cv::Mat hist1, hist2;
    cv::calcHist(&g1, 1, 0, cv::Mat(), hist1, 1, &histSize, &histRange);
    cv::calcHist(&g2, 1, 0, cv::Mat(), hist2, 1, &histSize, &histRange);
    cv::normalize(hist1, hist1, 1.0, 0.0, cv::NORM_L1);
    cv::normalize(hist2, hist2, 1.0, 0.0, cv::NORM_L1);
    double histCorr = cv::compareHist(hist1, hist2, cv::HISTCMP_CORREL);
    if (histCorr < 0) histCorr = 0;

    // Edge similarity
    cv::Mat e1, e2;
    cv::Canny(g1, e1, 50, 150);
    cv::Canny(g2, e2, 50, 150);
    cv::Mat andEdges, orEdges;
    cv::bitwise_and(e1, e2, andEdges);
    cv::bitwise_or(e1, e2, orEdges);
    double edgeSim = 0.0;
    double orCount = (double)cv::countNonZero(orEdges);
    if (orCount > 0) {
        edgeSim = (double)cv::countNonZero(andEdges) / orCount;
    }

    // Combined score: weighted average
    return 0.6 * histCorr + 0.4 * edgeSim;
}

double StereoDetector::scoreSplitAt(const cv::Mat& frame, int splitX, bool horizontal) {
    if (splitX <= 0 || splitX >= (horizontal ? frame.cols : frame.rows)) return 0.0;

    cv::Mat leftRegion, rightRegion;

    if (horizontal) {
        int leftWidth = splitX;
        int rightWidth = frame.cols - splitX;
        int minWidth = std::min(leftWidth, rightWidth);
        int height = frame.rows;

        leftRegion = frame(cv::Rect(splitX - minWidth, 0, minWidth, height)).clone();
        rightRegion = frame(cv::Rect(splitX, 0, minWidth, height)).clone();
    } else {
        int topHeight = splitX;
        int bottomHeight = frame.rows - splitX;
        int minHeight = std::min(topHeight, bottomHeight);
        int width = frame.cols;

        leftRegion = frame(cv::Rect(0, splitX - minHeight, width, minHeight)).clone();
        rightRegion = frame(cv::Rect(0, splitX, width, minHeight)).clone();
    }

    double similarity = compareRegions(leftRegion, rightRegion);

    // Also check the black border / separator confidence
    cv::Mat strip;
    if (horizontal) {
        int stripW = std::max(4, frame.cols / 200);
        int stripStart = std::max(0, splitX - stripW / 2);
        strip = frame(cv::Rect(stripStart, 0, stripW, frame.rows)).clone();
    } else {
        int stripH = std::max(4, frame.rows / 200);
        int stripStart = std::max(0, splitX - stripH / 2);
        strip = frame(cv::Rect(0, stripStart, frame.cols, stripH)).clone();
    }

    cv::Mat grayStrip;
    if (strip.channels() == 3) cv::cvtColor(strip, grayStrip, cv::COLOR_BGR2GRAY);
    else grayStrip = strip;

    cv::Scalar mean = cv::mean(grayStrip);
    double darknessBonus = 1.0 - (mean[0] / 255.0);

    // A good split has high similarity between regions AND a dark separator
    return 0.5 * similarity + 0.5 * darknessBonus;
}

StereoDetectionResult StereoDetector::findBestSplit(const cv::Mat& frame) {
    StereoDetectionResult best;
    best.confidence = 0.0;

    int w = frame.cols;
    int h = frame.rows;

    struct Candidate {
        int pos;
        double score;
        bool horizontal;
    };
    std::vector<Candidate> candidates;

    // Try horizontal splits (side-by-side)
    // Check at 25%, 33%, 50%, 66%, 75% positions for robustness
    std::vector<int> hCandidates;
    hCandidates.push_back(w / 4);
    hCandidates.push_back(w / 3);
    hCandidates.push_back(w / 2);
    hCandidates.push_back(2 * w / 3);
    hCandidates.push_back(3 * w / 4);

    // Also search around the midpoint ±10%
    int mid = w / 2;
    int range = w / 10;
    for (int x = mid - range; x <= mid + range; x += std::max(1, w / 40)) {
        hCandidates.push_back(x);
    }

    // Try vertical splits (over-under)
    std::vector<int> vCandidates;
    vCandidates.push_back(h / 2);
    vCandidates.push_back(h / 3);
    vCandidates.push_back(2 * h / 3);

    for (int x : hCandidates) {
        if (x < w * 0.15 || x > w * 0.85) continue;
        double score = scoreSplitAt(frame, x, true);
        candidates.push_back({x, score, true});
    }

    for (int y : vCandidates) {
        if (y < h * 0.15 || y > h * 0.85) continue;
        double score = scoreSplitAt(frame, y, false);
        candidates.push_back({y, score, false});
    }

    if (candidates.empty()) return best;

    auto bestIt = std::max_element(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) { return a.score < b.score; });

    best.confidence = bestIt->score;
    best.splitPoint = bestIt->pos;
    best.horizontalSplit = bestIt->horizontal;

    if (best.horizontalSplit) {
        int leftW = best.splitPoint;
        int rightW = w - best.splitPoint;
        // Determine the layout type
        if (std::abs(leftW - rightW) < w * 0.02) {
            best.layout = StereoLayout::SideBySide;
        } else {
            best.layout = StereoLayout::SideBySide_Wide;
        }
        // Ensure both regions are valid; if one is larger it may include bezel
        int eyeW = std::min(leftW, rightW);
        best.leftRect = cv::Rect(best.splitPoint - eyeW, 0, eyeW, h);
        best.rightRect = cv::Rect(best.splitPoint, 0, eyeW, h);
    } else {
        best.layout = StereoLayout::OverUnder;
        int topH = best.splitPoint;
        int botH = h - best.splitPoint;
        int eyeH = std::min(topH, botH);
        best.leftRect = cv::Rect(0, best.splitPoint - eyeH, w, eyeH);
        best.rightRect = cv::Rect(0, best.splitPoint, w, eyeH);
    }

    return best;
}
