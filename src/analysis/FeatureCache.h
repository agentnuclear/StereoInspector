#pragma once
#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>
#include <vector>

struct CachedFeatures {
    std::vector<cv::KeyPoint> leftKp;
    std::vector<cv::KeyPoint> rightKp;
    cv::Mat leftDesc;
    cv::Mat rightDesc;
    int64_t frameNumber = -1;
    int rows = 0;
    int cols = 0;
};

class FeatureCache {
public:
    static FeatureCache& instance();

    bool getOrb(const cv::Mat& leftGray, const cv::Mat& rightGray,
                std::vector<cv::KeyPoint>& leftKp,
                std::vector<cv::KeyPoint>& rightKp,
                cv::Mat& leftDesc, cv::Mat& rightDesc,
                int nfeatures = 1000);

    void newFrame(int64_t frameNumber, int rows, int cols);
    void reset();

private:
    FeatureCache() = default;
    ~FeatureCache() = default;
    FeatureCache(const FeatureCache&) = delete;
    FeatureCache& operator=(const FeatureCache&) = delete;

    cv::Ptr<cv::ORB> m_orb;
    CachedFeatures m_cache;
    int m_staleAge = 0;
};
