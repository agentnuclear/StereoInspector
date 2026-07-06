#include "VrLensAnalyzer.h"
#include <opencv2/imgproc.hpp>
#include <cmath>

VrLensAnalyzer::VrLensAnalyzer() : BaseAnalyzer("VRLens", true) {}

double VrLensAnalyzer::estimateLensDistortion(const cv::Mat& eye) {
    // VR headsets apply barrel distortion pre-warp.
    // We detect this by measuring radial edge density variation.
    cv::Mat gray;
    if (eye.channels() == 3) cv::cvtColor(eye, gray, cv::COLOR_BGR2GRAY);
    else gray = eye;

    cv::Mat edges;
    cv::Canny(gray, edges, 30, 100);

    int cx = gray.cols / 2;
    int cy = gray.rows / 2;
    int maxR = std::min(cx, cy);

    // Sample edge density in concentric rings
    std::vector<double> ringDensity(maxR, 0.0);
    std::vector<int> ringCount(maxR, 0);

    for (int y = 0; y < edges.rows; y++) {
        for (int x = 0; x < edges.cols; x++) {
            if (edges.at<uchar>(y, x)) {
                int dx = x - cx;
                int dy = y - cy;
                int r = (int)std::sqrt((double)(dx * dx + dy * dy));
                if (r < maxR) {
                    ringDensity[r] += 1.0;
                    ringCount[r]++;
                }
            }
        }
    }

    // Normalize by ring area (2*pi*r)
    double totalDensity = 0.0;
    int validRings = 0;
    for (int r = 5; r < maxR - 5; r++) {
        if (ringCount[r] > 0) {
            double area = 2.0 * 3.14159 * r;
            ringDensity[r] /= area;
            totalDensity += ringDensity[r];
            validRings++;
        }
    }

    if (validRings == 0) return 0.0;
    double avgDensity = totalDensity / validRings;

    // Measure radial deviation from average
    double deviation = 0.0;
    for (int r = 5; r < maxR - 5; r++) {
        deviation += std::abs(ringDensity[r] - avgDensity);
    }
    deviation /= validRings;
    deviation /= (avgDensity + 0.001); // normalize

    return std::min(1.0, deviation);
}

double VrLensAnalyzer::estimateFoveation(const cv::Mat& eye) {
    // Foveated rendering: sharp center, blurry periphery.
    // Measure Laplacian variance in center vs edges.
    cv::Mat gray;
    if (eye.channels() == 3) cv::cvtColor(eye, gray, cv::COLOR_BGR2GRAY);
    else gray = eye;

    int cx = gray.cols / 2;
    int cy = gray.rows / 2;
    int innerR = std::min(gray.cols, gray.rows) / 4;
    int outerR = std::min(gray.cols, gray.rows) / 2;

    cv::Mat lap;
    cv::Laplacian(gray, lap, CV_64F);

    auto regionVariance = [&](int centerX, int centerY, int radius) -> double {
        cv::Mat mask = cv::Mat::zeros(gray.size(), CV_8U);
        cv::circle(mask, cv::Point(centerX, centerY), radius, cv::Scalar(255), -1);
        cv::Scalar m, s;
        cv::meanStdDev(lap, m, s, mask);
        return s[0] * s[0];
    };

    double centerVar = regionVariance(cx, cy, innerR);
    double outerVar = regionVariance(cx, cy, outerR);
    double periphVar = outerVar - centerVar;
    if (periphVar < 0) periphVar = 0;
    if (outerVar > 0) {
        // Normalize: higher = more foveation (center sharper than periphery)
        return std::min(1.0, (centerVar - periphVar) / (centerVar + 0.001));
    }
    return 0.0;
}

double VrLensAnalyzer::estimateGodRays(const cv::Mat& eye) {
    // God rays / glare: bright regions with radial streaks.
    // Detect by thresholding bright areas and measuring radial spread.
    cv::Mat gray;
    if (eye.channels() == 3) cv::cvtColor(eye, gray, cv::COLOR_BGR2GRAY);
    else gray = eye;

    cv::Mat bright;
    cv::threshold(gray, bright, 220, 255, cv::THRESH_BINARY);

    double brightRatio = (double)cv::countNonZero(bright) / (double)bright.total();

    // Dilate to capture glow
    cv::Mat dilated;
    cv::dilate(bright, dilated, cv::Mat(), cv::Point(-1, -1), 3);
    cv::Mat glowOnly;
    cv::subtract(dilated, bright, glowOnly);

    double glowRatio = (double)cv::countNonZero(glowOnly) / (double)gray.total();

    // God rays = excessive glow relative to bright area
    if (brightRatio > 0.001) {
        return std::min(1.0, glowRatio / brightRatio);
    }
    return 0.0;
}

void VrLensAnalyzer::analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) {
    double distL = estimateLensDistortion(leftEye);
    double distR = estimateLensDistortion(rightEye);
    result.lensDistortionDelta = std::abs(distL - distR);

    double fovL = estimateFoveation(leftEye);
    double fovR = estimateFoveation(rightEye);
    result.foveationAsymmetry = std::abs(fovL - fovR);

    double godL = estimateGodRays(leftEye);
    double godR = estimateGodRays(rightEye);
    result.godRayDifference = std::abs(godL - godR);
}
