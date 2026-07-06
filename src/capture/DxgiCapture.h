#pragma once
#include "core/Frame.h"
#include "config/Config.h"
#include "analysis/modules/StereoDetector.h"
#include <atomic>
#include <memory>
#include <thread>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class DxgiCapture {
public:
    explicit DxgiCapture(const AppConfig& config);
    ~DxgiCapture();

    bool initialize();
    void start(LatestFrameBuffer& frameBuffer);
    void stop();
    bool isRunning() const;
    int width() const;
    int height() const;
    StereoLayout currentLayout() const;
    void resetDetection();

private:
    void captureLoop(LatestFrameBuffer& frameBuffer);
    bool initDxgi();
    void splitFrame(const cv::Mat& frame, cv::Mat& leftEye, cv::Mat& rightEye,
                    StereoDetectionResult& detResult);
    cv::Mat dxgiToMat(IDXGIResource* resource);

    struct Impl;
    std::unique_ptr<Impl> m_impl;
    AppConfig m_config;
    std::atomic<bool> m_running{false};
    std::thread m_worker;

    ComPtr<ID3D11Device> m_device;
    ComPtr<ID3D11DeviceContext> m_context;
    ComPtr<IDXGIOutputDuplication> m_dup;
    ComPtr<IDXGIOutput1> m_output1;
    int m_width = 0;
    int m_height = 0;

    StereoDetector m_stereoDetector;
    StereoDetectionResult m_lastDetection;
};
