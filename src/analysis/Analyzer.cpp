#include "Analyzer.h"
#include "FeatureCache.h"
#include "modules/TemporalAnalyzer.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>
#include <algorithm>
#include <cmath>
#include <spdlog/spdlog.h>

struct AnalyzerPipeline::Impl {
    std::vector<std::unique_ptr<IStereoAnalyzer>> m_analyzers;
    AppConfig m_config;
    AnalysisResult m_latestResult;
    FrameTime m_frameTime;
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_newFrame{false};
    MetricHistory m_history;

    // Stereo model baseline
    StereoModelBaseline m_baseline;
    std::atomic<bool> m_baselineActive{false};
    std::atomic<bool> m_baselinePending{false};
    cv::Mat m_pendingLeft;
    cv::Mat m_pendingRight;
    BaselineCapturedCallback m_onBaselineCaptured;
    BaselineRefusedCallback m_onBaselineRefused;

    // Temporal analyzer
    std::unique_ptr<TemporalAnalyzer> m_temporal;

    mutable std::mutex m_resultMutex;
    mutable std::mutex m_timeMutex;
    mutable std::mutex m_historyMutex;
    mutable std::mutex m_baselineMutex;
    std::thread m_worker;
};

AnalyzerPipeline::AnalyzerPipeline(const AppConfig& config)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->m_config = config;
    m_impl->m_temporal = std::make_unique<TemporalAnalyzer>();
}

AnalyzerPipeline::~AnalyzerPipeline() { stop(); }

void AnalyzerPipeline::registerAnalyzer(std::unique_ptr<IStereoAnalyzer> analyzer) {
    m_impl->m_analyzers.push_back(std::move(analyzer));
}

void AnalyzerPipeline::start(LatestFrameBuffer& frameBuffer) {
    if (m_impl->m_running.exchange(true)) return;
    m_impl->m_worker = std::thread(&AnalyzerPipeline::processLoop, this, std::ref(frameBuffer));
}

void AnalyzerPipeline::stop() {
    m_impl->m_running = false;
    if (m_impl->m_worker.joinable()) m_impl->m_worker.join();
}

void AnalyzerPipeline::waitForFrame() { m_impl->m_newFrame = false; }
bool AnalyzerPipeline::isRunning() const { return m_impl->m_running.load(); }

AnalysisResult AnalyzerPipeline::getLatestResult() const {
    std::lock_guard<std::mutex> lock(m_impl->m_resultMutex);
    return m_impl->m_latestResult;
}

FrameTime AnalyzerPipeline::getFrameTime() const {
    std::lock_guard<std::mutex> lock(m_impl->m_timeMutex);
    return m_impl->m_frameTime;
}

MetricHistory AnalyzerPipeline::getHistory() const {
    std::lock_guard<std::mutex> lock(m_impl->m_historyMutex);
    return m_impl->m_history;
}

std::vector<IStereoAnalyzer*> AnalyzerPipeline::analyzers() const {
    std::vector<IStereoAnalyzer*> result;
    for (auto& a : m_impl->m_analyzers) result.push_back(a.get());
    return result;
}

void AnalyzerPipeline::setConfig(const AppConfig& config) { m_impl->m_config = config; }

AppConfig AnalyzerPipeline::getConfig() const { return m_impl->m_config; }

void AnalyzerPipeline::setCheckToggles(const CheckToggles& toggles) {
    m_impl->m_config.checks = toggles;
    for (auto& a : m_impl->m_analyzers) {
        const auto& name = a->name();
        if (name == "SSIM") { a->setEnabled(toggles.ssim); }
        else if (name == "PixelDiff") { a->setEnabled(toggles.pixelDiff); }
        else if (name == "Histogram") { a->setEnabled(toggles.histogram); }
        else if (name == "Edge") { a->setEnabled(toggles.edge); }
        else if (name == "ORB") { a->setEnabled(toggles.orb); }
        else if (name == "OpticalFlow") { a->setEnabled(toggles.opticalFlow); }
        else if (name == "Blur") { a->setEnabled(toggles.blur); }
        else if (name == "Brightness") { a->setEnabled(toggles.brightness); }
        else if (name == "Contrast") { a->setEnabled(toggles.contrast); }
        else if (name == "Bloom") { a->setEnabled(toggles.bloom); }
        else if (name == "Shadow") { a->setEnabled(toggles.shadow); }
        else if (name == "StereoOffset") { a->setEnabled(toggles.stereoOffset); }
        else if (name == "OCR") { a->setEnabled(toggles.ocr); }
    }
}

void AnalyzerPipeline::setBaseline(const cv::Mat& leftEye, const cv::Mat& rightEye) {
    std::lock_guard<std::mutex> lock(m_impl->m_baselineMutex);
    m_impl->m_pendingLeft = leftEye.clone();
    m_impl->m_pendingRight = rightEye.clone();
    m_impl->m_baselinePending = true;
    spdlog::info("Baseline pending — will capture stereo model on next analysis frame");
}

void AnalyzerPipeline::clearBaseline() {
    {
        std::lock_guard<std::mutex> lock(m_impl->m_baselineMutex);
        m_impl->m_baseline = StereoModelBaseline{};
        m_impl->m_baselineActive = false;
        m_impl->m_pendingLeft.release();
        m_impl->m_pendingRight.release();
        m_impl->m_temporal->reset();
    }
    spdlog::info("Stereo model baseline cleared");
}

bool AnalyzerPipeline::hasBaseline() const { return m_impl->m_baselineActive.load(); }

StereoModelBaseline AnalyzerPipeline::getBaselineInfo() const {
    std::lock_guard<std::mutex> lock(m_impl->m_baselineMutex);
    return m_impl->m_baseline;
}

void AnalyzerPipeline::setOnBaselineCaptured(BaselineCapturedCallback cb) {
    m_impl->m_onBaselineCaptured = std::move(cb);
}

void AnalyzerPipeline::setOnBaselineRefused(BaselineRefusedCallback cb) {
    m_impl->m_onBaselineRefused = std::move(cb);
}

SceneConfidence AnalyzerPipeline::computeSceneConfidence(const cv::Mat& left, const cv::Mat& right) {
    SceneConfidence sc;
    double threshold = m_impl->m_config.sceneConfidenceThreshold;

    auto evalRegion = [&](const cv::Mat& region) -> double {
        if (region.empty() || region.total() < 100) return 0.0;

        double totalPixels = (double)region.total();

        // Luminance
        double mean = cv::mean(region)[0] / 255.0;
        double lumScore = 1.0;
        if (mean < 0.05) lumScore = mean / 0.05;
        else if (mean > 0.95) lumScore = (1.0 - mean) / 0.05;

        // Edge density via Sobel magnitude
        cv::Mat gx, gy, mag;
        cv::Sobel(region, gx, CV_32F, 1, 0, 3);
        cv::Sobel(region, gy, CV_32F, 0, 1, 3);
        cv::magnitude(gx, gy, mag);
        double edgeDensity = cv::mean(mag)[0] / 255.0;
        double edgeScore = std::min(1.0, edgeDensity * 10.0);

        // Texture variance – Laplacian variance
        cv::Mat lap;
        cv::Laplacian(region, lap, CV_32F);
        cv::Scalar mLap, sLap;
        cv::meanStdDev(lap, mLap, sLap);
        double lapVar = sLap[0] / 255.0;
        double texScore = std::min(1.0, lapVar * 20.0);

        // Feature count – FAST corners (lower threshold: 10 instead of 20)
        std::vector<cv::KeyPoint> kps;
        cv::FAST(region, kps, 10, true);
        double featRatio = (double)kps.size() / std::max(1.0, totalPixels / 1000.0);
        double featScore = std::min(1.0, featRatio * 2.0);

        // Entropy
        int histSize = 256;
        float range[] = {0, 256};
        const float* histRange = {range};
        cv::Mat hist;
        cv::calcHist(&region, 1, 0, cv::Mat(), hist, 1, &histSize, &histRange);
        cv::normalize(hist, hist, 1.0, 0.0, cv::NORM_L1);
        double entropy = 0.0;
        for (int i = 0; i < histSize; i++) {
            float p = hist.at<float>(i);
            if (p > 0.001f) entropy -= p * std::log2(p);
        }
        double entScore = entropy / std::log2(256.0);

        return 0.20 * lumScore + 0.20 * edgeScore +
               0.20 * texScore + 0.20 * featScore + 0.20 * entScore;
    };

    auto evalEyeQuadrants = [&](const cv::Mat& eye) -> std::vector<double> {
        if (eye.empty()) return {0.0, 0.0, 0.0, 0.0};
        cv::Mat gray;
        if (eye.channels() == 3) cv::cvtColor(eye, gray, cv::COLOR_BGR2GRAY);
        else gray = eye;

        int midX = gray.cols / 2;
        int midY = gray.rows / 2;
        std::vector<double> scores;
        scores.push_back(evalRegion(gray(cv::Rect(0, 0, midX, midY))));          // TL
        scores.push_back(evalRegion(gray(cv::Rect(midX, 0, gray.cols - midX, midY)))); // TR
        scores.push_back(evalRegion(gray(cv::Rect(0, midY, midX, gray.rows - midY)))); // BL
        scores.push_back(evalRegion(gray(cv::Rect(midX, midY, gray.cols - midX, gray.rows - midY)))); // BR
        return scores;
    };

    // Quadrant scores for each eye
    std::vector<double> leftQ = evalEyeQuadrants(left);
    std::vector<double> rightQ = evalEyeQuadrants(right);

    // Take worst score per quadrant across both eyes
    std::vector<double> perQuadrant(4);
    for (int i = 0; i < 4; i++) {
        perQuadrant[i] = std::min(leftQ[i], rightQ[i]);
    }

    // Use average of best 2 quadrants
    std::sort(perQuadrant.begin(), perQuadrant.end(), std::greater<double>());
    double bestAvg = (perQuadrant[0] + perQuadrant[1]) / 2.0;

    // Full-image fallback for uniform scenes
    cv::Mat leftGray, rightGray;
    if (left.channels() == 3) { cv::cvtColor(left, leftGray, cv::COLOR_BGR2GRAY); cv::cvtColor(right, rightGray, cv::COLOR_BGR2GRAY); }
    else { leftGray = left; rightGray = right; }
    double leftFull = evalRegion(leftGray);
    double rightFull = evalRegion(rightGray);
    double fullScore = std::min(leftFull, rightFull);

    // Overall = blend: prefer best-quadrant analysis but don't penalize uniform scenes
    sc.overall = std::max(bestAvg, fullScore * 0.5);
    sc.overall = std::max(0.0, std::min(1.0, sc.overall));
    sc.reliable = (sc.overall >= threshold);

    sc.luminanceScore = sc.overall;
    sc.edgeDensityScore = sc.overall;
    sc.textureScore = sc.overall;
    sc.featureScore = sc.overall;
    sc.entropyScore = sc.overall;

    return sc;
}

void AnalyzerPipeline::captureBaselineFromResult(const AnalysisResult& result) {
    auto& b = m_impl->m_baseline;
    b = StereoModelBaseline{};
    b.active = true;
    b.captureFrameNumber = result.frameNumber;

    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&tt));
    b.captureTimestamp = buf;

    b.captureConfidence = (float)result.matchQuality.confidence;
    b.captureTimePoint = std::chrono::steady_clock::now();

    // Reference values
    b.refAlignedSSIM = result.residual.alignedSSIM;
    b.refAlignedPixDiffPercent = result.residual.alignedPixDiffPercent;
    b.refAlignedBrightnessDelta = result.residual.alignedBrightnessDelta;
    b.refAlignedHistogramCorrelation = result.residual.alignedHistogramCorrelation;
    b.refAlignedEdgeSimilarity = result.residual.alignedEdgeSimilarity;
    b.refOcclusionRatio = result.residual.occlusionRatio;
    b.refDisparityMean = result.disparity.meanDisparity;
    b.refDisparityStd = result.disparity.stdDisparity;
    b.refDisparityRange = result.disparity.disparityRange;
    b.refDisparityInvalidRatio = result.disparity.invalidRatio;
    b.refDisparitySmoothness = result.disparity.smoothness;
    b.refVerticalAsymmetry = result.disparity.verticalAsymmetry;
    b.refLightingAsymmetry = result.asymmetry.lightingAsymmetry;
    b.refBloomAsymmetry = result.asymmetry.bloomAsymmetry;
    b.refShadowAsymmetry = result.asymmetry.shadowAsymmetry;
    b.refPostProcessAsymmetry = result.asymmetry.postProcessAsymmetry;
    b.refTextureAsymmetry = result.asymmetry.textureAsymmetry;
    b.refChromaticAsymmetry = result.asymmetry.chromaticAsymmetry;
    b.refContrastAsymmetry = result.asymmetry.contrastAsymmetry;
    b.refStereoOffset = result.stereoOffset;

    b.overallDeviation = 0.0;
    b.framesCompared = 0;
    b.tracking = true;

    m_impl->m_baselineActive = true;
    spdlog::info("Stereo baseline captured: SSIM={:.4f}, PixDiff={:.2f}%, DispMean={:.1f}",
                 result.residual.alignedSSIM, result.residual.alignedPixDiffPercent,
                 result.disparity.meanDisparity);
}

void AnalyzerPipeline::compareWithBaseline(AnalysisResult& result) {
    auto& b = m_impl->m_baseline;
    if (!b.active) return;

    result.deviations.clear();
    result.stereoModel.active = true;
    result.stereoModel.captureFrameNumber = b.captureFrameNumber;
    result.stereoModel.captureTimestamp = b.captureTimestamp;
    result.stereoModel.captureConfidence = b.captureConfidence;
    result.stereoModel.captureTimePoint = b.captureTimePoint;
    result.stereoModel.framesCompared = b.framesCompared;
    result.stereoModel.tracking = b.tracking;

    // Copy reference values into result for display
    result.stereoModel.refAlignedSSIM = b.refAlignedSSIM;
    result.stereoModel.refAlignedPixDiffPercent = b.refAlignedPixDiffPercent;
    result.stereoModel.refAlignedBrightnessDelta = b.refAlignedBrightnessDelta;
    result.stereoModel.refAlignedHistogramCorrelation = b.refAlignedHistogramCorrelation;
    result.stereoModel.refAlignedEdgeSimilarity = b.refAlignedEdgeSimilarity;
    result.stereoModel.refOcclusionRatio = b.refOcclusionRatio;
    result.stereoModel.refDisparityMean = b.refDisparityMean;
    result.stereoModel.refDisparityStd = b.refDisparityStd;
    result.stereoModel.refDisparityRange = b.refDisparityRange;
    result.stereoModel.refDisparityInvalidRatio = b.refDisparityInvalidRatio;
    result.stereoModel.refDisparitySmoothness = b.refDisparitySmoothness;
    result.stereoModel.refVerticalAsymmetry = b.refVerticalAsymmetry;
    result.stereoModel.refLightingAsymmetry = b.refLightingAsymmetry;
    result.stereoModel.refBloomAsymmetry = b.refBloomAsymmetry;
    result.stereoModel.refShadowAsymmetry = b.refShadowAsymmetry;
    result.stereoModel.refPostProcessAsymmetry = b.refPostProcessAsymmetry;
    result.stereoModel.refTextureAsymmetry = b.refTextureAsymmetry;
    result.stereoModel.refChromaticAsymmetry = b.refChromaticAsymmetry;
    result.stereoModel.refContrastAsymmetry = b.refContrastAsymmetry;
    result.stereoModel.refStereoOffset = b.refStereoOffset;

    struct MetricRule {
        double current;
        double ref;
        double tolerance;
        double weight;
        const char* name;
    };

    MetricRule rules[] = {
        {result.residual.alignedSSIM,              b.refAlignedSSIM,              0.030, 0.20, "Aligned SSIM"},
        {result.residual.alignedPixDiffPercent,     b.refAlignedPixDiffPercent,    1.50,  0.12, "Pixel Diff"},
        {result.residual.alignedBrightnessDelta,    b.refAlignedBrightnessDelta,   0.020, 0.08, "Brightness \u0394"},
        {result.residual.alignedHistogramCorrelation,b.refAlignedHistogramCorrelation,0.05, 0.06, "Histogram Corr"},
        {result.residual.alignedEdgeSimilarity,     b.refAlignedEdgeSimilarity,    0.050, 0.08, "Edge Sim"},
        {result.residual.occlusionRatio,            b.refOcclusionRatio,           0.050, 0.05, "Occlusion"},
        {result.disparity.meanDisparity,            b.refDisparityMean,            3.0,   0.05, "Disp Mean"},
        {result.disparity.stdDisparity,             b.refDisparityStd,             3.0,   0.03, "Disp Std"},
        {result.disparity.disparityRange,           b.refDisparityRange,           15.0,  0.02, "Disp Range"},
        {result.disparity.invalidRatio,             b.refDisparityInvalidRatio,    0.10,  0.03, "Disp Invalid"},
        {result.disparity.smoothness,               b.refDisparitySmoothness,      0.20,  0.03, "Disp Smooth"},
        {result.disparity.verticalAsymmetry,        b.refVerticalAsymmetry,        5.0,   0.02, "Vert Asymmetry"},
        {result.asymmetry.lightingAsymmetry,        b.refLightingAsymmetry,        0.030, 0.05, "Light Asym"},
        {result.asymmetry.bloomAsymmetry,           b.refBloomAsymmetry,           0.030, 0.04, "Bloom Asym"},
        {result.asymmetry.shadowAsymmetry,          b.refShadowAsymmetry,          0.030, 0.04, "Shadow Asym"},
        {result.asymmetry.postProcessAsymmetry,     b.refPostProcessAsymmetry,     0.030, 0.03, "PP Asym"},
        {result.asymmetry.textureAsymmetry,         b.refTextureAsymmetry,         0.030, 0.03, "Texture Asym"},
        {result.asymmetry.chromaticAsymmetry,       b.refChromaticAsymmetry,       0.030, 0.03, "Chrome Asym"},
        {result.asymmetry.contrastAsymmetry,        b.refContrastAsymmetry,        0.030, 0.02, "Contrast Asym"},
        {result.stereoOffset,                       b.refStereoOffset,             3.0,   0.02, "Stereo Offset"},
    };

    double weightedSum = 0.0;
    double totalWeight = 0.0;

    for (const auto& rule : rules) {
        double absDelta = std::abs(rule.current - rule.ref);
        double severity = rule.tolerance > 0.0 ? std::min(1.0, absDelta / rule.tolerance) : 0.0;

        if (severity > 0.2) {
            MetricDeviation dev;
            dev.metricName = rule.name;
            dev.baselineValue = rule.ref;
            dev.currentValue = rule.current;
            dev.delta = rule.current - rule.ref;
            dev.absDelta = absDelta;
            dev.tolerance = rule.tolerance;
            dev.severity = severity;
            dev.warning = severity > 0.5;
            dev.fail = severity >= 1.0;
            result.deviations.push_back(dev);
        }

        weightedSum += severity * rule.weight;
        totalWeight += rule.weight;
    }

    b.overallDeviation = totalWeight > 0.0 ? std::min(1.0, weightedSum / totalWeight) : 0.0;
    result.stereoModel.overallDeviation = b.overallDeviation;
    b.framesCompared++;
    result.stereoModel.framesCompared = b.framesCompared;

    result.stereoIntegrityScore = std::max(0.0, 100.0 * (1.0 - b.overallDeviation));
    if (result.stereoIntegrityScore >= 80.0) result.stereoStatus = StereoStatus::SAFE;
    else if (result.stereoIntegrityScore >= 50.0) result.stereoStatus = StereoStatus::WARNING;
    else result.stereoStatus = StereoStatus::DESYNC;

    result.issues.clear();
}

void AnalyzerPipeline::processLoop(LatestFrameBuffer& frameBuffer) {
    spdlog::info("Analysis pipeline started (correspondence-first mode)");

    auto lastTime = std::chrono::steady_clock::now();
    int frameCount = 0;
    double fpsAccum = 0.0;

    while (m_impl->m_running.load()) {
        auto frame = frameBuffer.load();
        if (!frame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        // Check for pending baseline capture — analyze pending frames fully
        bool justCaptured = false;
        bool captureRefused = false;
        {
            std::lock_guard<std::mutex> lock(m_impl->m_baselineMutex);
            if (m_impl->m_baselinePending && !m_impl->m_pendingLeft.empty()) {
                // Refuse baseline if scene confidence is too low
                SceneConfidence pendingSc = computeSceneConfidence(
                    m_impl->m_pendingLeft, m_impl->m_pendingRight);

                if (!pendingSc.reliable) {
                    spdlog::warn("Refusing baseline capture: low scene confidence ({:.2f})",
                                 pendingSc.overall);
                    captureRefused = true;
                    m_impl->m_pendingLeft.release();
                    m_impl->m_pendingRight.release();
                    m_impl->m_baselinePending = false;
                } else {
                    AnalysisResult baseResult = analyzeFrame(
                        m_impl->m_pendingLeft, m_impl->m_pendingRight, frame->frameNumber);
                    m_impl->m_temporal->analyze(m_impl->m_pendingLeft, m_impl->m_pendingRight, baseResult);
                    captureBaselineFromResult(baseResult);
                    justCaptured = true;

                    m_impl->m_pendingLeft.release();
                    m_impl->m_pendingRight.release();
                    m_impl->m_baselinePending = false;

                    // Reset history + temporal analysis state
                    {
                        std::lock_guard<std::mutex> hlock(m_impl->m_historyMutex);
                        m_impl->m_history = MetricHistory{};
                    }
                    m_impl->m_temporal->reset();
                }
            }
        }

        // Notify overlay if capture was refused
        if (captureRefused && m_impl->m_onBaselineRefused) {
            m_impl->m_onBaselineRefused("Scene too dark or featureless for baseline capture");
        }

        // Notify overlay if capture succeeded
        if (justCaptured && m_impl->m_onBaselineCaptured) {
            m_impl->m_onBaselineCaptured(
                m_impl->m_baseline.captureFrameNumber,
                m_impl->m_baseline.captureConfidence,
                m_impl->m_baseline.captureTimestamp);
        }

        auto analysisStart = std::chrono::high_resolution_clock::now();

        AnalysisResult result;
        try {
            result = analyzeFrame(frame->leftEye, frame->rightEye, frame->frameNumber);
        } catch (const std::exception& e) {
            spdlog::error("analyzeFrame exception: {}", e.what());
            continue;
        } catch (...) {
            spdlog::error("analyzeFrame unknown exception");
            continue;
        }
        result.timestamp = std::chrono::steady_clock::now();

        const auto& chk = m_impl->m_config.checks;

        // Temporal analysis
        if (chk.temporal) {
            m_impl->m_temporal->analyze(frame->leftEye, frame->rightEye, result);
        }

        // Scene confidence — gate all downstream decisions
        if (chk.sceneConfidence) {
            result.sceneConfidence = computeSceneConfidence(frame->leftEye, frame->rightEye);
        } else {
            result.sceneConfidence.reliable = true;
            result.sceneConfidence.overall = 1.0;
        }

        if (!result.sceneConfidence.reliable) {
            // Insufficient visual information — do not compare, do not score
            result.stereoStatus = StereoStatus::UNKNOWN;
            result.stereoIntegrityScore = 100.0;
            result.deviations.clear();
            result.issues.clear();
        } else if (justCaptured) {
            // Baseline just captured — show perfect match on this frame
            result.stereoStatus = StereoStatus::SAFE;
            result.stereoIntegrityScore = 100.0;
            result.deviations.clear();
            result.issues.clear();
        } else if (m_impl->m_baselineActive && chk.baselineComparison) {
            std::lock_guard<std::mutex> lock(m_impl->m_baselineMutex);
            compareWithBaseline(result);
        } else {
            if (chk.healthScore) {
                computeHealthScore(result);
                result.stereoIntegrityScore = result.stereoHealthScore;
            } else {
                result.stereoIntegrityScore = 100.0;
            }
            if (result.status == FrameStatus::PASS) result.stereoStatus = StereoStatus::SAFE;
            else if (result.status == FrameStatus::WARNING) result.stereoStatus = StereoStatus::WARNING;
            else result.stereoStatus = StereoStatus::DESYNC;
        }

        {
            std::lock_guard<std::mutex> lock(m_impl->m_resultMutex);
            m_impl->m_latestResult = result;
        }
        {
            std::lock_guard<std::mutex> hlock(m_impl->m_historyMutex);
            m_impl->m_history.push(result);
        }

        auto analysisEnd = std::chrono::high_resolution_clock::now();
        double analysisMs = std::chrono::duration<double, std::milli>(analysisEnd - analysisStart).count();

        frameCount++;
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - lastTime).count();
        if (elapsed >= 1.0) {
            fpsAccum = frameCount / elapsed;
            frameCount = 0;
            lastTime = now;
        }

        {
            std::lock_guard<std::mutex> lock(m_impl->m_timeMutex);
            m_impl->m_frameTime.analysisMs = analysisMs;
            m_impl->m_frameTime.analysisFps = fpsAccum;
        }
        m_impl->m_newFrame = true;
    }
    spdlog::info("Analysis pipeline stopped");
}

AnalysisResult AnalyzerPipeline::analyzeFrame(const cv::Mat& left, const cv::Mat& right,
                                               int64_t frameNum) {
    AnalysisResult result;
    result.frameNumber = frameNum;

    // Step 0: Update feature cache (frame number + resolution change detection)
    FeatureCache::instance().newFrame(frameNum, left.rows, left.cols);

    // Step 1: Compute stereo correspondence
    CorrespondenceResult corr;
    if (m_impl->m_config.checks.correspondence) {
        corr = m_correspondence.compute(left, right);
    }

    // Step 2: Run registered analyzers on ALIGNED (warped) images
    const cv::Mat& alignedRight = corr.success ? corr.warpedRight : right;

    for (auto& analyzer : m_impl->m_analyzers) {
        if (!analyzer->enabled()) continue;
        try {
            analyzer->analyze(left, alignedRight, result);
        } catch (const std::exception& e) {
            spdlog::error("Analyzer '{}' failed: {}", analyzer->name(), e.what());
        }
    }

    // Step 3-6: Compute derived metrics from correspondence
    if (corr.success) {
        const auto& chk = m_impl->m_config.checks;
        result.residual.alignedSSIM = result.ssim;
        result.residual.alignedPixDiffPercent = result.pixelDiffPercent;
        result.residual.alignedBrightnessDelta = result.brightnessDelta;
        result.residual.alignedEdgeSimilarity = result.edgeSimilarity;
        result.residual.alignedHistogramCorrelation = result.histogramCorrelation;
        result.residual.occlusionRatio = corr.occlusionMask.empty()
            ? 0.0 : (double)cv::countNonZero(corr.occlusionMask) / (double)(left.total());
        if (chk.disparityMetrics) {
            computeDisparityMetrics(corr, result);
        }
        if (chk.matchQuality) {
            computeMatchQuality(corr, result);
        } else {
            result.matchQuality.confidence = 0.0;
            result.matchQuality.totalMatches = 0;
        }
        if (chk.asymmetry) {
            computeAsymmetry(left, corr.warpedRight, corr.occlusionMask, result);
        }
        if (chk.detectIssues) {
            try {
                detectIssues(result, left, corr.warpedRight, corr.occlusionMask);
            } catch (const std::exception& e) {
                spdlog::error("detectIssues exception: {}", e.what());
            } catch (...) {
                spdlog::error("detectIssues unknown exception");
            }
        }
    } else {
        result.matchQuality.confidence = 0.0;
        result.residual.alignedSSIM = 0.0;
    }

    return result;
}

void AnalyzerPipeline::computeDisparityMetrics(const CorrespondenceResult& corr,
                                                AnalysisResult& result) {
    const auto& dm = corr.disparityMap;
    result.disparity.meanDisparity = dm.meanDisparity;
    result.disparity.medianDisparity = dm.medianDisparity;
    result.disparity.stdDisparity = dm.stdDisparity;
    result.disparity.minDisparity = dm.minDisparity;
    result.disparity.maxDisparity = dm.maxDisparity;
    result.disparity.disparityRange = dm.maxDisparity - dm.minDisparity;
    result.disparity.invalidRatio = dm.invalidRatio;
    result.disparity.disparityMap = dm.disparity.clone();
    result.disparity.validMap = dm.validMask.clone();

    if (!dm.disparity.empty()) {
        cv::Mat gradX, gradY;
        cv::Sobel(dm.disparity, gradX, CV_32F, 1, 0, 3);
        cv::Sobel(dm.disparity, gradY, CV_32F, 0, 1, 3);
        cv::Mat gradMag;
        cv::magnitude(gradX, gradY, gradMag);
        cv::Scalar meanGrad = cv::mean(gradMag, dm.validMask);
        double maxGrad = result.disparity.disparityRange;
        result.disparity.smoothness = maxGrad > 0.1
            ? std::max(0.0, 1.0 - meanGrad[0] / maxGrad) : 1.0;

        int midY = dm.disparity.rows / 2;
        cv::Mat topD = dm.disparity(cv::Rect(0, 0, dm.disparity.cols, midY));
        cv::Mat botD = dm.disparity(cv::Rect(0, midY, dm.disparity.cols, dm.disparity.rows - midY));
        cv::Mat topM = dm.validMask(cv::Rect(0, 0, dm.disparity.cols, midY));
        cv::Mat botM = dm.validMask(cv::Rect(0, midY, dm.disparity.cols, dm.disparity.rows - midY));
        result.disparity.verticalAsymmetry =
            std::abs(cv::mean(topD, topM)[0] - cv::mean(botD, botM)[0]);
    }
}

void AnalyzerPipeline::computeMatchQuality(const CorrespondenceResult& corr,
                                            AnalysisResult& result) {
    result.matchQuality.confidence = (double)corr.matchQuality;
    result.matchQuality.totalMatches = corr.disparityMap.validMask.empty()
        ? 0 : cv::countNonZero(corr.disparityMap.validMask);

    if (!corr.disparityMap.disparity.empty()) {
        double mean = result.disparity.meanDisparity;
        double std = result.disparity.stdDisparity;
        double lower = mean - std;
        double upper = mean + std;
        int total = 0, inliers = 0;
        for (int y = 0; y < corr.disparityMap.disparity.rows; y++) {
            const float* d = corr.disparityMap.disparity.ptr<float>(y);
            const uchar* v = corr.disparityMap.validMask.ptr<uchar>(y);
            for (int x = 0; x < corr.disparityMap.disparity.cols; x++) {
                if (v[x]) {
                    total++;
                    if (d[x] >= lower && d[x] <= upper) inliers++;
                }
            }
        }
        result.matchQuality.inlierCount = inliers;
        result.matchQuality.inlierRatio = total > 0 ? (double)inliers / total : 1.0;
    }
}

void AnalyzerPipeline::computeAsymmetry(const cv::Mat& left, const cv::Mat& warpedRight,
                                         const cv::Mat& occlusionMask, AnalysisResult& result) {
    if (left.empty() || warpedRight.empty()) return;
    const auto& chk = m_impl->m_config.checks;

    cv::Mat lGray, rGray;
    if (left.channels() == 3) {
        cv::cvtColor(left, lGray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(warpedRight, rGray, cv::COLOR_BGR2GRAY);
    } else {
        lGray = left;
        rGray = warpedRight;
    }

    // Valid pixels: non-occluded
    cv::Mat valid;
    if (occlusionMask.empty()) {
        valid = cv::Mat::ones(lGray.size(), CV_8UC1);
    } else {
        cv::bitwise_not(occlusionMask, valid);
    }

    // Lighting asymmetry
    if (chk.lightingAsym) {
        cv::Scalar mL = cv::mean(lGray, valid);
        cv::Scalar mR = cv::mean(rGray, valid);
        result.asymmetry.lightingAsymmetry = std::abs(mL[0] - mR[0]) / 255.0;
        result.brightnessDelta = result.asymmetry.lightingAsymmetry;
    }

    // Texture asymmetry (Laplacian variance – high-frequency detail difference)
    if (chk.textureAsym) {
        cv::Mat lapL, lapR;
        cv::Laplacian(lGray, lapL, CV_32F);
        cv::Laplacian(rGray, lapR, CV_32F);
        cv::Scalar varL = cv::mean(lapL.mul(lapL), valid);
        cv::Scalar varR = cv::mean(lapR.mul(lapR), valid);
        double maxV = std::max(varL[0], varR[0]);
        result.asymmetry.textureAsymmetry = maxV > 0.1 ? std::abs(varL[0] - varR[0]) / maxV : 0.0;
    }

    // Blur asymmetry (Tenengrad focus measure – Sobel gradient variance)
    if (chk.blurAsym) {
        cv::Mat gxL, gyL, gxR, gyR, gradL, gradR;
        cv::Sobel(lGray, gxL, CV_32F, 1, 0, 3);
        cv::Sobel(lGray, gyL, CV_32F, 0, 1, 3);
        cv::Sobel(rGray, gxR, CV_32F, 1, 0, 3);
        cv::Sobel(rGray, gyR, CV_32F, 0, 1, 3);
        cv::magnitude(gxL, gyL, gradL);
        cv::magnitude(gxR, gyR, gradR);
        cv::Scalar mGradL, vGradL, mGradR, vGradR;
        cv::meanStdDev(gradL, mGradL, vGradL, valid);
        cv::meanStdDev(gradR, mGradR, vGradR, valid);
        double maxV = std::max(vGradL[0], vGradR[0]);
        result.asymmetry.blurAsymmetry = maxV > 0.1 ? std::abs(vGradL[0] - vGradR[0]) / maxV : 0.0;
        result.blurDelta = result.asymmetry.blurAsymmetry;
    }

    // Post-process asymmetry (histogram shape)
    if (chk.postProcessAsym) {
        int histSize = 256;
        float range[] = {0, 256};
        const float* histRange = {range};
        cv::Mat histL, histR;
        cv::calcHist(&lGray, 1, 0, valid, histL, 1, &histSize, &histRange);
        cv::calcHist(&rGray, 1, 0, valid, histR, 1, &histSize, &histRange);
        cv::normalize(histL, histL, 1.0);
        cv::normalize(histR, histR, 1.0);
        result.asymmetry.postProcessAsymmetry = 1.0 - cv::compareHist(histL, histR, cv::HISTCMP_CORREL);
        result.histogramCorrelation = 1.0 - result.asymmetry.postProcessAsymmetry;
    }

    // Bloom & shadow asymmetry
    if (chk.bloomAsym || chk.shadowAsym) {
        cv::Mat brightL, brightR;
        cv::threshold(lGray, brightL, 200, 255, cv::THRESH_BINARY);
        cv::threshold(rGray, brightR, 200, 255, cv::THRESH_BINARY);
        double totalValid = (double)cv::countNonZero(valid);
        if (totalValid > 0) {
            if (chk.bloomAsym) {
                double bL = (double)cv::countNonZero(brightL & valid);
                double bR = (double)cv::countNonZero(brightR & valid);
                result.asymmetry.bloomAsymmetry = std::abs(bL / totalValid - bR / totalValid);
                result.bloomDifference = result.asymmetry.bloomAsymmetry;
            }
            if (chk.shadowAsym) {
                cv::Mat darkL, darkR;
                cv::threshold(lGray, darkL, 50, 255, cv::THRESH_BINARY_INV);
                cv::threshold(rGray, darkR, 50, 255, cv::THRESH_BINARY_INV);
                double sL = (double)cv::countNonZero(darkL & valid);
                double sR = (double)cv::countNonZero(darkR & valid);
                result.asymmetry.shadowAsymmetry = std::abs(sL / totalValid - sR / totalValid);
                result.shadowDifference = result.asymmetry.shadowAsymmetry;
            }
        }
    }

    // Contrast asymmetry (stddev delta)
    if (chk.contrastAsym) {
        cv::Scalar mL, mR, stdL, stdR;
        cv::meanStdDev(lGray, mL, stdL);
        cv::meanStdDev(rGray, mR, stdR);
        result.asymmetry.contrastAsymmetry = std::abs(stdL[0] - stdR[0]) / 255.0;
        result.contrastDelta = result.asymmetry.contrastAsymmetry;
    }

    // Chromatic asymmetry (per-channel color shift difference)
    if (chk.chromaticAsym && left.channels() == 3 && warpedRight.channels() == 3) {
        std::vector<cv::Mat> chL, chR;
        cv::split(left, chL);
        cv::split(warpedRight, chR);
        double totalDiff = 0.0;
        for (int c = 0; c < 3; c++) {
            cv::Scalar mL = cv::mean(chL[c], valid);
            cv::Scalar mR = cv::mean(chR[c], valid);
            totalDiff += std::abs(mL[0] - mR[0]);
        }
        result.asymmetry.chromaticAsymmetry = totalDiff / (3.0 * 255.0);
    }

    // Geometry missing
    if (chk.geometryMissing) {
        result.asymmetry.geometryMissing = result.residual.occlusionRatio;
    }
}

void AnalyzerPipeline::computeHealthScore(AnalysisResult& result) {
    const auto& t = m_impl->m_config.thresholds;
    double score = 100.0;
    result.issues.clear();

    auto ck = [&](double val, double warn, double fail,
                    bool lowerIsBetter, const std::string& name, double w) {
        double nv = lowerIsBetter ? val : 1.0 - val;
        if (nv > fail) {
            score -= (nv - fail) / (1.0 - fail) * 50.0 * w;
            result.issues.push_back(name + " FAIL (" + std::to_string(val) + ")");
        } else if (nv > warn) {
            score -= (nv - warn) / (fail - warn) * 25.0 * w;
            result.issues.push_back(name + " WARN (" + std::to_string(val) + ")");
        }
    };

    auto cn = [&](double val, double warn, double fail,
                   const std::string& name, double w) {
        if (val < fail) {
            score -= (fail - val) / fail * 50.0 * w;
            result.issues.push_back(name + " FAIL (" + std::to_string(val) + ")");
        } else if (val < warn) {
            score -= (warn - val) / warn * 25.0 * w;
            result.issues.push_back(name + " WARN (" + std::to_string(val) + ")");
        }
    };

    // PRIMARY: Residual metrics (after alignment)
    cn(result.residual.alignedSSIM, t.ssimWarning, t.ssimFail, "AlignedSSIM", 0.20);
    ck(result.residual.alignedPixDiffPercent, t.pixelDiffWarning, t.pixelDiffFail, false, "AlignedPixDiff", 0.10);
    ck(result.residual.alignedBrightnessDelta, t.brightnessDeltaWarning, t.brightnessDeltaFail, false, "AlignedBright", 0.06);
    cn(result.residual.alignedEdgeSimilarity, t.edgeWarning, t.edgeFail, "AlignedEdges", 0.08);
    cn(result.residual.alignedHistogramCorrelation, t.histogramWarning, t.histogramFail, "AlignedHistogram", 0.06);
    ck(result.residual.occlusionRatio, t.occlusionWarning, t.occlusionFail, false, "Occlusion", 0.04);

    // SECONDARY: Disparity health
    if (result.disparity.disparityRange > 0.1) {
        ck(result.disparity.invalidRatio, t.disparityInvalidWarning, t.disparityInvalidFail, false, "DispInvalid", 0.05);
        cn(result.disparity.smoothness, t.disparitySmoothnessWarning, t.disparitySmoothnessFail, "DispSmooth", 0.04);
        ck(result.disparity.verticalAsymmetry, t.disparityVertAsymWarning, t.disparityVertAsymFail, false, "DispVertAsym", 0.03);
    }

    // TERTIARY: Asymmetry defects
    ck(result.asymmetry.lightingAsymmetry, t.lightingAsymWarning, t.lightingAsymFail, false, "LightAsym", 0.05);
    ck(result.asymmetry.bloomAsymmetry, t.bloomWarning, t.bloomFail, false, "BloomAsym", 0.04);
    ck(result.asymmetry.shadowAsymmetry, t.shadowWarning, t.shadowFail, false, "ShadowAsym", 0.04);
    ck(result.asymmetry.postProcessAsymmetry, t.postProcessAsymWarning, t.postProcessAsymFail, false, "PPAsym", 0.03);
    ck(result.asymmetry.textureAsymmetry, t.textureAsymWarning, t.textureAsymFail, false, "TextureAsym", 0.03);
    ck(result.asymmetry.chromaticAsymmetry, t.chromaticAsymWarning, t.chromaticAsymFail, false, "ChromeAsym", 0.03);
    ck(result.asymmetry.geometryMissing, t.occlusionWarning, t.occlusionFail, false, "GeoMissing", 0.03);
    ck(result.asymmetry.contrastAsymmetry, t.contrastDeltaWarning, t.contrastDeltaFail, false, "ContrastAsym", 0.02);

    // TEMPORAL
    ck(result.temporal.flickerScore, t.temporalFlickerWarning, t.temporalFlickerFail, false, "Flicker", 0.03);
    cn(result.temporal.temporalStability, t.temporalStabilityWarning, t.temporalStabilityFail, "TempStability", 0.03);
    cn(result.temporal.disparityStability, t.disparityStabilityWarning, t.disparityStabilityFail, "DispStability", 0.02);

    // Match quality penalty
    if (result.matchQuality.confidence > 0.0 && result.matchQuality.confidence < t.matchQualityWarning) {
        score -= (t.matchQualityWarning - result.matchQuality.confidence) / t.matchQualityWarning * 20.0;
        result.issues.push_back("LowCorrConfidence (" +
                                std::to_string(result.matchQuality.confidence) + ")");
    }

    score = std::max(0.0, std::min(100.0, score));
    result.stereoHealthScore = score;

    if (score >= t.passThreshold) result.status = FrameStatus::PASS;
    else if (score >= t.warningThreshold) result.status = FrameStatus::WARNING;
    else result.status = FrameStatus::FAIL;
}

void AnalyzerPipeline::detectIssues(AnalysisResult& result,
                                     const cv::Mat& left, const cv::Mat& warpedRight,
                                     const cv::Mat& occlusionMask) {
    if (left.empty() || warpedRight.empty()) return;
    const auto& chk = m_impl->m_config.checks;
    result.detectedIssues.clear();

    cv::Mat diff;
    cv::absdiff(left, warpedRight, diff);

    cv::Mat grayDiff;
    if (diff.channels() == 3) {
        cv::cvtColor(diff, grayDiff, cv::COLOR_BGR2GRAY);
    } else {
        grayDiff = diff;
    }

    cv::Mat residualMap = grayDiff.clone();
    result.residualDiffMap = residualMap.clone();

    cv::Mat mask;
    cv::threshold(residualMap, mask, 25, 255, cv::THRESH_BINARY);

    // Remove occlusion areas from mask
    if (!occlusionMask.empty()) {
        cv::Mat resizedOcc;
        if (occlusionMask.size() != mask.size()) {
            cv::resize(occlusionMask, resizedOcc, mask.size(), 0, 0, cv::INTER_NEAREST);
        } else {
            resizedOcc = occlusionMask;
        }
        cv::Mat valid;
        cv::bitwise_not(resizedOcc, valid);
        cv::bitwise_and(mask, valid, mask);
    }

    // Morphological cleanup
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN,
                     cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3)));

    cv::Mat labels, stats, centroids;
    int nComponents = cv::connectedComponentsWithStats(mask, labels, stats, centroids, 8);

    const int minArea = 50;
    std::vector<DetectedIssue> rawIssues;
    for (int i = 1; i < nComponents; i++) {
        int area = stats.at<int>(i, cv::CC_STAT_AREA);
        if (area < minArea) continue;

        DetectedIssue issue;
        issue.areaPixels = area;
        issue.boundingBox.x = stats.at<int>(i, cv::CC_STAT_LEFT);
        issue.boundingBox.y = stats.at<int>(i, cv::CC_STAT_TOP);
        issue.boundingBox.width = stats.at<int>(i, cv::CC_STAT_WIDTH);
        issue.boundingBox.height = stats.at<int>(i, cv::CC_STAT_HEIGHT);
        issue.centerX = (float)centroids.at<double>(i, 0);
        issue.centerY = (float)centroids.at<double>(i, 1);

        cv::Rect box(issue.boundingBox.x, issue.boundingBox.y,
                     issue.boundingBox.width, issue.boundingBox.height);
        box &= cv::Rect(0, 0, residualMap.cols, residualMap.rows);
        if (box.width < 2 || box.height < 2) continue;

        cv::Mat regionDiff = residualMap(box);
        cv::Mat regionMask = mask(box);
        double meanDiff = cv::mean(regionDiff, regionMask)[0];
        issue.severity = std::min(1.0f, (float)(meanDiff / 128.0));

        // Extract region crops for classifier
        cv::Mat leftRegion, rightRegion, leftGray, rightGray;
        if (left.channels() == 3) {
            cv::cvtColor(left(box), leftRegion, cv::COLOR_BGR2RGB);
            cv::cvtColor(warpedRight(box), rightRegion, cv::COLOR_BGR2RGB);
            cv::cvtColor(left(box), leftGray, cv::COLOR_BGR2GRAY);
            cv::cvtColor(warpedRight(box), rightGray, cv::COLOR_BGR2GRAY);
        } else {
            leftRegion = left(box).clone();
            rightRegion = warpedRight(box).clone();
            leftGray = left(box);
            rightGray = warpedRight(box);
        }

        // Extract features and classify
        RegionFeatures features;
        features.brightnessDiff = (float)std::abs(cv::mean(leftGray)[0] - cv::mean(rightGray)[0]) / 255.0f;
        cv::Scalar mL, sL, mR, sR;
        cv::meanStdDev(leftGray, mL, sL);
        cv::meanStdDev(rightGray, mR, sR);
        features.contrastDiff = (float)std::abs(sL[0] - sR[0]) / 255.0f;
        features.meanLuminanceL = (float)mL[0] / 255.0f;
        features.meanLuminanceR = (float)mR[0] / 255.0f;
        features.stdLuminanceL = (float)sL[0] / 255.0f;
        features.stdLuminanceR = (float)sR[0] / 255.0f;
        features.meanDiff = (float)meanDiff / 255.0f;
        features.regionSize = (float)(box.width * box.height);
        features.regionAspectRatio = box.height > 0 ? (float)box.width / (float)box.height : 1.0f;
        features.regionPosX = (float)box.x / (float)std::max(left.cols, 1);
        features.regionPosY = (float)box.y / (float)std::max(left.rows, 1);

        // Use the classifier (if enabled) or create a generic issue
        DetectedIssue classified;
        if (chk.issueClassification) {
            ClassificationEvidence ev = m_classifier.extractEvidence(
                leftRegion, rightRegion, leftGray, rightGray, residualMap, box,
                left.cols, left.rows);

            // Copy evidence into features
            features.colorDiff = ev.colorDiff;
            features.edgeSimilarity = ev.edgeSimilarity;
            features.textureSimilarity = ev.textureSimilarity;
            features.gradientConsistency = ev.gradientConsistency;
            features.histogramSimilarity = ev.histogramSimilarity;
            features.bloomRatio = ev.meanDiff > 0.15f ? ev.meanDiff : 0.0f;
            features.edgeRatio = ev.edgeRatio;
            features.nearBorder = ev.nearBorder;
            features.nearCenter = ev.nearCenter;
            features.hasColor = ev.hasColor;

            double totalPixD = (double)leftGray.total();
            if (totalPixD > 0) {
                double eL = (double)cv::countNonZero(leftGray > 30) / totalPixD;
                double eR = (double)cv::countNonZero(rightGray > 30) / totalPixD;
                features.leftContentDensity = (float)eL;
                features.rightContentDensity = (float)eR;
            }

            // Canny edges for edge ratio
            cv::Mat edgesL, edgesR;
            cv::Canny(leftGray, edgesL, 50.0, 150.0);
            cv::Canny(rightGray, edgesR, 50.0, 150.0);
            double countL = (double)cv::countNonZero(edgesL);
            double countR = (double)cv::countNonZero(edgesR);
            features.edgeRatio = totalPixD > 0 ? (float)(std::abs(countL - countR) / totalPixD) : 0.0f;

            classified = m_classifier.classify(features);
        } else {
            // No classifier: use a generic classification
            classified.type = IssueType::LowConfidence;
            classified.confidence = std::min(1.0f, (float)(features.meanDiff * 2.0f));
            classified.evidence = ClassificationEvidence{};
        }
        classified.boundingBox = issue.boundingBox;
        classified.areaPixels = issue.areaPixels;
        classified.severity = issue.severity;
        classified.centerX = issue.centerX;
        classified.centerY = issue.centerY;

        // Compute disparity consistency from result
        if (!result.disparity.disparityMap.empty() && !result.disparity.validMap.empty()) {
            cv::Rect dBbox(box);
            dBbox &= cv::Rect(0, 0, result.disparity.disparityMap.cols, result.disparity.disparityMap.rows);
            if (dBbox.width > 2 && dBbox.height > 2) {
                cv::Mat dispROI = result.disparity.disparityMap(dBbox);
                cv::Mat validROI = result.disparity.validMap(dBbox);
                cv::Scalar dm = cv::mean(dispROI, validROI);
                double validPix = (double)cv::countNonZero(validROI);
                double totalPixDisp = (double)validROI.total();
                float validRatio = totalPixDisp > 0 ? (float)(validPix / totalPixDisp) : 0.0f;
                classified.evidence.disparityConsistency = validRatio;
                if (dm[0] > 0) {
                    float dispStd = (float)dm[0];
                    classified.evidence.disparityConsistency = std::min(1.0f, 1.0f / (1.0f + dispStd * 0.1f));
                }
            }
        }

        rawIssues.push_back(classified);
    }

    // Merge overlapping regions
    if (chk.issueMerging) {
        rawIssues = m_merger.merge(rawIssues);
    }

    // Filter and sort
    RegionMerger::Config mergerCfg = m_merger.config();
    std::vector<DetectedIssue> filtered = m_merger.filterInvalid(
        rawIssues, left.cols, left.rows, result.disparity.validMap);

    for (auto& iss : filtered) {
        if (!iss.isInvalidRegion) {
            result.detectedIssues.push_back(iss);
        }
    }

    std::sort(result.detectedIssues.begin(), result.detectedIssues.end(),
              [](const DetectedIssue& a, const DetectedIssue& b) {
                  return a.severity > b.severity;
              });
}
