#include "RegionMerger.h"
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>

std::vector<DetectedIssue> RegionMerger::merge(std::vector<DetectedIssue> issues) {
    issues = filterInvalid(issues, 0, 0, cv::Mat());
    issues = mergeOverlapping(issues);
    return issues;
}

bool RegionMerger::isOverlapping(const IssueRect& a, const IssueRect& b, float threshold) const {
    int ax1 = a.x, ay1 = a.y, ax2 = a.x + a.width, ay2 = a.y + a.height;
    int bx1 = b.x, by1 = b.y, bx2 = b.x + b.width, by2 = b.y + b.height;

    int ix1 = std::max(ax1, bx1), iy1 = std::max(ay1, by1);
    int ix2 = std::min(ax2, bx2), iy2 = std::min(ay2, by2);

    if (ix2 <= ix1 || iy2 <= iy1) {
        // Check distance-based merge (close but not overlapping)
        int cx1 = ax1 + a.width / 2, cy1 = ay1 + a.height / 2;
        int cx2 = bx1 + b.width / 2, cy2 = by1 + b.height / 2;
        float dist = std::sqrt((float)((cx1 - cx2) * (cx1 - cx2) + (cy1 - cy2) * (cy1 - cy2)));
        float avgSize = std::sqrt((float)(a.width * a.height + b.width * b.height) / 2.0f);
        return dist < avgSize * threshold;
    }

    int interArea = (ix2 - ix1) * (iy2 - iy1);
    int areaA = a.width * a.height;
    int areaB = b.width * b.height;
    int minArea = std::min(areaA, areaB);

    return minArea > 0 && (float)interArea / (float)minArea > 0.3f;
}

IssueRect RegionMerger::mergeRects(const IssueRect& a, const IssueRect& b) const {
    IssueRect r;
    r.x = std::min(a.x, b.x);
    r.y = std::min(a.y, b.y);
    r.width = std::max(a.x + a.width, b.x + b.width) - r.x;
    r.height = std::max(a.y + a.height, b.y + b.height) - r.y;
    return r;
}

std::vector<DetectedIssue> RegionMerger::mergeOverlapping(const std::vector<DetectedIssue>& issues) {
    if (issues.empty()) return {};

    std::vector<DetectedIssue> sorted = issues;
    std::sort(sorted.begin(), sorted.end(),
              [](const DetectedIssue& a, const DetectedIssue& b) {
                  return a.severity > b.severity;
              });

    std::vector<bool> merged(sorted.size(), false);
    std::vector<DetectedIssue> result;

    for (size_t i = 0; i < sorted.size(); i++) {
        if (merged[i]) continue;

        DetectedIssue cluster = sorted[i];
        bool changed = true;

        while (changed) {
            changed = false;
            for (size_t j = i + 1; j < sorted.size(); j++) {
                if (merged[j]) continue;
                if (isOverlapping(cluster.boundingBox, sorted[j].boundingBox, m_config.mergeDistanceRatio)) {
                    // Merge
                    cluster.boundingBox = mergeRects(cluster.boundingBox, sorted[j].boundingBox);
                    cluster.areaPixels = cluster.boundingBox.width * cluster.boundingBox.height;
                    cluster.centerX = (float)(cluster.boundingBox.x + cluster.boundingBox.width / 2);
                    cluster.centerY = (float)(cluster.boundingBox.y + cluster.boundingBox.height / 2);
                    cluster.severity = std::max(cluster.severity, sorted[j].severity);
                    cluster.confidence = std::max(cluster.confidence, sorted[j].confidence);
                    if (sorted[j].confidence > cluster.confidence) {
                        cluster.type = sorted[j].type;
                        cluster.confidence = sorted[j].confidence;
                        cluster.alternatives = sorted[j].alternatives;
                        cluster.reasoningText = sorted[j].reasoningText;
                    }
                    merged[j] = true;
                    changed = true;
                }
            }
        }

        result.push_back(cluster);
    }

    std::sort(result.begin(), result.end(),
              [](const DetectedIssue& a, const DetectedIssue& b) {
                  return a.severity > b.severity;
              });

    return result;
}

bool RegionMerger::isLensBorder(const IssueRect& r, int frameW, int frameH) const {
    if (frameW == 0 || frameH == 0) return false;
    float margin = m_config.lensBorderMargin;
    int marginX = (int)(frameW * margin);
    int marginY = (int)(frameH * margin);

    // Check if region touches the image border
    bool touchesTop = r.y <= marginY;
    bool touchesBottom = (r.y + r.height) >= (frameH - marginY);
    bool touchesLeft = r.x <= marginX;
    bool touchesRight = (r.x + r.width) >= (frameW - marginX);

    // Lens border regions are typically thin strips along edges
    bool isThinStrip = (r.width < frameW * 0.1f) || (r.height < frameH * 0.1f);
    bool touchesAnyEdge = touchesTop || touchesBottom || touchesLeft || touchesRight;

    return touchesAnyEdge && isThinStrip;
}

bool RegionMerger::isVignette(const IssueRect& r, int frameW, int frameH) const {
    if (frameW == 0 || frameH == 0) return false;
    float cx = (float)(r.x + r.width / 2) / (float)frameW - 0.5f;
    float cy = (float)(r.y + r.height / 2) / (float)frameH - 0.5f;
    float dist = std::sqrt(cx * cx + cy * cy);
    float vignetteRadius = m_config.vignetteRadius;
    return dist > vignetteRadius;
}

bool RegionMerger::isLowInformation(const IssueRect& r, const cv::Mat& gray) const {
    if (gray.empty()) return false;
    cv::Rect box(r.x, r.y, r.width, r.height);
    box &= cv::Rect(0, 0, gray.cols, gray.rows);
    if (box.width < 2 || box.height < 2) return true;

    cv::Mat region = gray(box);
    cv::Scalar mean, stddev;
    cv::meanStdDev(region, mean, stddev);

    // Low variance = low information
    if (stddev[0] < 5.0) return true;

    // Very small regions
    if (r.width * r.height < m_config.smallRegionThreshold) return true;

    return false;
}

std::vector<DetectedIssue> RegionMerger::filterInvalid(const std::vector<DetectedIssue>& issues,
                                                        int frameW, int frameH,
                                                        const cv::Mat& /*disparityValid*/) {
    std::vector<DetectedIssue> result;

    for (auto issue : issues) {
        issue.isInvalidRegion = false;

        // Check area threshold
        if (issue.areaPixels < m_config.minArea) {
            issue.isInvalidRegion = true;
            issue.invalidReason = "Too small (" + std::to_string(issue.areaPixels) + " px)";
            result.push_back(issue);
            continue;
        }

        if (m_config.maxArea > 0 && issue.areaPixels > m_config.maxArea) {
            issue.isInvalidRegion = true;
            issue.invalidReason = "Too large";
            result.push_back(issue);
            continue;
        }

        // Check lens border
        if (frameW > 0 && frameH > 0) {
            if (isLensBorder(issue.boundingBox, frameW, frameH)) {
                issue.type = IssueType::LensBoundary;
                issue.isInvalidRegion = true;
                issue.invalidReason = "Lens boundary region";
                result.push_back(issue);
                continue;
            }

            if (isVignette(issue.boundingBox, frameW, frameH)) {
                issue.type = IssueType::LensBoundary;
                issue.isInvalidRegion = true;
                issue.invalidReason = "Vignette / lens distortion area";
                result.push_back(issue);
                continue;
            }
        }

        result.push_back(issue);
    }

    return result;
}
