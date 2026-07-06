#pragma once
#include "core/Types.h"
#include <string>
#include <opencv2/core.hpp>

class IStereoAnalyzer {
public:
    virtual ~IStereoAnalyzer() = default;
    virtual std::string name() const = 0;
    virtual void analyze(const cv::Mat& leftEye, const cv::Mat& rightEye, AnalysisResult& result) = 0;
    virtual bool enabled() const { return true; }
    virtual void setEnabled(bool enabled) { (void)enabled; }
};

class BaseAnalyzer : public IStereoAnalyzer {
public:
    BaseAnalyzer(std::string name, bool enabled = true);
    std::string name() const override;
    bool enabled() const override;
    void setEnabled(bool enabled) override;

protected:
    std::string m_name;
    bool m_enabled;
};
