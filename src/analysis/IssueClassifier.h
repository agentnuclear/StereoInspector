#pragma once
#include "core/Types.h"
#include <opencv2/core.hpp>

struct RegionFeatures {
    float brightnessDiff = 0.0f;
    float contrastDiff = 0.0f;
    float meanLuminanceL = 0.0f;
    float meanLuminanceR = 0.0f;
    float stdLuminanceL = 0.0f;
    float stdLuminanceR = 0.0f;
    float colorDiff = 0.0f;
    float edgeSimilarity = 1.0f;
    float textureSimilarity = 1.0f;
    float gradientConsistency = 1.0f;
    float histogramSimilarity = 1.0f;
    float featureMatchDensity = 0.0f;
    float disparityConsistency = 1.0f;
    float regionSize = 0.0f;
    float regionAspectRatio = 1.0f;
    float regionPosX = 0.0f;
    float regionPosY = 0.0f;
    float temporalStability = 1.0f;
    float meanDiff = 0.0f;
    float bloomRatio = 0.0f;
    float shadowRatio = 0.0f;
    float edgeRatio = 0.0f;
    float leftContentDensity = 0.0f;
    float rightContentDensity = 0.0f;
    bool hasColor = false;
    bool nearBorder = false;
    bool nearCenter = false;
};

class IssueClassifier {
public:
    IssueClassifier();
    ~IssueClassifier() = default;

    ClassificationEvidence extractEvidence(const cv::Mat& leftRegion, const cv::Mat& rightRegion,
                                            const cv::Mat& leftGray, const cv::Mat& rightGray,
                                            const cv::Mat& diffMap, const cv::Rect& box,
                                            int frameW, int frameH);

    DetectedIssue classify(const RegionFeatures& features);

    void setConfidenceThreshold(float t) { m_confidenceThreshold = t; }
    float confidenceThreshold() const { return m_confidenceThreshold; }

private:
    float m_confidenceThreshold = 0.70f;

    float scoreLighting(const RegionFeatures& f);
    float scoreShadow(const RegionFeatures& f);
    float scoreBloom(const RegionFeatures& f);
    float scoreReflection(const RegionFeatures& f);
    float scoreTexture(const RegionFeatures& f);
    float scoreMaterial(const RegionFeatures& f);
    float scoreTransparency(const RegionFeatures& f);
    float scoreEdge(const RegionFeatures& f);
    float scoreMissingGeometry(const RegionFeatures& f);
    float scoreExtraGeometry(const RegionFeatures& f);
    float scoreMissingObject(const RegionFeatures& f);
    float scoreMissingParticle(const RegionFeatures& f);
    float scoreMissingUI(const RegionFeatures& f);
    float scoreText(const RegionFeatures& f);
    float scoreStereoOffset(const RegionFeatures& f);
    float scoreDepthDisparity(const RegionFeatures& f);
    float scoreOcclusion(const RegionFeatures& f);
    float scorePostProcess(const RegionFeatures& f);
    float scoreTemporal(const RegionFeatures& f);
    float scoreLensBoundary(const RegionFeatures& f);

    IssueType selectBest(const std::vector<std::pair<IssueType, float>>& scores,
                          std::vector<ClassificationCandidate>& alternatives);
};
