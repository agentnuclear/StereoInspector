#include "IssueClassifier.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>
#include <algorithm>
#include <cmath>

IssueClassifier::IssueClassifier() = default;

ClassificationEvidence IssueClassifier::extractEvidence(
    const cv::Mat& leftRegion, const cv::Mat& rightRegion,
    const cv::Mat& leftGray, const cv::Mat& rightGray,
    const cv::Mat& diffMap, const cv::Rect& box,
    int frameW, int frameH)
{
    ClassificationEvidence ev;
    if (leftRegion.empty() || rightRegion.empty()) return ev;
    if (leftGray.empty() || rightGray.empty()) return ev;
    if (leftGray.cols < 5 || leftGray.rows < 5) return ev;

    double totalPix = (double)leftRegion.total();
    if (totalPix < 1.0) return ev;

    // Brightness / luminance
    cv::Scalar meanL = cv::mean(leftGray);
    cv::Scalar meanR = cv::mean(rightGray);
    cv::Scalar stdL, stdR;
    cv::meanStdDev(leftGray, meanL, stdL);
    cv::meanStdDev(rightGray, meanR, stdR);

    ev.brightnessDiff = (float)std::abs(meanL[0] - meanR[0]) / 255.0f;
    ev.contrastDiff = (float)std::abs(stdL[0] - stdR[0]) / 255.0f;

    // Color difference (if color)
    if (leftRegion.channels() == 3 && rightRegion.channels() == 3) {
        ev.hasColor = true;
        std::vector<cv::Mat> chL(3), chR(3);
        cv::split(leftRegion, chL);
        cv::split(rightRegion, chR);
        double dr = std::abs(cv::mean(chL[2])[0] - cv::mean(chR[2])[0]) / 255.0;
        double dg = std::abs(cv::mean(chL[1])[0] - cv::mean(chR[1])[0]) / 255.0;
        double db = std::abs(cv::mean(chL[0])[0] - cv::mean(chR[0])[0]) / 255.0;
        ev.colorDiff = (float)((dr + dg + db) / 3.0);
    }

    // Edge similarity (Canny)
    cv::Mat edgesL, edgesR;
    cv::Canny(leftGray, edgesL, 50.0, 150.0);
    cv::Canny(rightGray, edgesR, 50.0, 150.0);
    cv::Mat edgeIntersection, edgeUnion;
    cv::bitwise_and(edgesL, edgesR, edgeIntersection);
    cv::bitwise_or(edgesL, edgesR, edgeUnion);
    double edgeCount = (double)cv::countNonZero(edgeUnion);
    ev.edgeSimilarity = edgeCount > 0
        ? (float)((double)cv::countNonZero(edgeIntersection) / edgeCount) : 1.0f;
    double edgeLD = (double)cv::countNonZero(edgesL) / totalPix;
    double edgeRD = (double)cv::countNonZero(edgesR) / totalPix;
    ev.edgeRatio = (float)std::abs(edgeLD - edgeRD);

    // Texture similarity (Laplacian variance)
    cv::Mat lapL, lapR;
    cv::Laplacian(leftGray, lapL, CV_32F);
    cv::Laplacian(rightGray, lapR, CV_32F);
    cv::Scalar mL, sL, mR, sR;
    cv::meanStdDev(lapL, mL, sL);
    cv::meanStdDev(lapR, mR, sR);
    float varL = (float)(sL[0] * sL[0]);
    float varR = (float)(sR[0] * sR[0]);
    float maxVar = std::max(varL, varR);
    ev.textureSimilarity = maxVar > 0.01f ? 1.0f - std::abs(varL - varR) / maxVar : 1.0f;

    // Gradient consistency (Sobel orientation)
    cv::Mat gradXL, gradXR, gradYL, gradYR;
    cv::Sobel(leftGray, gradXL, CV_32F, 1, 0, 3);
    cv::Sobel(leftGray, gradYL, CV_32F, 0, 1, 3);
    cv::Sobel(rightGray, gradXR, CV_32F, 1, 0, 3);
    cv::Sobel(rightGray, gradYR, CV_32F, 0, 1, 3);
    cv::Mat angleL, angleR, angleDiff;
    cv::phase(gradXL, gradYL, angleL);
    cv::phase(gradXR, gradYR, angleR);
    cv::absdiff(angleL, angleR, angleDiff);
    cv::Scalar meanAngleDiff = cv::mean(angleDiff, edgesL | edgesR);
    float gc = 1.0f - (float)(meanAngleDiff[0] / CV_PI);
    ev.gradientConsistency = std::isnan(gc) ? 1.0f : std::clamp(gc, 0.0f, 1.0f);

    // Histogram similarity
    int histSize = 32;
    float range[] = {0, 256};
    const float* histRange = {range};
    cv::Mat histL, histR;
    cv::calcHist(&leftGray, 1, 0, cv::Mat(), histL, 1, &histSize, &histRange);
    cv::calcHist(&rightGray, 1, 0, cv::Mat(), histR, 1, &histSize, &histRange);
    cv::normalize(histL, histL, 1.0, 0, cv::NORM_L1);
    cv::normalize(histR, histR, 1.0, 0, cv::NORM_L1);
    ev.histogramSimilarity = (float)cv::compareHist(histL, histR, cv::HISTCMP_CORREL);
    if (std::isnan(ev.histogramSimilarity) || ev.histogramSimilarity < 0) ev.histogramSimilarity = 0;

    // Mean diff from difference map
    if (!diffMap.empty()) {
        cv::Mat regionDiff = diffMap(box);
        ev.meanDiff = (float)cv::mean(regionDiff)[0] / 255.0f;
    }

    // Bloom ratio (bright pixels > 200)
    cv::Mat brightL, brightR;
    cv::threshold(leftGray, brightL, 200, 255, cv::THRESH_BINARY);
    cv::threshold(rightGray, brightR, 200, 255, cv::THRESH_BINARY);
    double bL = (double)cv::countNonZero(brightL) / totalPix;
    double bR = (double)cv::countNonZero(brightR) / totalPix;
    ev.bloomRatio = (float)std::abs(bL - bR);

    // Shadow ratio (dark pixels < 50)
    cv::Mat darkL, darkR;
    cv::threshold(leftGray, darkL, 50, 255, cv::THRESH_BINARY_INV);
    cv::threshold(rightGray, darkR, 50, 255, cv::THRESH_BINARY_INV);
    double shL = (double)cv::countNonZero(darkL) / totalPix;
    double shR = (double)cv::countNonZero(darkR) / totalPix;
    ev.shadowRatio = (float)std::abs(shL - shR);

    // Content density (pixels > 30 threshold)
    double cL = (double)cv::countNonZero(leftGray > 30) / totalPix;
    double cR = (double)cv::countNonZero(rightGray > 30) / totalPix;
    ev.leftContentDensity = (float)cL;
    ev.rightContentDensity = (float)cR;

    // Region properties
    ev.regionSize = (float)(box.width * box.height);
    ev.regionAspectRatio = box.height > 0 ? (float)box.width / (float)box.height : 1.0f;
    ev.regionPosX = (float)box.x / (float)std::max(frameW, 1);
    ev.regionPosY = (float)box.y / (float)std::max(frameH, 1);

    // Near border check (within 5% of edge)
    float margin = 0.05f;
    ev.nearBorder = ev.regionPosX < margin || ev.regionPosX > (1.0f - margin) ||
                    ev.regionPosY < margin || ev.regionPosY > (1.0f - margin);
    ev.nearCenter = std::abs(ev.regionPosX - 0.5f) < 0.15f &&
                    std::abs(ev.regionPosY - 0.5f) < 0.15f;

    // Disparity consistency (default 1.0, set externally)
    // Feature match density (set externally from ORB)

    return ev;
}

// ----- Scoring functions -----
// Each returns a score 0.0 - 1.0 for how well the evidence matches that type

float IssueClassifier::scoreLighting(const RegionFeatures& f) {
    float s = f.brightnessDiff * 1.5f;
    s += f.contrastDiff * 0.5f;
    if (f.edgeSimilarity > 0.7f) s += 0.2f;
    if (f.textureSimilarity > 0.7f) s += 0.15f;
    if (f.histogramSimilarity < 0.6f) s += 0.15f;
    if (f.bloomRatio < 0.1f && f.shadowRatio < 0.1f) s += 0.2f;
    s -= f.edgeRatio * 0.5f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scoreShadow(const RegionFeatures& f) {
    float s = f.shadowRatio * 2.0f;
    s += f.brightnessDiff * 0.5f;
    if (f.shadowRatio > f.bloomRatio) s += 0.2f;
    if (f.meanLuminanceL < 0.3f || f.meanLuminanceR < 0.3f) s += 0.15f;
    if (f.textureSimilarity > 0.6f) s += 0.1f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scoreBloom(const RegionFeatures& f) {
    float s = f.bloomRatio * 2.0f;
    s += f.brightnessDiff * 0.3f;
    if (f.bloomRatio > f.shadowRatio) s += 0.2f;
    if (f.meanLuminanceL > 0.7f || f.meanLuminanceR > 0.7f) s += 0.15f;
    if (f.edgeSimilarity > 0.6f) s += 0.1f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scoreReflection(const RegionFeatures& f) {
    float s = f.colorDiff * 0.8f + f.brightnessDiff * 0.4f;
    s += (1.0f - f.edgeSimilarity) * 0.6f;
    s += (1.0f - f.gradientConsistency) * 0.4f;
    if (f.histogramSimilarity > 0.5f && f.histogramSimilarity < 0.85f) s += 0.2f;
    if (f.textureSimilarity > 0.5f) s += 0.1f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scoreTexture(const RegionFeatures& f) {
    float s = (1.0f - f.textureSimilarity) * 1.5f;
    s += (1.0f - f.gradientConsistency) * 0.3f;
    if (f.brightnessDiff < 0.1f) s += 0.15f;
    if (f.edgeSimilarity > 0.5f && f.edgeSimilarity < 0.85f) s += 0.1f;
    if (f.edgeRatio < 0.1f) s += 0.1f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scoreMaterial(const RegionFeatures& f) {
    float s = (1.0f - f.textureSimilarity) * 0.8f;
    s += std::abs(f.contrastDiff) * 0.6f;
    s += (1.0f - f.histogramSimilarity) * 0.4f;
    if (f.edgeSimilarity > 0.4f && f.edgeSimilarity < 0.8f) s += 0.15f;
    if (f.colorDiff > 0.05f) s += 0.1f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scoreTransparency(const RegionFeatures& f) {
    float s = f.colorDiff * 0.5f + f.brightnessDiff * 0.3f;
    s += (1.0f - f.edgeSimilarity) * 0.4f;
    if (f.edgeRatio < 0.15f) s += 0.2f;
    if (f.histogramSimilarity > 0.6f && f.histogramSimilarity < 0.9f) s += 0.15f;
    if (f.textureSimilarity > 0.4f && f.textureSimilarity < 0.8f) s += 0.1f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scoreEdge(const RegionFeatures& f) {
    float s = f.edgeRatio * 1.5f;
    s += (1.0f - f.edgeSimilarity) * 0.5f;
    if (f.brightnessDiff < 0.05f && f.colorDiff < 0.05f) s += 0.2f;
    if (f.textureSimilarity > 0.7f) s += 0.1f;
    s -= f.meanDiff * 0.5f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scoreMissingGeometry(const RegionFeatures& f) {
    float s = (1.0f - f.edgeSimilarity) * 0.6f;
    s += f.edgeRatio * 0.8f;
    s += std::abs(f.leftContentDensity - f.rightContentDensity) * 0.5f;
    if (f.edgeRatio > 0.3f) s += 0.2f;
    if (f.brightnessDiff > 0.1f) s += 0.1f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scoreExtraGeometry(const RegionFeatures& f) {
    float s = (1.0f - f.edgeSimilarity) * 0.5f;
    s += f.edgeRatio * 0.6f;
    s += std::abs(f.leftContentDensity - f.rightContentDensity) * 0.4f;
    if (f.edgeRatio > 0.3f && f.leftContentDensity > f.rightContentDensity) s += 0.2f;
    if (f.colorDiff < 0.05f) s += 0.1f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scoreMissingObject(const RegionFeatures& f) {
    float s = std::abs(f.leftContentDensity - f.rightContentDensity) * 1.5f;
    s += (1.0f - f.edgeSimilarity) * 0.4f;
    s += (1.0f - f.histogramSimilarity) * 0.3f;
    if (f.edgeRatio > 0.4f) s += 0.2f;
    if (f.brightnessDiff > 0.2f) s += 0.1f;
    if (f.textureSimilarity < 0.3f) s += 0.1f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scoreMissingParticle(const RegionFeatures& f) {
    float s = std::abs(f.leftContentDensity - f.rightContentDensity) * 0.8f;
    s += (1.0f - f.edgeSimilarity) * 0.3f;
    if (f.regionSize < 2000.0f) s += 0.3f;
    if (f.regionAspectRatio > 0.5f && f.regionAspectRatio < 2.0f) s += 0.15f;
    if (f.meanDiff > 0.1f && f.meanDiff < 0.4f) s += 0.1f;
    if (f.brightnessDiff > 0.1f) s += 0.1f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scoreMissingUI(const RegionFeatures& f) {
    float s = std::abs(f.leftContentDensity - f.rightContentDensity) * 0.6f;
    s += (1.0f - f.edgeSimilarity) * 0.3f;
    if (f.nearBorder) s += 0.2f;
    if (f.regionAspectRatio < 0.3f || f.regionAspectRatio > 3.0f) s += 0.15f;
    if (f.edgeRatio > 0.2f && f.edgeRatio < 0.6f) s += 0.1f;
    if (f.colorDiff > 0.05f) s += 0.1f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scoreText(const RegionFeatures& f) {
    float s = (1.0f - f.edgeSimilarity) * 0.4f;
    s += f.edgeRatio * 0.5f;
    if (f.regionSize < 5000.0f) s += 0.2f;
    if (f.regionAspectRatio > 1.5f || f.regionAspectRatio < 0.5f) s += 0.15f;
    if (f.brightnessDiff > 0.1f && f.colorDiff < 0.05f) s += 0.1f;
    if (f.contrastDiff > 0.1f) s += 0.1f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scoreStereoOffset(const RegionFeatures& f) {
    float s = f.edgeRatio * 0.5f;
    s += (1.0f - f.gradientConsistency) * 0.4f;
    if (f.brightnessDiff < 0.03f && f.colorDiff < 0.03f) s += 0.25f;
    if (f.textureSimilarity > 0.8f) s += 0.15f;
    if (f.edgeSimilarity > 0.7f) s += 0.1f;
    s -= f.meanDiff * 0.3f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scoreDepthDisparity(const RegionFeatures& f) {
    float s = (1.0f - f.disparityConsistency) * 1.5f;
    s += (1.0f - f.gradientConsistency) * 0.3f;
    if (f.edgeSimilarity > 0.7f) s += 0.15f;
    if (f.textureSimilarity > 0.7f) s += 0.1f;
    if (f.brightnessDiff < 0.05f) s += 0.1f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scoreOcclusion(const RegionFeatures& f) {
    float s = (1.0f - f.edgeSimilarity) * 0.5f;
    s += f.edgeRatio * 0.4f;
    if (f.nearBorder) s += 0.2f;
    if (f.disparityConsistency < 0.5f) s += 0.2f;
    if (f.brightnessDiff > 0.05f && f.brightnessDiff < 0.3f) s += 0.1f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scorePostProcess(const RegionFeatures& f) {
    float s = f.colorDiff * 0.6f + f.brightnessDiff * 0.3f;
    s += (1.0f - f.histogramSimilarity) * 0.4f;
    if (f.edgeSimilarity > 0.8f) s += 0.2f;
    if (f.textureSimilarity > 0.8f) s += 0.15f;
    if (f.gradientConsistency > 0.8f) s += 0.1f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scoreTemporal(const RegionFeatures& f) {
    float s = (1.0f - f.temporalStability) * 1.5f;
    if (f.brightnessDiff < 0.03f && f.colorDiff < 0.03f) s += 0.2f;
    if (f.meanDiff < 0.05f) s += 0.15f;
    if (f.edgeSimilarity > 0.8f) s += 0.1f;
    return std::min(1.0f, std::max(0.0f, s));
}

float IssueClassifier::scoreLensBoundary(const RegionFeatures& f) {
    float s = 0.0f;
    if (f.nearBorder) s += 0.4f;
    if (f.regionAspectRatio > 3.0f || f.regionAspectRatio < 0.2f) s += 0.2f;
    if (f.brightnessDiff < 0.05f && f.colorDiff < 0.05f) s += 0.2f;
    if (f.regionPosY < 0.1f || f.regionPosY > 0.9f) s += 0.15f;
    return std::min(1.0f, std::max(0.0f, s));
}

IssueType IssueClassifier::selectBest(
    const std::vector<std::pair<IssueType, float>>& scores,
    std::vector<ClassificationCandidate>& alternatives)
{
    if (scores.empty()) return IssueType::Unknown;

    // Sort by score descending
    std::vector<std::pair<IssueType, float>> sorted = scores;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    IssueType best = sorted[0].first;

    // Build alternatives from remaining
    for (size_t i = 1; i < sorted.size(); i++) {
        if (sorted[i].second >= m_confidenceThreshold * 0.5f) {
            alternatives.push_back({sorted[i].first, sorted[i].second});
        }
    }

    return best;
}

DetectedIssue IssueClassifier::classify(const RegionFeatures& features) {
    DetectedIssue issue;
    issue.confidence = 0.0f;
    issue.evidence = ClassificationEvidence{};

    // Score every type
    std::vector<std::pair<IssueType, float>> scores = {
        {IssueType::LightingDifference, scoreLighting(features)},
        {IssueType::ShadowDifference, scoreShadow(features)},
        {IssueType::BloomDifference, scoreBloom(features)},
        {IssueType::ReflectionDifference, scoreReflection(features)},
        {IssueType::TextureDifference, scoreTexture(features)},
        {IssueType::MaterialDifference, scoreMaterial(features)},
        {IssueType::TransparencyDifference, scoreTransparency(features)},
        {IssueType::EdgeDifference, scoreEdge(features)},
        {IssueType::MissingGeometry, scoreMissingGeometry(features)},
        {IssueType::ExtraGeometry, scoreExtraGeometry(features)},
        {IssueType::MissingObject, scoreMissingObject(features)},
        {IssueType::MissingParticle, scoreMissingParticle(features)},
        {IssueType::MissingUI, scoreMissingUI(features)},
        {IssueType::TextDifference, scoreText(features)},
        {IssueType::StereoOffset, scoreStereoOffset(features)},
        {IssueType::DepthDisparityError, scoreDepthDisparity(features)},
        {IssueType::OcclusionDifference, scoreOcclusion(features)},
        {IssueType::PostProcessDifference, scorePostProcess(features)},
        {IssueType::TemporalDifference, scoreTemporal(features)},
        {IssueType::LensBoundary, scoreLensBoundary(features)},
    };

    std::vector<ClassificationCandidate> alternatives;
    IssueType bestType = selectBest(scores, alternatives);
    auto it = std::find_if(scores.begin(), scores.end(),
                           [bestType](const auto& p) { return p.first == bestType; });
    float bestScore = it != scores.end() ? it->second : 0.0f;

    issue.type = bestType;
    issue.confidence = bestScore;

    if (bestScore >= m_confidenceThreshold) {
        issue.type = bestType;
        issue.confidence = bestScore;
    } else if (bestScore >= m_confidenceThreshold * 0.7f) {
        issue.type = bestType;
        issue.confidence = bestScore;
    } else {
        issue.type = IssueType::LowConfidence;
        issue.confidence = bestScore;
    }

    issue.alternatives = alternatives;

    // Build reasoning text
    std::string reason;
    auto appendReason = [&](const char* label, float /*val*/, const char* desc) {
        reason += std::string("  ") + label + ": " + desc + "\n";
    };
    reason += "Classification:\n";
    reason += std::string("  ") + IssueTypeName(issue.type) + "\n\n";
    reason += "Evidence:\n";
    appendReason("Brightness Diff", features.brightnessDiff,
                 features.brightnessDiff > 0.15f ? "High" :
                 features.brightnessDiff > 0.05f ? "Medium" : "Low");
    appendReason("Contrast Diff", features.contrastDiff,
                 features.contrastDiff > 0.15f ? "High" :
                 features.contrastDiff > 0.05f ? "Medium" : "Low");
    appendReason("Color Diff", features.colorDiff,
                 features.colorDiff > 0.1f ? "High" :
                 features.colorDiff > 0.03f ? "Medium" : "Low");
    appendReason("Edge Similarity", features.edgeSimilarity,
                 features.edgeSimilarity > 0.8f ? "High" :
                 features.edgeSimilarity > 0.5f ? "Medium" : "Low");
    appendReason("Texture Similarity", features.textureSimilarity,
                 features.textureSimilarity > 0.8f ? "High" :
                 features.textureSimilarity > 0.5f ? "Medium" : "Low");
    appendReason("Histogram Similarity", features.histogramSimilarity,
                 features.histogramSimilarity > 0.8f ? "High" :
                 features.histogramSimilarity > 0.5f ? "Medium" : "Low");
    reason += "  Content Match: " +
        std::string(std::abs(features.leftContentDensity - features.rightContentDensity) < 0.15f
                    ? "Yes" : "No") + "\n";
    reason += "  Geometry Match: " +
        std::string(features.edgeSimilarity > 0.6f ? "Yes" : "Partial") + "\n";
    char confBuf[64];
    snprintf(confBuf, sizeof(confBuf), "\nConfidence: %.0f%%\n", issue.confidence * 100.0f);
    reason += confBuf;

    if (!alternatives.empty()) {
        reason += "\nAlternatives:\n";
        for (auto& alt : alternatives) {
            char altBuf[64];
            snprintf(altBuf, sizeof(altBuf), "  %s (%.0f%%)\n",
                     IssueTypeName(alt.type), alt.confidence * 100.0f);
            reason += altBuf;
        }
    }

    issue.reasoningText = reason;
    return issue;
}
