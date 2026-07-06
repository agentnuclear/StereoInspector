#include "DxgiCapture.h"
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <spdlog/spdlog.h>

struct DxgiCapture::Impl {
    int64_t frameNumber = 0;
    int detectionInterval = 30; // re-detect stereo layout every N frames
};

DxgiCapture::DxgiCapture(const AppConfig& config)
    : m_config(config), m_impl(std::make_unique<Impl>()) {}

DxgiCapture::~DxgiCapture() {
    stop();
}

bool DxgiCapture::initDxgi() {
    HRESULT hr;

    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1 };
    D3D_FEATURE_LEVEL outLevel;

    hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
        featureLevels, 2, D3D11_SDK_VERSION,
        &m_device, &outLevel, &m_context
    );

    if (FAILED(hr)) {
        spdlog::error("D3D11CreateDevice failed: {:08x}", hr);
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = m_device.As(&dxgiDevice);
    if (FAILED(hr)) {
        spdlog::error("QI for IDXGIDevice failed: {:08x}", hr);
        return false;
    }

    ComPtr<IDXGIAdapter> adapter;
    hr = dxgiDevice->GetAdapter(&adapter);
    if (FAILED(hr)) {
        spdlog::error("GetAdapter failed: {:08x}", hr);
        return false;
    }

    ComPtr<IDXGIOutput> output;
    hr = adapter->EnumOutputs(m_config.captureOutput, &output);
    if (FAILED(hr)) {
        spdlog::error("EnumOutputs failed: {:08x}", hr);
        return false;
    }

    ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr)) {
        spdlog::error("QI for IDXGIOutput1 failed: {:08x}", hr);
        return false;
    }

    hr = output1->DuplicateOutput(m_device.Get(), &m_dup);
    if (FAILED(hr)) {
        spdlog::error("DuplicateOutput failed: {:08x}", hr);
        return false;
    }

    DXGI_OUTPUT_DESC outputDesc;
    output->GetDesc(&outputDesc);
    m_width = (int)outputDesc.DesktopCoordinates.right - outputDesc.DesktopCoordinates.left;
    m_height = (int)outputDesc.DesktopCoordinates.bottom - outputDesc.DesktopCoordinates.top;

    spdlog::info("DXGI capture initialized: {}x{}", m_width, m_height);
    return true;
}

cv::Mat DxgiCapture::dxgiToMat(IDXGIResource* resource) {
    ComPtr<ID3D11Texture2D> tex;
    HRESULT hr = resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
    if (FAILED(hr)) return cv::Mat();

    D3D11_TEXTURE2D_DESC desc;
    tex->GetDesc(&desc);

    D3D11_TEXTURE2D_DESC stagingDesc = {};
    stagingDesc.Width = desc.Width;
    stagingDesc.Height = desc.Height;
    stagingDesc.MipLevels = 1;
    stagingDesc.ArraySize = 1;
    stagingDesc.Format = desc.Format;
    stagingDesc.SampleDesc.Count = 1;
    stagingDesc.Usage = D3D11_USAGE_STAGING;
    stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

    ComPtr<ID3D11Texture2D> staging;
    hr = m_device->CreateTexture2D(&stagingDesc, nullptr, &staging);
    if (FAILED(hr)) return cv::Mat();

    m_context->CopyResource(staging.Get(), tex.Get());

    D3D11_MAPPED_SUBRESOURCE mapped;
    hr = m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) return cv::Mat();

    cv::Mat mat(desc.Height, desc.Width, CV_8UC4);
    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
    uint8_t* dst = mat.data;

    for (UINT y = 0; y < desc.Height; y++) {
        memcpy(dst + y * mat.step, src + y * mapped.RowPitch, desc.Width * 4);
    }

    m_context->Unmap(staging.Get(), 0);

    cv::cvtColor(mat, mat, cv::COLOR_BGRA2BGR);
    return mat;
}

void DxgiCapture::splitFrame(const cv::Mat& frame, cv::Mat& leftEye, cv::Mat& rightEye,
                              StereoDetectionResult& detResult) {
    if (frame.empty()) return;

    const auto& region = m_config.stereoRegion;

    if (!region.autoSplit) {
        // Manual regions from config
        leftEye = frame(cv::Rect(region.x, region.y, region.width, region.height)).clone();
        rightEye = frame(cv::Rect(region.x + region.width + 1, region.y,
                                   region.width, region.height)).clone();
        detResult.layout = StereoLayout::Custom;
        detResult.leftRect = cv::Rect(region.x, region.y, region.width, region.height);
        detResult.rightRect = cv::Rect(region.x + region.width + 1, region.y,
                                        region.width, region.height);
        detResult.confidence = 1.0;
        return;
    }

    // Use stereo detector for smart splitting
    detResult = m_stereoDetector.detect(frame);

    leftEye = frame(detResult.leftRect).clone();
    rightEye = frame(detResult.rightRect).clone();
}

void DxgiCapture::resetDetection() {
    m_impl->frameNumber = 0;
}

StereoLayout DxgiCapture::currentLayout() const {
    return m_lastDetection.layout;
}

void DxgiCapture::start(LatestFrameBuffer& frameBuffer) {
    if (m_running.exchange(true)) return;

    if (!initDxgi()) {
        spdlog::error("Failed to initialize DXGI capture");
        m_running = false;
        return;
    }

    m_worker = std::thread(&DxgiCapture::captureLoop, this, std::ref(frameBuffer));
}

void DxgiCapture::stop() {
    m_running = false;
    if (m_worker.joinable()) {
        m_worker.join();
    }
    if (m_dup) {
        m_dup->ReleaseFrame();
    }
}

bool DxgiCapture::isRunning() const {
    return m_running.load();
}

int DxgiCapture::width() const { return m_width; }
int DxgiCapture::height() const { return m_height; }

void DxgiCapture::captureLoop(LatestFrameBuffer& frameBuffer) {
    spdlog::info("DXGI capture loop started");

    auto lastTime = std::chrono::steady_clock::now();
    int frameCount = 0;

    while (m_running.load()) {
        ComPtr<IDXGIResource> resource;
        DXGI_OUTDUPL_FRAME_INFO frameInfo;

        HRESULT hr = m_dup->AcquireNextFrame(16, &frameInfo, &resource);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (FAILED(hr)) {
            if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_DEVICE_REMOVED) {
                spdlog::warn("DXGI device lost, reinitializing...");
                m_dup->ReleaseFrame();
                if (initDxgi()) continue;
                break;
            }
            m_dup->ReleaseFrame();
            continue;
        }

        if (frameInfo.LastPresentTime.QuadPart != 0) {
            cv::Mat frame = dxgiToMat(resource.Get());
            if (!frame.empty()) {
                auto captureFrame = std::make_shared<CaptureFrame>();
                captureFrame->frame = frame.clone();
                captureFrame->width = frame.cols;
                captureFrame->height = frame.rows;
                captureFrame->frameNumber = ++m_impl->frameNumber;
                captureFrame->captureTime = std::chrono::steady_clock::now();

                // Detect and split stereo
                StereoDetectionResult detResult;
                splitFrame(frame, captureFrame->leftEye, captureFrame->rightEye, detResult);
                captureFrame->detectedLayout = detResult.layout;
                captureFrame->splitPoint = detResult.splitPoint;
                m_lastDetection = detResult;

                frameBuffer.store(captureFrame);
                frameCount++;
            }
        }

        m_dup->ReleaseFrame();

        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - lastTime).count();
        if (elapsed >= 1.0) {
            if (frameCount > 0) {
                m_captureFps.store(frameCount / elapsed);
                spdlog::debug("Capture FPS: {:.1f}", m_captureFps.load());
            }
            frameCount = 0;
            lastTime = now;
        }
    }

    spdlog::info("DXGI capture loop stopped");
}
