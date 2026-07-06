#include "FeatureCache.h"
#include <opencv2/imgproc.hpp>

FeatureCache& FeatureCache::instance() {
    static FeatureCache cache;
    return cache;
}

void FeatureCache::newFrame(int64_t frameNumber, int rows, int cols) {
    if (m_cache.frameNumber != frameNumber || m_cache.rows != rows || m_cache.cols != cols) {
        m_staleAge++;
    }
    if (m_staleAge > 5 || m_cache.rows != rows || m_cache.cols != cols) {
        m_cache = CachedFeatures{};
        m_staleAge = 0;
    }
}

void FeatureCache::reset() {
    m_cache = CachedFeatures{};
    m_staleAge = 0;
    m_orb = nullptr;
}

bool FeatureCache::getOrb(const cv::Mat& leftGray, const cv::Mat& rightGray,
                           std::vector<cv::KeyPoint>& leftKp,
                           std::vector<cv::KeyPoint>& rightKp,
                           cv::Mat& leftDesc, cv::Mat& rightDesc,
                           int nfeatures) {
    if (!m_cache.leftDesc.empty() && m_cache.rows == leftGray.rows && m_cache.cols == leftGray.cols) {
        leftKp = m_cache.leftKp;
        rightKp = m_cache.rightKp;
        leftDesc = m_cache.leftDesc.clone();
        rightDesc = m_cache.rightDesc.clone();
        return true; // cache hit
    }

    if (!m_orb) {
        m_orb = cv::ORB::create(nfeatures);
    } else {
        m_orb->setMaxFeatures(nfeatures);
    }

    m_orb->detectAndCompute(leftGray, cv::Mat(), m_cache.leftKp, m_cache.leftDesc);
    m_orb->detectAndCompute(rightGray, cv::Mat(), m_cache.rightKp, m_cache.rightDesc);
    m_cache.rows = leftGray.rows;
    m_cache.cols = leftGray.cols;

    leftKp = m_cache.leftKp;
    rightKp = m_cache.rightKp;
    leftDesc = m_cache.leftDesc.clone();
    rightDesc = m_cache.rightDesc.clone();
    m_staleAge = 0;
    return false;
}
