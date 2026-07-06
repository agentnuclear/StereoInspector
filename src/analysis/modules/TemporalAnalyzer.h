#pragma once
#include "analysis/IStereoAnalyzer.h"
#include "core/Types.h"
#include <deque>
#include <mutex>

class TemporalAnalyzer : public BaseAnalyzer {
public:
    TemporalAnalyzer();
    ~TemporalAnalyzer() = default;

    void analyze(const cv::Mat& leftEye, const cv::Mat& rightEye,
                 AnalysisResult& result) override;

    // Accept previous results for temporal tracking
    void pushFrame(const AnalysisResult& result);
    void reset();

private:
    struct FrameSnapshot {
        double meanLuminance = 0.0;
        double disparityMean = 0.0;
        double healthScore = 100.0;
        cv::Mat prevLeftGray;
        cv::Mat prevRightGray;
    };

    std::deque<FrameSnapshot> m_history;
    mutable std::mutex m_mutex;
    static constexpr size_t MAX_FRAMES = 60;
};
