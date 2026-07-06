#pragma once
#include "core/Types.h"
#include <opencv2/core.hpp>
#include <vector>
#include <memory>

struct DisparityMap {
    cv::Mat disparity;          // Dense disparity map (float, same size as input)
    cv::Mat confidence;         // Per-pixel confidence (0-1)
    cv::Mat validMask;          // Binary mask of valid disparity pixels
    double minDisparity = 0.0;
    double maxDisparity = 0.0;
    double meanDisparity = 0.0;
    double medianDisparity = 0.0;
    double stdDisparity = 0.0;
    double invalidRatio = 0.0;
};

struct CorrespondenceResult {
    DisparityMap disparityMap;
    cv::Mat warpedRight;        // Right eye warped to left coordinate system
    cv::Mat warpedLeft;         // Left eye warped to right coordinate system
    cv::Mat occlusionMask;      // Pixels visible in one eye but not the other
    double matchQuality = 0.0;  // Overall correspondence quality (0-1)
    bool success = false;
};

class StereoCorrespondence {
public:
    enum class Method {
        StereoBM,           // Block Matching (fast, lower quality)
        StereoSGBM,         // Semi-Global Block Matching (better quality)
        OpticalFlow,        // Dense optical flow (RAFT-like via Farneback)
        FeatureBased        // Sparse feature matching + dense interpolation
    };

    struct Config {
        Method method = Method::StereoSGBM;
        int maxDisparity = 128;
        int blockSize = 9;
        int P1 = 8 * 3 * 9 * 9;   // SGBM P1
        int P2 = 32 * 3 * 9 * 9;  // SGBM P2
        int uniquenessRatio = 10;
        int speckleWindowSize = 100;
        int speckleRange = 32;
        int disp12MaxDiff = 1;
        int preFilterCap = 63;
        double lambda = 8000.0;    // For SGBM
        double sigmaColor = 1.2;   // For WLS filter
        bool useWLSFilter = true;  // Weighted Least Squares filter
        double downscaleFactor = 1.0; // Downscale for speed (0.5 = half res)
    };

    explicit StereoCorrespondence(const Config& config = Config());
    ~StereoCorrespondence();

    // Compute dense correspondence between left and right eyes
    CorrespondenceResult compute(const cv::Mat& left, const cv::Mat& right);

    // Warp an image using a disparity map
    static cv::Mat warpImage(const cv::Mat& image, const cv::Mat& disparity,
                              bool rightToLeft = true);

    // Compute disparity statistics
    static DisparityMap computeDisparityStats(const cv::Mat& disparity);

    // Get current config
    const Config& getConfig() const { return m_config; }
    void setConfig(const Config& config) { m_config = config; }

private:
    Config m_config;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};