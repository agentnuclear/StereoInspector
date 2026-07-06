#include "Visualizer.h"
#include "analysis/FeatureCache.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/highgui.hpp>
#include <algorithm>
#include <cmath>

Visualizer::Visualizer() = default;
Visualizer::~Visualizer() = default;

cv::Mat Visualizer::render(const cv::Mat& frame, const cv::Mat& leftEye, const cv::Mat& rightEye,
                            const AnalysisResult& result, VisualizationMode mode,
                            const MetricHistory* history,
                            int selectedIssueIndex) const {
    cv::Mat output;
    switch (mode) {
        case VisualizationMode::Normal:
            output = renderNormal(frame, &result, history);
            break;
        case VisualizationMode::DifferenceHeatmap:
            output = renderDifferenceHeatmap(leftEye, rightEye, result.residualDiffMap);
            break;
        case VisualizationMode::StereoDifferenceOverlay:
            output = renderStereoDifferenceOverlay(leftEye, rightEye, result);
            break;
        case VisualizationMode::EdgeComparison:
            output = renderEdgeComparison(leftEye, rightEye);
            break;
        case VisualizationMode::FeatureMatchOverlay:
            output = renderFeatureMatch(leftEye, rightEye);
            break;
        case VisualizationMode::HistogramView:
            output = renderHistogramView(leftEye, rightEye);
            break;
        case VisualizationMode::BlurMap:
            output = renderBlurMap(leftEye, rightEye);
            break;
        case VisualizationMode::BlinkLeft:
            output = renderBlink(leftEye, rightEye, true);
            break;
        case VisualizationMode::BlinkRight:
            output = renderBlink(leftEye, rightEye, false);
            break;
        case VisualizationMode::DisparityHeatmap:
            output = renderDisparityHeatmap(result);
            break;
        default:
            output = renderNormal(frame, &result, history);
            break;
    }

    overlaySelectedIssue(output, result, selectedIssueIndex);
    return output;
}

void Visualizer::overlayMiniGraph(cv::Mat& frame, const HistoryBuffer& data,
                                   int x, int y, int w, int h,
                                   double minVal, double maxVal,
                                   const cv::Scalar& color,
                                   const std::string& label) const {
    if (data.empty()) return;

    double range = maxVal - minVal;
    if (range < 0.001) range = 1.0;

    // Background
    cv::rectangle(frame, cv::Rect(x, y, w, h), cv::Scalar(15, 15, 25, 180), -1);
    cv::rectangle(frame, cv::Rect(x, y, w, h), cv::Scalar(60, 60, 80), 1);

    // Grid lines
    for (int i = 0; i <= 4; i++) {
        int gy = y + h - 1 - (h * i) / 4;
        cv::line(frame, cv::Point(x, gy), cv::Point(x + w, gy),
                 cv::Scalar(50, 50, 65), 1);
    }

    // Data line
    size_t n = data.size();
    if (n >= 2) {
        std::vector<cv::Point> points;
        for (size_t i = 0; i < n; i++) {
            int px = x + (int)((double)i / (double)(n - 1) * w);
            int py = y + h - 1 - (int)(((data[i] - minVal) / range) * (h - 1));
            py = std::max(y, std::min(y + h - 1, py));
            points.emplace_back(px, py);
        }

        for (size_t i = 1; i < points.size(); i++) {
            cv::line(frame, points[i - 1], points[i], color, 2);
        }

        // End dot
        cv::circle(frame, points.back(), 3, color, -1);

        // Current value label
        char valBuf[64];
        snprintf(valBuf, sizeof(valBuf), "%s: %.2f", label.c_str(), data.back());
        cv::putText(frame, valBuf, cv::Point(x + 4, y + 14),
                    cv::FONT_HERSHEY_SIMPLEX, 0.4, cv::Scalar(180, 180, 200), 1);
    }
}

cv::Mat Visualizer::renderNormal(const cv::Mat& frame, const AnalysisResult* result,
                                  const MetricHistory* history) const {
    cv::Mat output = frame.clone();

    if (result && history) {
        int gX = 10, gY = output.rows - 90;
        int gW = 140, gH = 60;

        overlayMiniGraph(output, history->healthScore, gX, gY, gW, gH,
                         0.0, 100.0, cv::Scalar(80, 180, 80), "Health");
        overlayMiniGraph(output, history->ssim, gX + gW + 6, gY, gW, gH,
                         0.0, 1.0, cv::Scalar(220, 180, 40), "SSIM");
        overlayMiniGraph(output, history->pixelDiff, gX, gY - gH - 6, gW, gH,
                         0.0, 100.0, cv::Scalar(60, 140, 220), "PixDiff");
        overlayMiniGraph(output, history->brightnessDelta, gX + gW + 6, gY - gH - 6, gW, gH,
                         0.0, 1.0, cv::Scalar(200, 80, 160), "Bright\u0394");

        // Health score overlay
        char buf[64];
        snprintf(buf, sizeof(buf), "Health: %.1f", result->stereoHealthScore);
        cv::putText(output, buf, cv::Point(output.cols - 200, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6,
                    result->stereoHealthScore >= 80 ? cv::Scalar(80, 200, 80) :
                    result->stereoHealthScore >= 50 ? cv::Scalar(60, 180, 220) :
                    cv::Scalar(60, 60, 220),
                    2);
    }

    return output;
}

void Visualizer::overlaySelectedIssue(cv::Mat& image, const AnalysisResult& result,
                                       int selectedIndex) const {
    if (selectedIndex < 0 || selectedIndex >= (int)result.detectedIssues.size()) return;
    const auto& issue = result.detectedIssues[selectedIndex];
    if (issue.isInvalidRegion) return;

    cv::Rect r(issue.boundingBox.x, issue.boundingBox.y,
               issue.boundingBox.width, issue.boundingBox.height);
    r &= cv::Rect(0, 0, image.cols, image.rows);
    if (r.width < 2 || r.height < 2) return;

    cv::rectangle(image, r, cv::Scalar(255, 255, 0), 3);

    cv::Scalar labelColor;
    switch (issue.type) {
        case IssueType::LightingDifference:   labelColor = cv::Scalar(255, 150, 0); break;
        case IssueType::ShadowDifference:     labelColor = cv::Scalar(150, 80, 50); break;
        case IssueType::BloomDifference:      labelColor = cv::Scalar(255, 200, 0); break;
        case IssueType::ReflectionDifference: labelColor = cv::Scalar(50, 180, 255); break;
        case IssueType::TextureDifference:    labelColor = cv::Scalar(200, 50, 200); break;
        case IssueType::MaterialDifference:   labelColor = cv::Scalar(255, 130, 50); break;
        case IssueType::TransparencyDifference: labelColor = cv::Scalar(200, 200, 50); break;
        case IssueType::EdgeDifference:       labelColor = cv::Scalar(255, 130, 0); break;
        case IssueType::MissingGeometry:      labelColor = cv::Scalar(200, 0, 0); break;
        case IssueType::ExtraGeometry:        labelColor = cv::Scalar(255, 100, 0); break;
        case IssueType::MissingObject:        labelColor = cv::Scalar(255, 50, 50); break;
        case IssueType::MissingParticle:      labelColor = cv::Scalar(255, 150, 150); break;
        case IssueType::MissingUI:            labelColor = cv::Scalar(30, 130, 255); break;
        case IssueType::TextDifference:       labelColor = cv::Scalar(255, 200, 150); break;
        case IssueType::StereoOffset:         labelColor = cv::Scalar(255, 130, 130); break;
        case IssueType::DepthDisparityError:  labelColor = cv::Scalar(255, 0, 130); break;
        case IssueType::OcclusionDifference:  labelColor = cv::Scalar(130, 130, 130); break;
        case IssueType::PostProcessDifference: labelColor = cv::Scalar(200, 255, 75); break;
        case IssueType::TemporalDifference:   labelColor = cv::Scalar(200, 130, 255); break;
        case IssueType::LensBoundary:         labelColor = cv::Scalar(100, 100, 100); break;
        case IssueType::LowConfidence:        labelColor = cv::Scalar(180, 180, 180); break;
        default: labelColor = cv::Scalar(255, 255, 255); break;
    }

    char label[256];
    snprintf(label, sizeof(label), "%s (%.0f%%) [%d\u00d7%d]",
             IssueTypeName(issue.type), issue.confidence * 100.0f,
             issue.boundingBox.width, issue.boundingBox.height);

    int fontFace = cv::FONT_HERSHEY_DUPLEX;
    double fontScale = 0.65;
    int thickness = 2;
    int baseline;
    cv::Size ts = cv::getTextSize(label, fontFace, fontScale, thickness, &baseline);

    cv::Point lp(r.x, std::max(r.y - 10, ts.height + 8));
    cv::Rect bgRect(lp.x - 4, lp.y - ts.height - 4, ts.width + 8, ts.height + 8);
    bgRect &= cv::Rect(0, 0, image.cols, image.rows);

    cv::rectangle(image, bgRect, cv::Scalar(0, 0, 0), -1);
    cv::rectangle(image, bgRect, cv::Scalar(255, 255, 0), 1);

    cv::putText(image, label, cv::Point(lp.x + 2, lp.y - 2),
                fontFace, fontScale, labelColor, thickness, cv::LINE_AA);
}

cv::Mat Visualizer::renderDifferenceHeatmap(const cv::Mat& left, const cv::Mat& right,
                                             const cv::Mat& alignedDiff) const {
    cv::Mat diff;
    if (!alignedDiff.empty()) {
        diff = alignedDiff.clone();
    } else {
        cv::Mat leftResized, rightResized;
        if (left.size() != right.size()) {
            int w = std::min(left.cols, right.cols);
            int h = std::min(left.rows, right.rows);
            cv::resize(left, leftResized, cv::Size(w, h));
            cv::resize(right, rightResized, cv::Size(w, h));
        } else {
            leftResized = left;
            rightResized = right;
        }
        cv::absdiff(leftResized, rightResized, diff);
        if (diff.channels() == 3) {
            cv::cvtColor(diff, diff, cv::COLOR_BGR2GRAY);
        }
    }

    // Custom green → yellow → red colormap
    cv::Mat normalized;
    diff.convertTo(normalized, CV_32F, 1.0 / 255.0);
    cv::Mat heatmap(diff.size(), CV_8UC3, cv::Scalar(0, 0, 0));

    for (int y = 0; y < diff.rows; y++) {
        for (int x = 0; x < diff.cols; x++) {
            float v = normalized.at<float>(y, x);
            if (v < 0.08f) {
                // Green: matching
                heatmap.at<cv::Vec3b>(y, x) = cv::Vec3b(0, 180, 0);
            } else if (v < 0.3f) {
                // Yellow: moderate
                float t = (v - 0.08f) / 0.22f;
                heatmap.at<cv::Vec3b>(y, x) = cv::Vec3b(
                    (uchar)(t * 255),
                    (uchar)(180 * (1.0f - t * 0.5f)),
                    0);
            } else {
                // Red: large mismatch
                float t = std::min(1.0f, (v - 0.3f) / 0.7f);
                heatmap.at<cv::Vec3b>(y, x) = cv::Vec3b(
                    (uchar)(180 + 75 * t),
                    (uchar)(90 * (1.0f - t)),
                    0);
            }
        }
    }
    return heatmap;
}

cv::Mat Visualizer::renderStereoDifferenceOverlay(const cv::Mat& left, const cv::Mat& /*right*/,
                                                    const AnalysisResult& result) const {
    cv::Mat display;
    if (left.channels() == 1) cv::cvtColor(left, display, cv::COLOR_GRAY2BGR);
    else display = left.clone();

    for (const auto& issue : result.detectedIssues) {
        cv::Rect r(issue.boundingBox.x, issue.boundingBox.y,
                   issue.boundingBox.width, issue.boundingBox.height);
        r &= cv::Rect(0, 0, display.cols, display.rows);
        if (r.width < 2 || r.height < 2) continue;

        cv::Scalar color;
        switch (issue.type) {
            case IssueType::LightingDifference:   color = cv::Scalar(0, 150, 255); break;
            case IssueType::ShadowDifference:     color = cv::Scalar(50, 50, 150); break;
            case IssueType::BloomDifference:      color = cv::Scalar(0, 200, 255); break;
            case IssueType::ReflectionDifference: color = cv::Scalar(255, 180, 50); break;
            case IssueType::TextureDifference:    color = cv::Scalar(200, 0, 200); break;
            case IssueType::MaterialDifference:   color = cv::Scalar(50, 130, 255); break;
            case IssueType::TransparencyDifference: color = cv::Scalar(200, 200, 50); break;
            case IssueType::EdgeDifference:       color = cv::Scalar(130, 255, 0); break;
            case IssueType::MissingGeometry:      color = cv::Scalar(0, 0, 200); break;
            case IssueType::ExtraGeometry:        color = cv::Scalar(0, 100, 255); break;
            case IssueType::MissingObject:        color = cv::Scalar(0, 0, 255); break;
            case IssueType::MissingParticle:      color = cv::Scalar(100, 100, 255); break;
            case IssueType::MissingUI:            color = cv::Scalar(255, 130, 30); break;
            case IssueType::TextDifference:       color = cv::Scalar(200, 200, 255); break;
            case IssueType::StereoOffset:         color = cv::Scalar(200, 130, 130); break;
            case IssueType::DepthDisparityError:  color = cv::Scalar(130, 0, 255); break;
            case IssueType::OcclusionDifference:  color = cv::Scalar(130, 130, 130); break;
            case IssueType::PostProcessDifference: color = cv::Scalar(130, 255, 200); break;
            case IssueType::TemporalDifference:   color = cv::Scalar(255, 130, 200); break;
            case IssueType::LensBoundary:         color = cv::Scalar(100, 100, 100); break;
            case IssueType::LowConfidence:        color = cv::Scalar(180, 180, 180); break;
            default: color = cv::Scalar(0, 255, 255); break;
        }

        cv::rectangle(display, r, color, 2);

        char label[128];
        snprintf(label, sizeof(label), "%s (%.0f%%)",
                 IssueTypeName(issue.type), issue.confidence * 100.0f);

        int baseline;
        cv::Size ts = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.4, 1, &baseline);
        cv::Point lp(r.x, std::max(r.y - 4, ts.height + 2));
        cv::rectangle(display, cv::Rect(lp.x, lp.y - ts.height - 2, ts.width + 4, ts.height + 4),
                      cv::Scalar(0, 0, 0), -1);
        cv::putText(display, label, cv::Point(lp.x + 2, lp.y - 2),
                    cv::FONT_HERSHEY_SIMPLEX, 0.4, color, 1);
    }

    return display;
}

cv::Mat Visualizer::renderEdgeComparison(const cv::Mat& left, const cv::Mat& right) const {
    cv::Mat leftGray, rightGray;

    if (left.channels() == 3) {
        cv::cvtColor(left, leftGray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(right, rightGray, cv::COLOR_BGR2GRAY);
    } else {
        leftGray = left;
        rightGray = right;
    }

    cv::Mat edgesL, edgesR;
    cv::Canny(leftGray, edgesL, 50, 150);
    cv::Canny(rightGray, edgesR, 50, 150);

    cv::Mat composite;
    cv::cvtColor(leftGray, composite, cv::COLOR_GRAY2BGR);

    std::vector<cv::Mat> channels;
    cv::split(composite, channels);
    channels[1] = channels[1] | edgesR;
    channels[2] = channels[2] | edgesL;
    cv::merge(channels, composite);

    return composite;
}

cv::Mat Visualizer::renderFeatureMatch(const cv::Mat& left, const cv::Mat& right) const {
    cv::Mat leftGray, rightGray;

    if (left.channels() == 3) {
        cv::cvtColor(left, leftGray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(right, rightGray, cv::COLOR_BGR2GRAY);
    } else {
        leftGray = left;
        rightGray = right;
    }

    std::vector<cv::KeyPoint> kpL, kpR;
    cv::Mat descL, descR;
    FeatureCache::instance().getOrb(leftGray, rightGray, kpL, kpR, descL, descR, 500);

    cv::Mat output;
    if (!descL.empty() && !descR.empty()) {
        auto matcher = cv::DescriptorMatcher::create(cv::DescriptorMatcher::BRUTEFORCE_HAMMING);
        std::vector<cv::DMatch> matches;
        matcher->match(descL, descR, matches);

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

        cv::drawMatches(leftGray, kpL, rightGray, kpR, goodMatches, output,
                        cv::Scalar::all(-1), cv::Scalar::all(-1),
                        std::vector<char>(), cv::DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS);
    } else {
        output = cv::Mat(std::max(left.rows, right.rows), left.cols + right.cols, CV_8UC3, cv::Scalar(0, 0, 0));
    }

    return output;
}

cv::Mat Visualizer::renderHistogramView(const cv::Mat& left, const cv::Mat& right) const {
    cv::Mat leftGray, rightGray;

    if (left.channels() == 3) {
        cv::cvtColor(left, leftGray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(right, rightGray, cv::COLOR_BGR2GRAY);
    } else {
        leftGray = left;
        rightGray = right;
    }

    int histSize = 256;
    float range[] = {0, 256};
    const float* histRange = {range};

    cv::Mat histL, histR;
    cv::calcHist(&leftGray, 1, 0, cv::Mat(), histL, 1, &histSize, &histRange);
    cv::calcHist(&rightGray, 1, 0, cv::Mat(), histR, 1, &histSize, &histRange);

    int histW = 512, histH = 400;
    cv::Mat histImage(histH, histW, CV_8UC3, cv::Scalar(0, 0, 0));

    cv::normalize(histL, histL, 0, histImage.rows, cv::NORM_MINMAX);
    cv::normalize(histR, histR, 0, histImage.rows, cv::NORM_MINMAX);

    for (int i = 1; i < histSize; i++) {
        cv::line(histImage,
                 cv::Point((i - 1) * histW / histSize, histH - cvRound(histL.at<float>(i - 1))),
                 cv::Point(i * histW / histSize, histH - cvRound(histL.at<float>(i))),
                 cv::Scalar(255, 0, 0), 2);

        cv::line(histImage,
                 cv::Point((i - 1) * histW / histSize, histH - cvRound(histR.at<float>(i - 1))),
                 cv::Point(i * histW / histSize, histH - cvRound(histR.at<float>(i))),
                 cv::Scalar(0, 255, 0), 2);
    }

    return histImage;
}

cv::Mat Visualizer::renderBlurMap(const cv::Mat& left, const cv::Mat& right) const {
    cv::Mat leftGray, rightGray;

    if (left.channels() == 3) {
        cv::cvtColor(left, leftGray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(right, rightGray, cv::COLOR_BGR2GRAY);
    } else {
        leftGray = left;
        rightGray = right;
    }

    cv::Mat lapL, lapR;
    cv::Laplacian(leftGray, lapL, CV_32F);
    cv::Laplacian(rightGray, lapR, CV_32F);

    cv::Mat blurMapL, blurMapR;
    cv::convertScaleAbs(lapL, blurMapL);
    cv::convertScaleAbs(lapR, blurMapR);

    cv::Mat combined;
    cv::hconcat(blurMapL, blurMapR, combined);

    cv::Mat heatmap;
    cv::applyColorMap(combined, heatmap, cv::COLORMAP_INFERNO);
    return heatmap;
}

cv::Mat Visualizer::renderBlink(const cv::Mat& left, const cv::Mat& right, bool showLeft) const {
    if (showLeft) {
        cv::Mat display;
        if (left.channels() == 1) {
            cv::cvtColor(left, display, cv::COLOR_GRAY2BGR);
        } else {
            display = left.clone();
        }
        cv::putText(display, "LEFT EYE", cv::Point(30, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);
        return display;
    } else {
        cv::Mat display;
        if (right.channels() == 1) {
            cv::cvtColor(right, display, cv::COLOR_GRAY2BGR);
        } else {
            display = right.clone();
        }
        cv::putText(display, "RIGHT EYE", cv::Point(30, 30),
                    cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0, 255, 0), 2);
        return display;
    }
}

cv::Mat Visualizer::renderDisparityHeatmap(const AnalysisResult& result) const {
    const auto& dm = result.disparity;
    if (dm.disparityRange < 0.1) {
        cv::Mat blank(480, 640, CV_8UC3, cv::Scalar(20, 20, 30));
        cv::putText(blank, "No disparity data", cv::Point(200, 240),
                    cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(100, 100, 120), 2);
        return blank;
    }

    cv::Mat dispVis;
    if (dm.maxDisparity - dm.minDisparity > 0.1) {
        cv::Mat norm;
        dm.disparityMap.convertTo(norm, CV_8U, 255.0 / std::max(1.0, dm.maxDisparity - dm.minDisparity),
                                  -dm.minDisparity * 255.0 / std::max(1.0, dm.maxDisparity - dm.minDisparity));
        cv::applyColorMap(norm, dispVis, cv::COLORMAP_JET);
    } else {
        dispVis = cv::Mat(dm.disparityMap.size(), CV_8UC3, cv::Scalar(0, 0, 0));
    }

    // Apply valid mask: darken invalid pixels (vectorized)
    if (!dm.validMap.empty()) {
        cv::Mat invalidMask;
        cv::bitwise_not(dm.validMap, invalidMask);
        dispVis.setTo(cv::Scalar(40, 40, 50), invalidMask);
    }

    // Overlay stats
    char buf[256];
    snprintf(buf, sizeof(buf), "Disparity  Mean: %.1f  Range: %.1f  Invalid: %.1f%%",
             dm.meanDisparity, dm.disparityRange, dm.invalidRatio * 100.0);
    cv::putText(dispVis, buf, cv::Point(12, 28),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255, 255, 255), 1);

    return dispVis;
}
