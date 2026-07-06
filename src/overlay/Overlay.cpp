#include "Overlay.h"
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>
#include <windowsx.h>
#include <spdlog/spdlog.h>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

Overlay::Overlay() = default;
Overlay::~Overlay() { shutdown(); }

bool Overlay::initialize(HINSTANCE hInstance, int nCmdShow) {
    m_hinstance = hInstance;
    if (!createWindow(hInstance)) { spdlog::error("Failed to create overlay window"); return false; }
    if (!createD3D11Device()) { spdlog::error("Failed to create D3D11 device"); return false; }
    if (!createSwapChain()) { spdlog::error("Failed to create swap chain"); return false; }
    if (!initImGui()) { spdlog::error("Failed to initialize ImGui"); return false; }

    ShowWindow(m_hwnd, nCmdShow);
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    m_initialized = true;
    spdlog::info("Overlay initialized");
    return true;
}

void Overlay::shutdown() {
    m_initialized = false;
    if (m_d3dContext) m_d3dContext->ClearState();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    cleanupD3D();
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = nullptr; }
}

bool Overlay::isInitialized() const { return m_initialized.load(); }

void Overlay::setResultCallback(ResultCallback resultFn) { m_getResult = std::move(resultFn); }
void Overlay::setTimeCallback(TimeCallback timeFn) { m_getTime = std::move(timeFn); }
void Overlay::setFrameCallback(FrameCallback frameFn) { m_getFrame = std::move(frameFn); }
void Overlay::setHistoryCallback(HistoryCallback historyFn) { m_getHistory = std::move(historyFn); }
void Overlay::setLayoutCallback(LayoutCallback layoutFn) { m_getLayout = std::move(layoutFn); }
void Overlay::setSyncCallback(SyncCallback syncFn) { m_doSync = std::move(syncFn); }
void Overlay::setClearBaselineCallback(ClearBaselineCallback clearFn) { m_doClearBaseline = std::move(clearFn); }
void Overlay::setConfigCallback(ConfigCallback cb) { m_updateChecks = std::move(cb); }
void Overlay::setGetConfigCallback(GetConfigCallback cb) { m_getChecks = std::move(cb); }
void Overlay::setVisualizationMode(VisualizationMode mode) { m_vizMode = mode; }
void Overlay::setCaptureSafe(bool safe) { m_captureSafe = safe; }
bool Overlay::isCaptureSafe() const { return m_captureSafe.load(); }

void Overlay::setVisible(bool visible) {
    m_visible = visible;
    ShowWindow(m_hwnd, visible ? SW_SHOW : SW_HIDE);
}
void Overlay::setFrozen(bool frozen) { m_frozen = frozen; }

void Overlay::showSyncFeedback(uint64_t frameNumber, float confidence, const std::string& timestamp) {
    m_syncFeedback.show = true;
    m_syncFeedback.timer = 5.0f;
    m_syncFeedback.frameNumber = frameNumber;
    m_syncFeedback.confidence = confidence;
    m_syncFeedback.timestamp = timestamp;
}
void Overlay::showSyncRefused(const std::string& reason) {
    m_syncRefused.show = true;
    m_syncRefused.timer = 5.0f;
    m_syncRefused.reason = reason;
}

void Overlay::resetLayout() {
    if (ImGui::GetIO().IniFilename) {
        std::remove(ImGui::GetIO().IniFilename);
    }
    m_selectedIssueIndex = -1;
    spdlog::info("Layout reset");
}

bool Overlay::isVisible() const { return m_visible.load(); }
bool Overlay::isFrozen() const { return m_frozen.load(); }
VisualizationMode Overlay::visualizationMode() const { return m_vizMode.load(); }

bool Overlay::createWindow(HINSTANCE hInstance) {
    m_screenWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    m_screenHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = "StereoInspectorOverlay";

    if (!RegisterClassEx(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            spdlog::error("Failed to register overlay window class: {}", err);
            return false;
        }
    }

    int virtLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int virtTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    m_hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        "StereoInspectorOverlay", "Stereo Inspector",
        WS_POPUP, virtLeft, virtTop, m_screenWidth, m_screenHeight,
        nullptr, nullptr, hInstance, this
    );
    if (!m_hwnd) { spdlog::error("Failed to create overlay window: {}", GetLastError()); return false; }

    SetLayeredWindowAttributes(m_hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
    SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);
    return true;
}

bool Overlay::createD3D11Device() {
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL out;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                    levels, 3, D3D11_SDK_VERSION, &m_d3dDevice, &out, &m_d3dContext);
    return SUCCEEDED(hr);
}

bool Overlay::createSwapChain() {
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = m_screenWidth;
    sd.BufferDesc.Height = m_screenHeight;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 144;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = m_hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    ComPtr<IDXGIDevice> dxgiDev;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory> factory;
    HRESULT hr = m_d3dDevice.As(&dxgiDev);
    if (FAILED(hr) || FAILED(dxgiDev->GetAdapter(&adapter)) ||
        FAILED(adapter->GetParent(IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateSwapChain(m_d3dDevice.Get(), &sd, &m_swapChain)) ||
        FAILED(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&m_backBuffer))) ||
        FAILED(m_d3dDevice->CreateRenderTargetView(m_backBuffer.Get(), nullptr, &m_rtView)))
        return false;
    return true;
}

bool Overlay::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = "stereo_inspector_layout.ini";
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NavNoCaptureKeyboard;
    HDC hdc = GetDC(m_hwnd);
    float dpi = (float)GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(m_hwnd, hdc);
    io.FontGlobalScale = dpi / 96.0f;
    ImGui::StyleColorsDark();

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.WindowMinSize = ImVec2(240, 80);
    style.WindowMenuButtonPosition = ImGuiDir_None;

    if (!ImGui_ImplWin32_Init(m_hwnd)) return false;
    if (!ImGui_ImplDX11_Init(m_d3dDevice.Get(), m_d3dContext.Get())) return false;
    return true;
}

void Overlay::cleanupD3D() {
    m_vizSRV.Reset();
    m_vizTexture.Reset();
    m_rtView.Reset();
    m_backBuffer.Reset();
    m_swapChain.Reset();
    m_d3dContext.Reset();
    m_d3dDevice.Reset();
}

// ---------------------------------------------------------------------------
// Mini graph helper (unchanged)
// ---------------------------------------------------------------------------
void Overlay::renderMiniGraph(const char* label, const HistoryBuffer& data,
                               float graphWidth, float graphHeight,
                               float minVal, float maxVal,
                               unsigned int lineColor) {
    if (data.empty()) { ImGui::Text("%s: no data", label); return; }
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + graphWidth, pos.y + graphHeight),
                      IM_COL32(20, 20, 30, 180), 4.0f);
    dl->PushClipRect(pos, ImVec2(pos.x + graphWidth, pos.y + graphHeight));
    float range = maxVal - minVal;
    if (range < 0.001f) range = 1.0f;
    size_t n = data.size();
    for (int i = 0; i <= 4; i++) {
        float y = pos.y + graphHeight * (1.0f - i / 4.0f);
        dl->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + graphWidth, y), IM_COL32(60, 60, 80, 80));
    }
    std::vector<ImVec2> pts;
    for (size_t i = 0; i < n; i++) {
        float x = pos.x + (float)i / (float)std::max(n - 1, (size_t)1) * graphWidth;
        float y = pos.y + graphHeight * (1.0f - (float)((data[i] - minVal) / range));
        pts.emplace_back(x, y);
    }
    if (pts.size() >= 2) {
        std::vector<ImVec2> fill = pts;
        fill.insert(fill.begin(), ImVec2(pts[0].x, pos.y + graphHeight));
        fill.push_back(ImVec2(pts.back().x, pos.y + graphHeight));
        dl->AddPolyline(fill.data(), (int)fill.size(), IM_COL32(255, 255, 255, 30), ImDrawFlags_Closed, 1.0f);
        dl->AddPolyline(pts.data(), (int)pts.size(), lineColor, 0, 2.0f);
        dl->AddCircleFilled(pts.back(), 3.0f, lineColor);
    }
    dl->PopClipRect();
    double cur = data.back();
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + graphHeight + 4));
    ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s: %.2f", label, cur);
}

void Overlay::renderGraphPanel(const MetricHistory& history) {
    float aw = ImGui::GetContentRegionAvail().x;
    float gw = (aw - ImGui::GetStyle().ItemSpacing.x) / 2.0f;
    float gh = 65.0f;
    renderMiniGraph("Health", history.healthScore, gw, gh, 0.0f, 100.0f, IM_COL32(76, 175, 80, 220));
    ImGui::SameLine();
    renderMiniGraph("SSIM", history.ssim, gw, gh, 0.0f, 1.0f, IM_COL32(33, 150, 243, 220));
    renderMiniGraph("Disp", history.disparityMean, gw, gh, 0.0f, 200.0f, IM_COL32(255, 152, 0, 220));
    ImGui::SameLine();
    renderMiniGraph("Bright", history.brightnessDelta, gw, gh, 0.0f, 1.0f, IM_COL32(156, 39, 176, 220));
    // Temporal metrics row
    renderMiniGraph("Stability", history.temporalStability, gw, gh, 0.0f, 1.0f, IM_COL32(0, 230, 200, 220));
    ImGui::SameLine();
    renderMiniGraph("AlignSSIM", history.alignmentSSIM, gw, gh, 0.0f, 1.0f, IM_COL32(100, 200, 255, 220));
    renderMiniGraph("Flicker", history.flickerScore, gw, gh, 0.0f, 1.0f, IM_COL32(255, 100, 100, 220));
    ImGui::SameLine();
    renderMiniGraph("DispStab", history.disparityStability, gw, gh, 0.0f, 1.0f, IM_COL32(200, 180, 60, 220));
}

void Overlay::renderModeButtons(VisualizationMode currentMode) {
    float aw = ImGui::GetContentRegionAvail().x;
    int cols = 5;
    float bw = (aw - ImGui::GetStyle().ItemSpacing.x * (cols - 1)) / cols;

    auto btn = [&](VisualizationMode m, const char* label, const char* tip) {
        bool act = (currentMode == m);
        if (act) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.44f, 0.73f, 0.8f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.17f, 0.52f, 0.82f, 0.9f));
        }
        if (ImGui::Button(label, ImVec2(bw, 26))) { m_vizMode = m; }
        if (act) ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s (%s)", label, tip);
    };

    btn(VisualizationMode::Normal, "Normal", "F1");
    ImGui::SameLine();
    btn(VisualizationMode::StereoDifferenceOverlay, "Diff Ov", "F3");
    ImGui::SameLine();
    btn(VisualizationMode::DifferenceHeatmap, "Heatmap", "F4");
    ImGui::SameLine();
    btn(VisualizationMode::EdgeComparison, "Edges", "Ctrl+F1");
    ImGui::SameLine();
    btn(VisualizationMode::DisparityHeatmap, "Disp", "Ctrl+F7");
    btn(VisualizationMode::FeatureMatchOverlay, "Feat", "Ctrl+F2");
    ImGui::SameLine();
    btn(VisualizationMode::HistogramView, "Hist", "Ctrl+F3");
    ImGui::SameLine();
    btn(VisualizationMode::BlurMap, "Blur", "Ctrl+F4");
    ImGui::SameLine();
    btn(VisualizationMode::BlinkLeft, "L", "Ctrl+F5");
    ImGui::SameLine();
    btn(VisualizationMode::BlinkRight, "R", "Ctrl+F6");
}

// ---------------------------------------------------------------------------
// Texture upload for visualization
// ---------------------------------------------------------------------------
void Overlay::updateVizTexture(const cv::Mat& image) {
    if (image.empty()) return;
    cv::Mat rgba;
    if (image.channels() == 3) cv::cvtColor(image, rgba, cv::COLOR_BGR2RGBA);
    else if (image.channels() == 1) cv::cvtColor(image, rgba, cv::COLOR_GRAY2RGBA);
    else rgba = image;

    int w = rgba.cols, h = rgba.rows;
    if (!m_vizTexture || m_vizTexWidth != w || m_vizTexHeight != h) {
        m_vizSRV.Reset();
        m_vizTexture.Reset();
        D3D11_TEXTURE2D_DESC td = {};
        td.Width = w; td.Height = h; td.MipLevels = 1; td.ArraySize = 1;
        td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        D3D11_SUBRESOURCE_DATA sd = {};
        sd.pSysMem = rgba.data; sd.SysMemPitch = w * 4;
        HRESULT hr = m_d3dDevice->CreateTexture2D(&td, &sd, &m_vizTexture);
        if (SUCCEEDED(hr)) {
            D3D11_SHADER_RESOURCE_VIEW_DESC sv = {};
            sv.Format = td.Format; sv.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            sv.Texture2D.MipLevels = 1;
            m_d3dDevice->CreateShaderResourceView(m_vizTexture.Get(), &sv, &m_vizSRV);
            m_vizTexWidth = w; m_vizTexHeight = h;
        }
    } else {
        m_d3dContext->UpdateSubresource(m_vizTexture.Get(), 0, nullptr, rgba.data, w * 4, 0);
    }
}

// ---------------------------------------------------------------------------
// Sync feedback popup (rendered once per frame, not inside a window)
// ---------------------------------------------------------------------------
void Overlay::renderSyncFeedback() {
    ImGuiIO& io = ImGui::GetIO();
    if (m_syncFeedback.show) {
        m_syncFeedback.timer -= io.DeltaTime;
        float alpha = std::min(1.0f, m_syncFeedback.timer / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        ImGui::Begin("###SyncCaptured", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text("Stereo Model Captured");
        ImGui::Text("Frame: %llu  Conf: %.2f", (unsigned long long)m_syncFeedback.frameNumber, m_syncFeedback.confidence);
        ImGui::Text("Time: %s", m_syncFeedback.timestamp.c_str());
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (m_syncFeedback.timer <= 0.0f) m_syncFeedback.show = false;
    }
    if (m_syncRefused.show) {
        m_syncRefused.timer -= io.DeltaTime;
        float alpha = std::min(1.0f, m_syncRefused.timer / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.2f, 1.0f));
        ImGui::Begin("###SyncRefused", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text("Baseline Capture Refused");
        ImGui::TextWrapped("%s", m_syncRefused.reason.c_str());
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (m_syncRefused.timer <= 0.0f) m_syncRefused.show = false;
    }
}

// ===========================================================================
// Consolidated UI rendering
// ===========================================================================
void Overlay::renderWindows() {
    if (!m_visible.load()) return;

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    renderSyncFeedback();

    AnalysisResult result;
    FrameTime ft;
    MetricHistory history;
    if (m_getResult) result = m_getResult();
    if (m_getTime) ft = m_getTime();
    if (m_getHistory) history = m_getHistory();

    renderHubWindow(result, ft, history);
    renderVisualizationWindow();

    ImGui::Render();
}

// ---------------------------------------------------------------------------
// Hub Window – tabbed main interface
// ---------------------------------------------------------------------------
void Overlay::renderHubWindow(const AnalysisResult& result, const FrameTime& ft, const MetricHistory& history) {
    ImGui::SetNextWindowSize(ImVec2(380, 320), ImGuiCond_FirstUseEver);
    ImGui::Begin("Stereo Inspector", nullptr, ImGuiWindowFlags_NoScrollbar);

    if (ImGui::BeginTabBar("HubTabs")) {
        if (ImGui::BeginTabItem("Summary")) {
            renderSummaryTab(result, ft);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Status")) {
            renderStatusTab(result, ft);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Metrics")) {
            renderMetricsTab(result);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Issues")) {
            renderIssuesTab(result);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Checks")) {
            renderChecksTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Graphs")) {
            renderGraphsTab(history);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Summary Tab – at-a-glance key indicators
// ---------------------------------------------------------------------------
void Overlay::renderSummaryTab(const AnalysisResult& result, const FrameTime& ft) {
    // Status row
    ImVec4 sc;
    const char* statusLabel = "";
    switch (result.stereoStatus) {
        case StereoStatus::SAFE:    sc = ImVec4(0.3f,0.85f,0.3f,1.0f); statusLabel="SAFE"; break;
        case StereoStatus::WARNING: sc = ImVec4(1.0f,0.6f,0.0f,1.0f); statusLabel="WARN"; break;
        case StereoStatus::DESYNC:  sc = ImVec4(1.0f,0.2f,0.2f,1.0f); statusLabel="DESYNC"; break;
        case StereoStatus::UNKNOWN: sc = ImVec4(0.5f,0.5f,0.5f,1.0f); statusLabel="?"; break;
    }
    ImGui::Text("Status:"); ImGui::SameLine(80);
    ImGui::TextColored(sc, "%s", statusLabel);

    // Integrity
    ImGui::Text("Integrity:"); ImGui::SameLine(80);
    ImVec4 ic = result.stereoIntegrityScore >= 80.0f ? ImVec4(0.3f,0.85f,0.3f,1.0f) :
                result.stereoIntegrityScore >= 50.0f ? ImVec4(1.0f,0.6f,0.0f,1.0f) :
                ImVec4(1.0f,0.2f,0.2f,1.0f);
    ImGui::TextColored(ic, "%.0f / 100", result.stereoIntegrityScore);

    // Scene confidence
    ImGui::Text("Scene:"); ImGui::SameLine(80);
    ImVec4 ssc = result.sceneConfidence.reliable ? ImVec4(0.4f,0.7f,1.0f,1.0f) : ImVec4(0.6f,0.6f,0.6f,1.0f);
    ImGui::TextColored(ssc, "%.0f%%", result.sceneConfidence.overall * 100.0);

    // FPS
    ImGui::Text("FPS:"); ImGui::SameLine(80);
    ImVec4 fpsc = ft.fps >= 60.0f ? ImVec4(0.3f,0.85f,0.3f,1.0f) :
                  ft.fps >= 30.0f ? ImVec4(1.0f,0.6f,0.0f,1.0f) :
                  ImVec4(1.0f,0.2f,0.2f,1.0f);
    ImGui::TextColored(fpsc, "%.1f", ft.fps);

    // Frame number
    ImGui::Text("Frame:"); ImGui::SameLine(80);
    ImGui::Text("%lld", (long long)result.frameNumber);

    // Analysis vs capture
    ImGui::Text("Analysis:"); ImGui::SameLine(80);
    ImGui::Text("%.1f fps / %.1f ms", ft.analysisFps, ft.analysisMs);
    ImGui::Text("Capture:"); ImGui::SameLine(80);
    ImGui::Text("%.1f fps / %.1f ms", ft.captureFps, ft.captureMs);

    ImGui::Separator();

    // Baseline status
    if (result.stereoModel.active) {
        ImGui::TextColored(ImVec4(0.3f,0.85f,0.3f,1.0f), "Baseline Locked");
        ImGui::Text("  Frame:  #%llu", (unsigned long long)result.stereoModel.captureFrameNumber);
        auto elapsed = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - result.stereoModel.captureTimePoint).count();
        ImGui::Text("  Age:    %.0fs", elapsed);
        ImGui::Text("  Tracked: %llu", (unsigned long long)result.stereoModel.framesCompared);
    } else {
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f), "No baseline \u2014 sync to enable comparison");
    }

    ImGui::Separator();

    // Visualization mode
    const char* modeName = VisualizationModeName(m_vizMode.load());
    ImGui::Text("Viz Mode:"); ImGui::SameLine(80);
    ImGui::TextColored(ImVec4(0.6f,0.6f,0.8f,1.0f), "%s", modeName);

    // Issue count
    int totalIssues = (int)(result.deviations.size() + result.detectedIssues.size());
    if (totalIssues > 0) {
        ImGui::Text("Issues:"); ImGui::SameLine(80);
        ImVec4 ic2 = totalIssues > 5 ? ImVec4(1.0f,0.3f,0.3f,1.0f) : ImVec4(1.0f,0.6f,0.0f,1.0f);
        ImGui::TextColored(ic2, "%d (%zu deviations, %zu region)",
            totalIssues, result.deviations.size(), result.detectedIssues.size());
    } else {
        ImGui::Text("Issues:"); ImGui::SameLine(80);
        ImGui::TextColored(ImVec4(0.3f,0.85f,0.3f,1.0f), "None");
    }
}

// ---------------------------------------------------------------------------
// Status Tab – overview + controls (L1/L2/L3 progressive disclosure)
// ---------------------------------------------------------------------------
void Overlay::renderStatusTab(const AnalysisResult& result, const FrameTime& ft) {
    // Level 1: critical essentials (always visible)
    // Level 2: detailed metrics (collapsible)
    // Level 3: deep debug (collapsible)

    // Controls area at top
    renderControlsArea(result);

    ImGui::Separator();

    // -- Level 2: Frame Timing & Details --
    if (ImGui::CollapsingHeader("Frame Details", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Frame:    %lld", (long long)result.frameNumber);
        ImGui::Text("Capture:  %.1f fps / %.1f ms", ft.captureFps, ft.captureMs);
        ImGui::Text("Analysis: %.1f fps / %.1f ms", ft.analysisFps, ft.analysisMs);

        if (result.stereoModel.active) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.3f,0.85f,0.3f,1.0f), "Baseline Locked");
            ImGui::Text("Frame:  #%llu", (unsigned long long)result.stereoModel.captureFrameNumber);
            auto elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - result.stereoModel.captureTimePoint).count();
            ImGui::Text("Age:    %.0fs", elapsed);
            ImGui::Text("Tracked: %llu", (unsigned long long)result.stereoModel.framesCompared);
        } else {
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f), "No baseline — sync to enable comparison");
        }
    }

    // -- Level 3: Raw Details (debug) --
    if (ImGui::CollapsingHeader("Raw Details")) {
        ImGui::Text("ORB Matches:  %d", result.featureMatchCount);
        ImGui::Text("Opt Flow:     %.2f", result.opticalFlowMagnitude);
        ImGui::Text("Blur \u0394:      %.4f", result.blurDelta);
        ImGui::Text("Contrast \u0394:  %.4f", result.contrastDelta);
        ImGui::Text("Lens Dist:    %.4f", result.lensDistortionDelta);
        ImGui::Text("Foveation:    %.4f", result.foveationAsymmetry);
        ImGui::Text("God Rays:     %.4f", result.godRayDifference);
        ImGui::Separator();
        ImGui::Text("Disparity Range: %.1f", result.disparity.disparityRange);
        ImGui::Text("Disp Invalid:    %.1f%%", result.disparity.invalidRatio * 100.0);
        ImGui::Text("Disp Smoothness: %.3f", result.disparity.smoothness);
        ImGui::Text("Corr Confidence: %.2f", result.matchQuality.confidence);
        ImGui::Text("Temporal Stable: %.3f", result.temporal.temporalStability);
    }
}

// ---------------------------------------------------------------------------
// Controls area (within Status tab)
// ---------------------------------------------------------------------------
void Overlay::renderControlsArea(const AnalysisResult& result) {
    bool hasBase = result.stereoModel.active;
    float bw = ImGui::GetContentRegionAvail().x;

    // Baseline management row
    if (hasBase) {
        ImGui::TextColored(ImVec4(0.3f,0.85f,0.3f,1.0f), "Baseline: Locked");
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear")) {
            if (m_doClearBaseline) m_doClearBaseline();
        }
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f,0.58f,0.25f,0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.17f,0.68f,0.30f,0.9f));
    if (ImGui::Button(hasBase ? "Resync" : "Sync (Capture Model)", ImVec2(bw * 0.8f, 28))) {
        if (m_doSync) m_doSync();
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    if (ImGui::Button("Reset Layout", ImVec2(bw * 0.18f, 28))) {
        resetLayout();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset all window positions to defaults");

    // Visualization mode buttons
    ImGui::Dummy(ImVec2(0, 4));
    renderModeButtons(m_vizMode.load());
}

// ---------------------------------------------------------------------------
// Metrics Tab – per-metric comparison table
// ---------------------------------------------------------------------------
void Overlay::renderMetricsTab(const AnalysisResult& result) {
    auto row = [&](const char* label, double cur, double ref, double mul, const char* fmt) {
        cur *= mul; ref *= mul;
        double delta = cur - ref;
        ImGui::Text("%s", label); ImGui::SameLine(120);
        ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.0f), fmt, ref); ImGui::SameLine(210);
        ImGui::TextColored(ImVec4(0.8f,0.8f,1.0f,1.0f), fmt, cur); ImGui::SameLine(300);
        ImVec4 dc = (std::abs(delta) < 0.001) ? ImVec4(0.5f,0.5f,0.5f,1.0f) :
                     (std::abs(delta) < 0.01) ? ImVec4(1.0f,1.0f,0.3f,1.0f) :
                     ImVec4(1.0f,0.3f,0.3f,1.0f);
        ImGui::TextColored(dc, "%+.4f", delta);
    };

    if (result.stereoModel.active) {
        // Header
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f), "%-9s %6s %6s %s", "", "Base", "Cur", "\u0394");
        ImGui::Separator();

        // -- Level 1: primary metrics --
        if (ImGui::CollapsingHeader("Alignment", ImGuiTreeNodeFlags_DefaultOpen)) {
            row("SSIM",       result.residual.alignedSSIM,           result.stereoModel.refAlignedSSIM,1.0,"%.4f");
            row("PixDiff %",  result.residual.alignedPixDiffPercent, result.stereoModel.refAlignedPixDiffPercent,1.0,"%.2f");
            row("Bright \u0394",result.residual.alignedBrightnessDelta,result.stereoModel.refAlignedBrightnessDelta,1.0,"%.4f");
            row("Edge Sim",   result.residual.alignedEdgeSimilarity,  result.stereoModel.refAlignedEdgeSimilarity,1.0,"%.4f");
            row("Hist Corr",  result.residual.alignedHistogramCorrelation,result.stereoModel.refAlignedHistogramCorrelation,1.0,"%.4f");
            row("Occlusion",  result.residual.occlusionRatio,         result.stereoModel.refOcclusionRatio,100.0,"%.1f%%");
        }

        // -- Level 2: secondary metrics --
        if (ImGui::CollapsingHeader("Disparity & Asymmetry")) {
            row("Disp Mean",  result.disparity.meanDisparity,         result.stereoModel.refDisparityMean,1.0,"%.1f");
            row("Bloom Asym", result.asymmetry.bloomAsymmetry,        result.stereoModel.refBloomAsymmetry,1.0,"%.4f");
            row("Shadow",     result.asymmetry.shadowAsymmetry,       result.stereoModel.refShadowAsymmetry,1.0,"%.4f");
        }

        // -- Level 3: all remaining --
        if (ImGui::CollapsingHeader("All Metrics")) {
            row("Disp Range", result.disparity.disparityRange,        result.stereoModel.refDisparityRange,1.0,"%.1f");
            row("Disp Smooth",result.disparity.smoothness,            result.stereoModel.refDisparitySmoothness,1.0,"%.3f");
            row("Disp Invalid",result.disparity.invalidRatio,         result.stereoModel.refDisparityInvalidRatio,100.0,"%.1f%%");
            row("Light Asym", result.asymmetry.lightingAsymmetry,     result.stereoModel.refLightingAsymmetry,1.0,"%.4f");
        }
    } else {
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f), "Sync a baseline to see metric comparison.\n");

        if (ImGui::CollapsingHeader("Current Readings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("SSIM:           %.4f", result.residual.alignedSSIM);
            ImGui::Text("PixDiff:        %.2f%%", result.residual.alignedPixDiffPercent);
            ImGui::Text("Brightness \u0394:  %.4f", result.residual.alignedBrightnessDelta);
            ImGui::Text("Edge Sim:       %.4f", result.residual.alignedEdgeSimilarity);
            ImGui::Text("Occlusion:      %.1f%%", result.residual.occlusionRatio * 100.0);
            ImGui::Separator();
            ImGui::Text("Disp Mean:      %.1f px", result.disparity.meanDisparity);
            ImGui::Text("Disp Range:     %.1f px", result.disparity.disparityRange);
            ImGui::Text("Light Asym:     %.4f", result.asymmetry.lightingAsymmetry);
            ImGui::Text("Bloom Asym:     %.4f", result.asymmetry.bloomAsymmetry);
        }
    }
}

// ---------------------------------------------------------------------------
// Issues Tab – grouped by category, with severity indicators
// ---------------------------------------------------------------------------
void Overlay::renderIssuesTab(const AnalysisResult& result) {
    if (result.stereoStatus == StereoStatus::UNKNOWN) {
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f),
            "Stereo comparison unavailable \u2014 scene too dark or featureless.");
        return;
    }

    // -- Deviation issues (comparison against baseline) --
    if (!result.deviations.empty()) {
        bool hasFail = false, hasWarn = false;
        for (auto& d : result.deviations) { if (d.fail) hasFail = true; if (d.warning) hasWarn = true; }
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 6));

        ImGui::TextColored(hasFail ? ImVec4(1.0f,0.2f,0.2f,1.0f) :
                           hasWarn ? ImVec4(1.0f,0.6f,0.0f,1.0f) :
                                     ImVec4(0.8f,0.8f,0.3f,1.0f),
            "Baseline Deviations (%zu)", result.deviations.size());
        ImGui::Separator();

        for (size_t i = 0; i < result.deviations.size(); i++) {
            const auto& d = result.deviations[i];
            const char* severityTag = "";
            ImVec4 severityColor;
            if (d.fail)    { severityTag = "FAIL"; severityColor = ImVec4(1.0f,0.2f,0.2f,1.0f); }
            else if (d.warning) { severityTag = "WARN"; severityColor = ImVec4(1.0f,0.6f,0.0f,1.0f); }
            else               { severityTag = "OK";   severityColor = ImVec4(0.8f,0.8f,0.3f,1.0f); }

            ImGui::TextColored(severityColor, "%-8s", severityTag);
            ImGui::SameLine(70);
            if (d.tolerance > 1.0)
                ImGui::Text("%s: B=%.1f C=%.1f \u0394%+.1f",
                    d.metricName.c_str(), d.baselineValue, d.currentValue, d.delta);
            else
                ImGui::Text("%s: B=%.3f C=%.3f \u0394%+.3f",
                    d.metricName.c_str(), d.baselineValue, d.currentValue, d.delta);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Tolerance: %.2f", d.tolerance);
            }
        }
        ImGui::PopStyleVar();
    } else if (result.stereoModel.active) {
        ImGui::TextColored(ImVec4(0.3f,0.85f,0.3f,1.0f), "No deviations \u2014 stereo matches baseline.");
    }

    // -- Detected region issues --
    int validCount = 0;
    for (auto& iss : result.detectedIssues) { if (!iss.isInvalidRegion) validCount++; }

    if (validCount > 0) {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextColored(ImVec4(1.0f,0.6f,0.2f,1.0f), "Region Issues (%d)", validCount);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 8));
        ImGui::BeginChild("IssueList", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.55f), true);

        for (int i = 0; i < (int)result.detectedIssues.size(); i++) {
            const auto& iss = result.detectedIssues[i];
            if (iss.isInvalidRegion) continue;

            bool sel = (i == m_selectedIssueIndex);

            ImVec4 gc;
            switch (iss.type) {
                case IssueType::LightingDifference:   gc=ImVec4(1.0f,0.6f,0.0f,1.0f); break;
                case IssueType::ShadowDifference:     gc=ImVec4(0.6f,0.3f,0.2f,1.0f); break;
                case IssueType::BloomDifference:      gc=ImVec4(1.0f,0.8f,0.0f,1.0f); break;
                case IssueType::ReflectionDifference: gc=ImVec4(0.3f,0.7f,1.0f,1.0f); break;
                case IssueType::TextureDifference:    gc=ImVec4(0.8f,0.2f,0.8f,1.0f); break;
                case IssueType::MaterialDifference:   gc=ImVec4(1.0f,0.5f,0.2f,1.0f); break;
                case IssueType::TransparencyDifference: gc=ImVec4(0.2f,0.8f,0.8f,1.0f); break;
                case IssueType::EdgeDifference:       gc=ImVec4(0.0f,1.0f,0.5f,1.0f); break;
                case IssueType::MissingGeometry:      gc=ImVec4(0.8f,0.0f,0.0f,1.0f); break;
                case IssueType::ExtraGeometry:        gc=ImVec4(1.0f,0.4f,0.0f,1.0f); break;
                case IssueType::MissingObject:        gc=ImVec4(1.0f,0.2f,0.2f,1.0f); break;
                case IssueType::MissingParticle:      gc=ImVec4(1.0f,0.6f,0.6f,1.0f); break;
                case IssueType::MissingUI:            gc=ImVec4(0.1f,0.5f,1.0f,1.0f); break;
                case IssueType::TextDifference:       gc=ImVec4(0.6f,0.8f,1.0f,1.0f); break;
                case IssueType::StereoOffset:         gc=ImVec4(0.5f,0.5f,1.0f,1.0f); break;
                case IssueType::DepthDisparityError:  gc=ImVec4(1.0f,0.0f,0.5f,1.0f); break;
                case IssueType::OcclusionDifference:  gc=ImVec4(0.5f,0.5f,0.5f,1.0f); break;
                case IssueType::PostProcessDifference: gc=ImVec4(0.3f,1.0f,0.7f,1.0f); break;
                case IssueType::TemporalDifference:   gc=ImVec4(0.2f,0.6f,1.0f,1.0f); break;
                case IssueType::LensBoundary:         gc=ImVec4(0.4f,0.4f,0.4f,1.0f); break;
                case IssueType::LowConfidence:        gc=ImVec4(0.6f,0.6f,0.6f,1.0f); break;
                default: gc=ImVec4(0.8f,0.8f,0.8f,1.0f); break;
            }

            char label[256];
            snprintf(label, sizeof(label), "%s (%.0f%%) [%d\u00d7%d]",
                     IssueTypeName(iss.type), iss.confidence * 100.0f,
                     iss.boundingBox.width, iss.boundingBox.height);

            ImGui::PushStyleColor(ImGuiCol_Text, gc);
            ImGui::PushID(i);
            if (ImGui::Selectable(label, sel, ImGuiSelectableFlags_SpanAllColumns)) {
                m_selectedIssueIndex = (m_selectedIssueIndex == i) ? -1 : i;
            }
            ImGui::PopID();
            ImGui::PopStyleColor();

            if (ImGui::IsItemHovered()) {
                std::string tip = "Area: " + std::to_string(iss.areaPixels) + " px\xc2\xb2\n"
                    "Pos: (" + std::to_string(iss.boundingBox.x) + ", " +
                    std::to_string(iss.boundingBox.y) + ")";
                if (!iss.alternatives.empty()) {
                    tip += "\n\nAlternatives:";
                    for (auto& alt : iss.alternatives) {
                        char ab[64];
                        snprintf(ab, sizeof(ab), "\n  %s (%.0f%%)",
                                 IssueTypeName(alt.type), alt.confidence * 100.0f);
                        tip += ab;
                    }
                }
                ImGui::SetTooltip("%s", tip.c_str());
            }
        }

        ImGui::EndChild();
        ImGui::PopStyleVar();

        // -- Selected issue detail --
        if (m_selectedIssueIndex >= 0 && m_selectedIssueIndex < (int)result.detectedIssues.size()) {
            const auto& selIssue = result.detectedIssues[m_selectedIssueIndex];
            if (!selIssue.isInvalidRegion) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.8f,0.8f,1.0f,1.0f), "Issue Detail");

                if (ImGui::BeginChild("IssueDetail", ImVec2(0, 0), true)) {
                    if (!selIssue.reasoningText.empty()) {
                        ImGui::TextWrapped("%s", selIssue.reasoningText.c_str());
                    } else {
                        ImGui::Text("Type: %s", IssueTypeName(selIssue.type));
                        ImGui::Text("Confidence: %.0f%%", selIssue.confidence * 100.0f);
                        ImGui::Text("Position: (%d, %d)", selIssue.boundingBox.x, selIssue.boundingBox.y);
                    ImGui::Text("Size: %d\u00d7%d px (%d px\xc2\xb2)",
                        selIssue.boundingBox.width, selIssue.boundingBox.height, selIssue.areaPixels);
                        ImGui::Separator();
                        ImGui::Text("Evidence:");
                        auto& ev = selIssue.evidence;
                        ImGui::Text("  Brightness Diff:  %.3f", ev.brightnessDiff);
                        ImGui::Text("  Contrast Diff:    %.3f", ev.contrastDiff);
                        ImGui::Text("  Color Diff:       %.3f", ev.colorDiff);
                        ImGui::Text("  Edge Similarity:  %.3f", ev.edgeSimilarity);
                        ImGui::Text("  Texture Similar:  %.3f", ev.textureSimilarity);
                        ImGui::Text("  Gradient Consist: %.3f", ev.gradientConsistency);
                        ImGui::Text("  Histogram Sim:    %.3f", ev.histogramSimilarity);
                        ImGui::Text("  Disp Consistency: %.3f", ev.disparityConsistency);
                    }
                }
                ImGui::EndChild();
            }
        }
    }

    // No issues at all
    if (result.deviations.empty() && result.detectedIssues.empty() && result.stereoStatus != StereoStatus::UNKNOWN) {
        if (!result.stereoModel.active && !result.issues.empty()) {
            ImGui::TextColored(ImVec4(1.0f,0.6f,0.0f,1.0f), "Issues (no baseline):");
            for (size_t i = 0; i < std::min((size_t)5, result.issues.size()); i++)
                ImGui::Text("  - %s", result.issues[i].c_str());
        } else {
            ImGui::TextColored(ImVec4(0.3f,0.85f,0.3f,1.0f), "No issues detected.");
        }
    }

    // -- Invalid/filtered regions (shown collapsed) --
    int invalidCount = 0;
    for (auto& iss : result.detectedIssues) { if (iss.isInvalidRegion) invalidCount++; }
    if (invalidCount > 0) {
        ImGui::Separator();
        char hdr[64];
        snprintf(hdr, sizeof(hdr), "Filtered Regions (%d)", invalidCount);
        if (ImGui::CollapsingHeader(hdr)) {
            for (auto& iss : result.detectedIssues) {
                if (!iss.isInvalidRegion) continue;
                ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f),
                    "  %s [%d\u00d7%d] %s",
                    IssueTypeName(iss.type),
                    iss.boundingBox.width, iss.boundingBox.height,
                    iss.invalidReason.c_str());
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Checks Tab – per-check enable/disable toggles
// ---------------------------------------------------------------------------
void Overlay::renderChecksTab() {
    CheckToggles toggles;
    if (m_getChecks) {
        toggles = m_getChecks();
    }

    auto toggle = [&](const char* label, bool& flag) {
        if (ImGui::Checkbox(label, &flag)) {
            if (m_updateChecks) m_updateChecks(toggles);
        }
    };

    if (ImGui::CollapsingHeader("Image Analysis", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(8);
        toggle("SSIM", toggles.ssim);
        toggle("Pixel Difference", toggles.pixelDiff);
        toggle("Histogram", toggles.histogram);
        toggle("Edge", toggles.edge);
        toggle("ORB Features", toggles.orb);
        toggle("Optical Flow", toggles.opticalFlow);
        toggle("Blur", toggles.blur);
        toggle("Brightness", toggles.brightness);
        toggle("Contrast", toggles.contrast);
        toggle("Bloom", toggles.bloom);
        toggle("Shadow", toggles.shadow);
        toggle("Stereo Offset", toggles.stereoOffset);
        toggle("OCR Text", toggles.ocr);
        ImGui::Unindent(8);
    }

    ImGui::Dummy(ImVec2(0, 4));

    if (ImGui::CollapsingHeader("Correspondence", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(8);
        toggle("Stereo Correspondence", toggles.correspondence);
        toggle("Disparity Metrics", toggles.disparityMetrics);
        toggle("Match Quality", toggles.matchQuality);
        ImGui::Unindent(8);
    }

    ImGui::Dummy(ImVec2(0, 4));

    if (ImGui::CollapsingHeader("Asymmetry", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(8);
        toggle("Master Asymmetry", toggles.asymmetry);
        ImGui::Indent(16);
        toggle("  Lighting", toggles.lightingAsym);
        toggle("  Bloom", toggles.bloomAsym);
        toggle("  Shadow", toggles.shadowAsym);
        toggle("  Post-Process", toggles.postProcessAsym);
        toggle("  Texture", toggles.textureAsym);
        toggle("  Blur", toggles.blurAsym);
        toggle("  Chromatic", toggles.chromaticAsym);
        toggle("  Contrast", toggles.contrastAsym);
        ImGui::Unindent(24);
    }

    ImGui::Dummy(ImVec2(0, 4));

    if (ImGui::CollapsingHeader("Issue Detection", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(8);
        toggle("Detect Issues", toggles.detectIssues);
        toggle("  Issue Classification", toggles.issueClassification);
        toggle("  Issue Merging", toggles.issueMerging);
        toggle("  Geometry Missing Check", toggles.geometryMissing);
        ImGui::Unindent(8);
    }

    ImGui::Dummy(ImVec2(0, 4));

    if (ImGui::CollapsingHeader("Scoring & Analysis", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Indent(8);
        toggle("Temporal Analysis", toggles.temporal);
        toggle("Scene Confidence", toggles.sceneConfidence);
        toggle("Health Score", toggles.healthScore);
        toggle("Baseline Comparison", toggles.baselineComparison);
        ImGui::Unindent(8);
    }

    ImGui::Dummy(ImVec2(0, 6));

    // Quick-select buttons
    ImGui::Text("Quick Actions:");
    if (ImGui::SmallButton("Enable All")) {
        toggles = CheckToggles{};
        for (auto& [key, val] : { std::pair<const char*, bool*>("", &toggles.correspondence) }) {
            (void)key; (void)val;
        }
        // Set all to true by re-initializing
        toggles = CheckToggles{};
        if (m_updateChecks) m_updateChecks(toggles);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Disable All")) {
        CheckToggles allOff;
        memset(&allOff, 0, sizeof(allOff));
        if (m_updateChecks) m_updateChecks(allOff);
    }
}

// ---------------------------------------------------------------------------
// Graphs Tab – 8 sparkline graphs
// ---------------------------------------------------------------------------
void Overlay::renderGraphsTab(const MetricHistory& history) {
    if (history.healthScore.empty()) {
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f), "No graph data yet \u2014 wait for metrics to accumulate.");
        return;
    }
    ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f), "Last %zu frames", history.healthScore.size());
    ImGui::Separator();
    renderGraphPanel(history);
}

// ---------------------------------------------------------------------------
// Visualization Window – shows the current mode's rendered output
// ---------------------------------------------------------------------------
void Overlay::renderVisualizationWindow() {
    ImGui::SetNextWindowSize(ImVec2(420, 320), ImGuiCond_FirstUseEver);
    ImGui::Begin("Visualization", nullptr, ImGuiWindowFlags_NoScrollbar);

    if (m_vizSRV) {
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float aspect = (float)m_vizTexWidth / (float)std::max(m_vizTexHeight, 1);
        float imgW = avail.x;
        float imgH = imgW / aspect;
        if (imgH > avail.y) {
            imgH = avail.y;
            imgW = imgH * aspect;
        }
        ImGui::Image((ImTextureID)m_vizSRV.Get(), ImVec2(imgW, imgH));

        const char* modeName = VisualizationModeName(m_vizMode.load());
        ImGui::TextColored(ImVec4(0.6f,0.6f,0.8f,1.0f), "Mode: %s", modeName);
    } else {
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f), "No visualization data");
    }

    ImGui::End();
}

// ===========================================================================
// renderFrame – called every iteration of main loop
// ===========================================================================
void Overlay::renderFrame() {
    if (!m_initialized) return;

    m_d3dContext->OMSetRenderTargets(1, m_rtView.GetAddressOf(), nullptr);
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    m_d3dContext->ClearRenderTargetView(m_rtView.Get(), clearColor);

    if (!m_captureSafe.load()) {
        renderWindows();

        // Update visualization texture
        if (m_getFrame && m_getResult) {
            cv::Mat frame = m_getFrame();
            if (!frame.empty()) {
                auto res = m_getResult();
                StereoLayout layout = m_getLayout ? m_getLayout() : StereoLayout::Unknown;
                cv::Mat left, right;
                int mid = frame.cols / 2;
                if (layout == StereoLayout::OverUnder) {
                    mid = frame.rows / 2;
                    left = frame(cv::Rect(0,0,frame.cols,mid)).clone();
                    right = frame(cv::Rect(0,mid,frame.cols,frame.rows-mid)).clone();
                } else {
                    left = frame(cv::Rect(0,0,mid,frame.rows)).clone();
                    right = frame(cv::Rect(mid,0,frame.cols-mid,frame.rows)).clone();
                }
                MetricHistory hist;
                if (m_getHistory) hist = m_getHistory();
                cv::Mat viz = m_visualizer.render(frame, left, right, res, m_vizMode.load(), &hist, m_selectedIssueIndex);
                updateVizTexture(viz);
            }
        }
    }

    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    m_swapChain->Present(1, 0);
}

bool Overlay::processMessages() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) return false;
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return true;
}

LRESULT CALLBACK Overlay::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return true;

    switch (msg) {
        case WM_NCHITTEST: {
            return HTCLIENT;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            if (auto* self = (Overlay*)GetWindowLongPtr(hwnd, GWLP_USERDATA)) {
                if (self->m_d3dDevice && self->m_swapChain) {
                    self->m_rtView.Reset();
                    self->m_backBuffer.Reset();
                    self->m_swapChain->ResizeBuffers(0, LOWORD(lParam), HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
                    self->m_swapChain->GetBuffer(0, IID_PPV_ARGS(&self->m_backBuffer));
                    self->m_d3dDevice->CreateRenderTargetView(self->m_backBuffer.Get(), nullptr, &self->m_rtView);
                }
            }
            return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
