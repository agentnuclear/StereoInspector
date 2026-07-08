#include "StereoCorrespondence.h"
#include "analysis/FeatureCache.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/video.hpp>
#include <spdlog/spdlog.h>

struct StereoCorrespondence::Impl {
    cv::Ptr<cv::StereoSGBM> sgbm;
    cv::Ptr<cv::StereoBM> bm;
};

StereoCorrespondence::StereoCorrespondence(const Config& config)
    : m_config(config), m_impl(std::make_unique<Impl>()) {

    m_impl->sgbm = cv::StereoSGBM::create(
        0, m_config.maxDisparity, m_config.blockSize,
        m_config.P1, m_config.P2,
        m_config.disp12MaxDiff, m_config.preFilterCap,
        m_config.uniquenessRatio, m_config.speckleWindowSize,
        m_config.speckleRange, cv::StereoSGBM::MODE_SGBM);

    m_impl->bm = cv::StereoBM::create(m_config.maxDisparity, m_config.blockSize);
    m_impl->bm->setPreFilterCap(m_config.preFilterCap);
    m_impl->bm->setUniquenessRatio(m_config.uniquenessRatio);
    m_impl->bm->setSpeckleWindowSize(m_config.speckleWindowSize);
    m_impl->bm->setSpeckleRange(m_config.speckleRange);
}

StereoCorrespondence::~StereoCorrespondence() = default;

CorrespondenceResult StereoCorrespondence::compute(const cv::Mat& left, const cv::Mat& right) {
    CorrespondenceResult result;
    result.success = false;

    if (left.empty() || right.empty() || left.size() != right.size()) {
        spdlog::warn("StereoCorrespondence: invalid input sizes");
        return result;
    }

    cv::Mat leftGray, rightGray;
    if (left.channels() == 3) {
        cv::cvtColor(left, leftGray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(right, rightGray, cv::COLOR_BGR2GRAY);
    } else {
        leftGray = left;
        rightGray = right;
    }

    cv::Mat dispFloat;

    if (m_config.method == Method::StereoSGBM) {
        cv::Mat leftResized, rightResized;
        float scale = (float)m_config.downscaleFactor;
        if (scale < 1.0f) {
            cv::resize(leftGray, leftResized, cv::Size(), scale, scale, cv::INTER_AREA);
            cv::resize(rightGray, rightResized, cv::Size(), scale, scale, cv::INTER_AREA);
        } else {
            leftResized = leftGray;
            rightResized = rightGray;
        }

        cv::Mat disp16S;
        m_impl->sgbm->compute(leftResized, rightResized, disp16S);

        // Scale back to original resolution
        if (scale < 1.0f) {
            cv::resize(disp16S, disp16S, leftGray.size(), 0, 0, cv::INTER_LINEAR);
        }

        disp16S.convertTo(dispFloat, CV_32F, 1.0 / 16.0);
    } else if (m_config.method == Method::StereoBM) {
        cv::Mat leftResized, rightResized;
        float scale = (float)m_config.downscaleFactor;
        if (scale < 1.0f) {
            cv::resize(leftGray, leftResized, cv::Size(), scale, scale, cv::INTER_AREA);
            cv::resize(rightGray, rightResized, cv::Size(), scale, scale, cv::INTER_AREA);
        } else {
            leftResized = leftGray;
            rightResized = rightGray;
        }

        cv::Mat disp16S;
        m_impl->bm->compute(leftResized, rightResized, disp16S);

        if (scale < 1.0f) {
            cv::resize(disp16S, disp16S, leftGray.size(), 0, 0, cv::INTER_LINEAR);
        }

        disp16S.convertTo(dispFloat, CV_32F, 1.0 / 16.0);
    } else if (m_config.method == Method::OpticalFlow) {
        // Farneback dense optical flow
        cv::Mat flow;
        cv::calcOpticalFlowFarneback(leftGray, rightGray, flow,
                                     0.5, 3, 15, 3, 5, 1.2, 0);
        // Extract horizontal disparity from flow
        dispFloat = cv::Mat(leftGray.size(), CV_32F, -1.0f);
        for (int y = 0; y < flow.rows; y++) {
            const cv::Vec2f* fPtr = flow.ptr<cv::Vec2f>(y);
            float* dPtr = dispFloat.ptr<float>(y);
            for (int x = 0; x < flow.cols; x++) {
                // Disparity is the horizontal flow component (left - right)
                // Positive means feature is to the right in right image (closer)
                float dx = fPtr[x][0];
                if (std::isfinite(dx)) dPtr[x] = dx;
            }
        }
    } else if (m_config.method == Method::FeatureBased) {
        // Sparse ORB features → dense disparity via interpolation
        std::vector<cv::KeyPoint> kpL, kpR;
        cv::Mat descL, descR;
        FeatureCache::instance().getOrb(leftGray, rightGray, kpL, kpR, descL, descR, 1000);
        dispFloat = cv::Mat(leftGray.size(), CV_32F, -1.0f);

        if (!descL.empty() && !descR.empty() && kpL.size() >= 5 && kpR.size() >= 5) {
            auto matcher = cv::DescriptorMatcher::create(cv::DescriptorMatcher::BRUTEFORCE_HAMMING);
            std::vector<cv::DMatch> matches;
            matcher->match(descL, descR, matches);

            if (matches.size() >= 4) {
                // Build sparse disparity points
                std::vector<cv::Point2f> srcPts, dstPts;
                std::vector<float> disparities;
                for (const auto& m : matches) {
                    float d = kpL[m.queryIdx].pt.x - kpR[m.trainIdx].pt.x;
                    if (std::abs(d) < 500) { // sanity filter
                        srcPts.push_back(kpL[m.queryIdx].pt);
                        dstPts.push_back(cv::Point2f(d, 0));
                        disparities.push_back(d);
                    }
                }
                if (srcPts.size() >= 3) {
                    // Inverse Distance Weighting interpolation (full resolution)
                    double sigma2 = 400.0; // falloff squared radius
                    for (int y = 0; y < dispFloat.rows; y++) {
                        float* dPtr = dispFloat.ptr<float>(y);
                        for (int x = 0; x < dispFloat.cols; x++) {
                            double weightSum = 0, dispSum = 0;
                            for (size_t i = 0; i < srcPts.size(); i++) {
                                double dx2 = x - srcPts[i].x;
                                double dy2 = y - srcPts[i].y;
                                double dist2 = dx2*dx2 + dy2*dy2;
                                double w = std::exp(-dist2 / (2.0 * sigma2));
                                weightSum += w;
                                dispSum += w * disparities[i];
                            }
                            if (weightSum > 1e-6) dPtr[x] = (float)(dispSum / weightSum);
                        }
                    }
                }
            }
        }
    }

    if (dispFloat.empty()) {
        spdlog::warn("Disparity computation returned empty map");
        return result;
    }

    // Compute disparity statistics
    result.disparityMap = computeDisparityStats(dispFloat);

    // Warp right to left using disparity
    result.warpedRight = warpImage(right, dispFloat, true);
    result.warpedLeft = warpImage(left, -dispFloat, false);

    // Occlusion mask: LR inconsistent pixels + high warping error
    result.occlusionMask = cv::Mat::zeros(leftGray.size(), CV_8UC1);
    // Start with LR-inconsistent pixels from SGBM (disparity < 0 = failed disp12MaxDiff)
    if (!result.disparityMap.validMask.empty()) {
        cv::bitwise_not(result.disparityMap.validMask, result.occlusionMask);
    }
    // Also include large warping inconsistencies (high threshold to catch only
    // genuine occlusion / depth-boundary artifacts, not real stereo differences)
    if (!result.warpedRight.empty()) {
        cv::Mat wLeft, wRight;
        if (left.channels() == 3) {
            cv::cvtColor(left, wLeft, cv::COLOR_BGR2GRAY);
            cv::cvtColor(result.warpedRight, wRight, cv::COLOR_BGR2GRAY);
        } else {
            wLeft = leftGray;
            wRight = result.warpedRight;
        }
        cv::Mat diff;
        cv::absdiff(wLeft, wRight, diff);
        cv::Mat warpOcc;
        cv::threshold(diff, warpOcc, 60, 255, cv::THRESH_BINARY);
        cv::bitwise_or(result.occlusionMask, warpOcc, result.occlusionMask);
    }

    // Match quality: valid disparity ratio
    int totalPixels = (int)dispFloat.total();
    int validPixels = cv::countNonZero(result.disparityMap.validMask);
    result.matchQuality = totalPixels > 0 ? (float)validPixels / (float)totalPixels : 0.0f;
    result.success = result.matchQuality > 0.1f;

    return result;
}

cv::Mat StereoCorrespondence::warpImage(const cv::Mat& image, const cv::Mat& disparity,
                                         bool rightToLeft) {
    if (image.empty() || disparity.empty()) return cv::Mat();

    cv::Mat dispResized;
    if (image.size() != disparity.size()) {
        cv::resize(disparity, dispResized, image.size());
    } else {
        dispResized = disparity;
    }

    cv::Mat mapX(image.size(), CV_32FC1);
    cv::Mat mapY(image.size(), CV_32FC1);

    float sign = rightToLeft ? -1.0f : 1.0f;
    for (int y = 0; y < image.rows; y++) {
        const float* dPtr = dispResized.ptr<float>(y);
        float* mxPtr = mapX.ptr<float>(y);
        float* myPtr = mapY.ptr<float>(y);
        for (int x = 0; x < image.cols; x++) {
            mxPtr[x] = (float)x + dPtr[x] * sign;
            myPtr[x] = (float)y;
        }
    }

    cv::Mat warped;
    cv::remap(image, warped, mapX, mapY, cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
    return warped;
}

DisparityMap StereoCorrespondence::computeDisparityStats(const cv::Mat& disparity) {
    DisparityMap stats;
    if (disparity.empty()) return stats;

    stats.validMask = cv::Mat::zeros(disparity.size(), CV_8UC1);
    std::vector<double> validDisps;
    validDisps.reserve(disparity.total());

    double sum = 0.0, sumSq = 0.0;
    int count = 0;

    for (int y = 0; y < disparity.rows; y++) {
        const float* dPtr = disparity.ptr<float>(y);
        uchar* vPtr = stats.validMask.ptr<uchar>(y);
        for (int x = 0; x < disparity.cols; x++) {
            float d = dPtr[x];
            if (std::isfinite(d) && d >= 0.0f) {
                vPtr[x] = 255;
                validDisps.push_back(d);
                sum += d;
                sumSq += d * d;
                count++;
            }
        }
    }

    stats.disparity = disparity.clone();
    stats.minDisparity = validDisps.empty() ? 0.0
        : *std::min_element(validDisps.begin(), validDisps.end());
    stats.maxDisparity = validDisps.empty() ? 0.0
        : *std::max_element(validDisps.begin(), validDisps.end());
    stats.meanDisparity = count > 0 ? sum / count : 0.0;
    stats.stdDisparity = count > 0
        ? std::sqrt(sumSq / count - stats.meanDisparity * stats.meanDisparity) : 0.0;

    if (!validDisps.empty()) {
        size_t mid = validDisps.size() / 2;
        std::nth_element(validDisps.begin(), validDisps.begin() + mid, validDisps.end());
        stats.medianDisparity = validDisps[mid];
    }

    double totalPixels = (double)(disparity.rows * disparity.cols);
    stats.invalidRatio = totalPixels > 0 ? 1.0 - (double)count / totalPixels : 1.0;

    return stats;
}
