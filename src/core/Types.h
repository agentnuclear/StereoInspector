#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <deque>
#include <ctime>
#include <opencv2/core.hpp>

enum class VisualizationMode {
    Normal,
    DifferenceHeatmap,
    StereoDifferenceOverlay,
    EdgeComparison,
    FeatureMatchOverlay,
    HistogramView,
    BlurMap,
    BlinkLeft,
    BlinkRight,
    DisparityHeatmap
};

constexpr const char* VisualizationModeName(VisualizationMode m) {
    switch (m) {
        case VisualizationMode::Normal: return "Normal";
        case VisualizationMode::DifferenceHeatmap: return "Heatmap";
        case VisualizationMode::StereoDifferenceOverlay: return "Diff Overlay";
        case VisualizationMode::EdgeComparison: return "Edges";
        case VisualizationMode::FeatureMatchOverlay: return "Features";
        case VisualizationMode::HistogramView: return "Histogram";
        case VisualizationMode::BlurMap: return "Blur Map";
        case VisualizationMode::BlinkLeft: return "Blink L";
        case VisualizationMode::BlinkRight: return "Blink R";
        case VisualizationMode::DisparityHeatmap: return "Disp Heatmap";
    }
    return "?";
}

enum class IssueType {
    Unknown,
    LightingDifference,
    ShadowDifference,
    BloomDifference,
    ReflectionDifference,
    TextureDifference,
    MaterialDifference,
    TransparencyDifference,
    EdgeDifference,
    MissingGeometry,
    ExtraGeometry,
    MissingObject,
    MissingParticle,
    MissingUI,
    TextDifference,
    StereoOffset,
    DepthDisparityError,
    OcclusionDifference,
    PostProcessDifference,
    TemporalDifference,
    LensBoundary,
    LowConfidence
};

constexpr const char* IssueTypeName(IssueType t) {
    switch (t) {
        case IssueType::Unknown: return "Unknown";
        case IssueType::LightingDifference: return "Lighting Difference";
        case IssueType::ShadowDifference: return "Shadow Difference";
        case IssueType::BloomDifference: return "Bloom Difference";
        case IssueType::ReflectionDifference: return "Reflection Difference";
        case IssueType::TextureDifference: return "Texture Difference";
        case IssueType::MaterialDifference: return "Material Difference";
        case IssueType::TransparencyDifference: return "Transparency Difference";
        case IssueType::EdgeDifference: return "Edge Difference";
        case IssueType::MissingGeometry: return "Missing Geometry";
        case IssueType::ExtraGeometry: return "Extra Geometry";
        case IssueType::MissingObject: return "Missing Object";
        case IssueType::MissingParticle: return "Missing Particle";
        case IssueType::MissingUI: return "Missing UI";
        case IssueType::TextDifference: return "Text Difference";
        case IssueType::StereoOffset: return "Stereo Offset";
        case IssueType::DepthDisparityError: return "Depth/Disparity Error";
        case IssueType::OcclusionDifference: return "Occlusion Difference";
        case IssueType::PostProcessDifference: return "Post Process Difference";
        case IssueType::TemporalDifference: return "Temporal Difference";
        case IssueType::LensBoundary: return "Lens Boundary";
        case IssueType::LowConfidence: return "Low Confidence";
    }
    return "?";
}

struct IssueRect {
    int x = 0, y = 0, width = 0, height = 0;
};

struct ClassificationEvidence {
    float brightnessDiff = 0.0f;
    float contrastDiff = 0.0f;
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
    float edgeRatio = 0.0f;
    float meanDiff = 0.0f;
    float bloomRatio = 0.0f;
    float shadowRatio = 0.0f;
    float leftContentDensity = 0.0f;
    float rightContentDensity = 0.0f;
    bool hasColor = false;
    bool nearBorder = false;
    bool nearCenter = false;
};

struct ClassificationCandidate {
    IssueType type = IssueType::Unknown;
    float confidence = 0.0f;
};

struct DetectedIssue {
    IssueType type = IssueType::Unknown;
    float confidence = 0.0f;
    IssueRect boundingBox;
    int areaPixels = 0;
    float severity = 0.0f;
    float centerX = 0.0f, centerY = 0.0f;
    bool leftEyeOnly = false;
    bool isInvalidRegion = false;
    std::string invalidReason;
    std::vector<ClassificationCandidate> alternatives;
    ClassificationEvidence evidence;
    std::string reasoningText;
};

enum class StereoLayout {
    Unknown,
    SideBySide,
    OverUnder,
    SideBySide_Wide,
    SideBySide_HalfRes,
    Custom
};

enum class FrameStatus {
    PASS,
    WARNING,
    FAIL
};

enum class StereoStatus {
    SAFE,
    WARNING,
    DESYNC,
    UNKNOWN
};

constexpr const char* StereoStatusName(StereoStatus s) {
    switch (s) {
        case StereoStatus::SAFE: return "SAFE";
        case StereoStatus::WARNING: return "WARNING";
        case StereoStatus::DESYNC: return "DE-SYNC";
        case StereoStatus::UNKNOWN: return "UNKNOWN";
    }
    return "?";
}

struct SceneConfidence {
    double luminanceScore = 1.0;
    double edgeDensityScore = 1.0;
    double featureScore = 1.0;
    double textureScore = 1.0;
    double entropyScore = 1.0;
    double overall = 1.0;
    bool reliable = true;
};

// ------ Stereo Correspondence Metrics ------
struct DisparityMetrics {
    double meanDisparity = 0.0;
    double medianDisparity = 0.0;
    double stdDisparity = 0.0;
    double minDisparity = 0.0;
    double maxDisparity = 0.0;
    double invalidRatio = 0.0;        // % pixels without valid disparity
    double smoothness = 1.0;          // 0-1, how smooth the disparity field is
    double verticalAsymmetry = 0.0;   // disparity skew between top/bottom halves
    double disparityRange = 0.0;      // max - min
    cv::Mat disparityMap;              // Raw disparity map (float) for visualization
    cv::Mat validMap;                  // Valid pixel mask for visualization
};

struct MatchQualityMetrics {
    double inlierRatio = 1.0;         // fraction of matches consistent with dominant disparity
    double confidence = 1.0;          // overall correspondence confidence 0-1
    int totalMatches = 0;
    int inlierCount = 0;
    double rmsResidual = 0.0;         // reprojection error after alignment
};

struct ResidualMetrics {
    double alignedSSIM = 0.0;         // SSIM after warping alignment
    double alignedPixDiffPercent = 0.0;
    double alignedBrightnessDelta = 0.0;
    double alignedEdgeSimilarity = 0.0;
    double alignedHistogramCorrelation = 0.0;
    double occlusionRatio = 0.0;      // % occluded pixels
};

// ------ Asymmetric Rendering Defect Detection ------
struct AsymmetryMetrics {
    double lightingAsymmetry = 0.0;         // mean luminance difference after alignment
    double postProcessAsymmetry = 0.0;      // color histogram shape difference
    double bloomAsymmetry = 0.0;            // bloom/glow mismatch after alignment
    double shadowAsymmetry = 0.0;           // shadow region mismatch
    double chromaticAsymmetry = 0.0;        // color fringing difference
    double blurAsymmetry = 0.0;             // sharpness difference
    double textureAsymmetry = 0.0;          // texture detail difference (high-freq energy)
    double geometryMissing = 0.0;           // pixels in one eye with no correspondence
    double contrastAsymmetry = 0.0;         // contrast difference
};

// ------ Temporal Analysis ------
struct TemporalMetrics {
    double flickerScore = 0.0;         // frame-to-frame luminance flicker (0-1)
    double temporalSSIM = 1.0;         // SSIM between consecutive frames (same eye)
    double temporalStability = 1.0;    // consistency of metrics over last N frames
    double disparityStability = 1.0;   // frame-to-frame disparity consistency
    double instantTransitions = 0.0;   // count of sudden scene cuts / transitions
};

struct MetricDeviation {
    std::string metricName;
    double baselineValue = 0.0;
    double currentValue = 0.0;
    double delta = 0.0;
    double absDelta = 0.0;
    double severity = 0.0;     // 0.0–1.0 (delta / tolerance, capped at 1.0)
    double tolerance = 1.0;
    bool warning = false;       // severity > 0.5
    bool fail = false;          // severity >= 1.0
};

// ------ Baseline: Expected Stereo Model ------
struct StereoModelBaseline {
    bool active = false;
    uint64_t captureFrameNumber = 0;
    std::string captureTimestamp;
    float captureConfidence = 0.0f;
    std::chrono::steady_clock::time_point captureTimePoint;

    // Reference metric values captured at sync time
    double refAlignedSSIM = 0.0;
    double refAlignedPixDiffPercent = 0.0;
    double refAlignedBrightnessDelta = 0.0;
    double refAlignedHistogramCorrelation = 0.0;
    double refAlignedEdgeSimilarity = 0.0;
    double refOcclusionRatio = 0.0;
    double refDisparityMean = 0.0;
    double refDisparityStd = 0.0;
    double refDisparityRange = 0.0;
    double refDisparityInvalidRatio = 0.0;
    double refDisparitySmoothness = 0.0;
    double refVerticalAsymmetry = 0.0;
    double refLightingAsymmetry = 0.0;
    double refBloomAsymmetry = 0.0;
    double refShadowAsymmetry = 0.0;
    double refPostProcessAsymmetry = 0.0;
    double refTextureAsymmetry = 0.0;
    double refChromaticAsymmetry = 0.0;
    double refContrastAsymmetry = 0.0;
    double refStereoOffset = 0.0;

    // Overall deviation summary
    double overallDeviation = 0.0;   // 0.0–1.0 weighted average
    uint64_t framesCompared = 0;
    bool tracking = false;
};

// ------ Main Analysis Result ------
struct AnalysisResult {
    // Legacy fields (kept for backward compat, now computed on ALIGNED images)
    double ssim = 0.0;
    double pixelDiffPercent = 0.0;
    double histogramCorrelation = 1.0;
    double edgeSimilarity = 1.0;
    int featureMatchCount = 0;
    double opticalFlowMagnitude = 0.0;
    double blurDelta = 0.0;
    double brightnessDelta = 0.0;
    double contrastDelta = 0.0;
    double bloomDifference = 0.0;
    double shadowDifference = 0.0;
    double stereoOffset = 0.0;
    int ocrTextMismatches = 0;

    // VR-specific legacy
    double lensDistortionDelta = 0.0;
    double foveationAsymmetry = 0.0;
    double godRayDifference = 0.0;

    // === New stereo-aware metrics ===
    DisparityMetrics disparity;
    MatchQualityMetrics matchQuality;
    ResidualMetrics residual;
    AsymmetryMetrics asymmetry;
    TemporalMetrics temporal;

    // Baseline: expected stereo model
    StereoModelBaseline stereoModel;

    // Scene confidence
    SceneConfidence sceneConfidence;

    // Detected issues from residual analysis
    std::vector<DetectedIssue> detectedIssues;
    cv::Mat residualDiffMap;

    // Stereo status (baseline-based)
    StereoStatus stereoStatus = StereoStatus::SAFE;
    double stereoIntegrityScore = 100.0;  // replaces stereoHealthScore
    std::vector<MetricDeviation> deviations; // populated when baseline active

    // Fallback for no-baseline mode
    FrameStatus status = FrameStatus::PASS;
    std::vector<std::string> issues;
    double stereoHealthScore = 100.0;   // kept for fallback MetricHistory

    std::chrono::steady_clock::time_point timestamp;
    int64_t frameNumber = 0;
};

// ------ Backward compat: CaptureFrame, FrameTime, MetricHistory ------
struct CaptureFrame {
    cv::Mat frame;
    cv::Mat leftEye;
    cv::Mat rightEye;
    int64_t frameNumber = 0;
    int width = 0;
    int height = 0;
    StereoLayout detectedLayout = StereoLayout::Unknown;
    int splitPoint = 0;
    std::chrono::steady_clock::time_point captureTime;
};

struct FrameTime {
    double captureMs = 0.0;
    double analysisMs = 0.0;
    double totalMs = 0.0;
    double fps = 0.0;
    double captureFps = 0.0;
    double analysisFps = 0.0;
};

using HistoryBuffer = std::deque<double>;

struct MetricHistory {
    static constexpr size_t MAX_HISTORY = 300;

    HistoryBuffer healthScore;
    HistoryBuffer ssim;
    HistoryBuffer pixelDiff;
    HistoryBuffer brightnessDelta;
    HistoryBuffer stereoOffset;
    HistoryBuffer alignmentSSIM;
    HistoryBuffer disparityMean;
    HistoryBuffer temporalStability;
    HistoryBuffer flickerScore;
    HistoryBuffer disparityStability;

    void push(const AnalysisResult& r) {
        pushOrPrune(healthScore, r.stereoIntegrityScore);
        pushOrPrune(ssim, r.ssim);
        pushOrPrune(pixelDiff, r.pixelDiffPercent);
        pushOrPrune(brightnessDelta, r.brightnessDelta);
        pushOrPrune(stereoOffset, r.stereoOffset);
        pushOrPrune(alignmentSSIM, r.residual.alignedSSIM);
        pushOrPrune(disparityMean, r.disparity.meanDisparity);
        pushOrPrune(temporalStability, r.temporal.temporalStability);
        pushOrPrune(flickerScore, r.temporal.flickerScore);
        pushOrPrune(disparityStability, r.temporal.disparityStability);
    }

private:
    static void pushOrPrune(HistoryBuffer& buf, double val) {
        buf.push_back(val);
        if (buf.size() > MAX_HISTORY) buf.pop_front();
    }
};
