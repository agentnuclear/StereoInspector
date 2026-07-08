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
void Overlay::setGetAppConfigCallback(GetAppConfigCallback cb) { m_getAppConfig = std::move(cb); }
void Overlay::setSetAppConfigCallback(SetAppConfigCallback cb) { m_setAppConfig = std::move(cb); }
void Overlay::setExportReportCallback(ExportReportCallback cb) { m_exportReport = std::move(cb); }
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
    m_splitterRatio = 0.35f;
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

void Overlay::applyStyle() {
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.ChildRounding = 4.0f;
    style.PopupRounding = 4.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 1.0f;
    style.TabBorderSize = 0.0f;

    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(6, 4);
    style.ItemSpacing = ImVec2(6, 4);
    style.ItemInnerSpacing = ImVec2(4, 4);
    style.IndentSpacing = 16.0f;
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 8.0f;

    style.WindowMinSize = ImVec2(240, 80);
    style.WindowMenuButtonPosition = ImGuiDir_None;

    // Dark theme with higher contrast
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text]                   = ImVec4(0.92f, 0.92f, 0.95f, 1.00f);
    colors[ImGuiCol_TextDisabled]           = ImVec4(0.50f, 0.50f, 0.55f, 1.00f);
    colors[ImGuiCol_WindowBg]               = ImVec4(0.08f, 0.08f, 0.10f, 0.94f);
    colors[ImGuiCol_ChildBg]                = ImVec4(0.10f, 0.10f, 0.12f, 0.94f);
    colors[ImGuiCol_PopupBg]                = ImVec4(0.10f, 0.10f, 0.13f, 0.95f);
    colors[ImGuiCol_Border]                 = ImVec4(0.20f, 0.20f, 0.25f, 0.50f);
    colors[ImGuiCol_BorderShadow]           = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_FrameBg]                = ImVec4(0.14f, 0.14f, 0.17f, 0.94f);
    colors[ImGuiCol_FrameBgHovered]         = ImVec4(0.20f, 0.20f, 0.25f, 0.94f);
    colors[ImGuiCol_FrameBgActive]          = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_TitleBg]                = ImVec4(0.06f, 0.06f, 0.08f, 1.00f);
    colors[ImGuiCol_TitleBgActive]          = ImVec4(0.10f, 0.10f, 0.13f, 1.00f);
    colors[ImGuiCol_TitleBgCollapsed]       = ImVec4(0.08f, 0.08f, 0.10f, 0.75f);
    colors[ImGuiCol_MenuBarBg]              = ImVec4(0.10f, 0.10f, 0.13f, 0.95f);
    colors[ImGuiCol_ScrollbarBg]            = ImVec4(0.10f, 0.10f, 0.12f, 0.90f);
    colors[ImGuiCol_ScrollbarGrab]          = ImVec4(0.25f, 0.25f, 0.30f, 0.80f);
    colors[ImGuiCol_ScrollbarGrabHovered]   = ImVec4(0.35f, 0.35f, 0.40f, 0.90f);
    colors[ImGuiCol_ScrollbarGrabActive]    = ImVec4(0.45f, 0.45f, 0.50f, 1.00f);
    colors[ImGuiCol_CheckMark]              = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]             = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrabActive]       = ImVec4(0.40f, 0.75f, 1.00f, 1.00f);
    colors[ImGuiCol_Button]                 = ImVec4(0.18f, 0.18f, 0.22f, 0.94f);
    colors[ImGuiCol_ButtonHovered]          = ImVec4(0.25f, 0.25f, 0.32f, 0.94f);
    colors[ImGuiCol_ButtonActive]           = ImVec4(0.30f, 0.30f, 0.38f, 1.00f);
    colors[ImGuiCol_Header]                 = ImVec4(0.18f, 0.18f, 0.24f, 0.80f);
    colors[ImGuiCol_HeaderHovered]          = ImVec4(0.24f, 0.24f, 0.32f, 0.90f);
    colors[ImGuiCol_HeaderActive]           = ImVec4(0.28f, 0.28f, 0.38f, 1.00f);
    colors[ImGuiCol_Separator]              = ImVec4(0.25f, 0.25f, 0.30f, 0.50f);
    colors[ImGuiCol_SeparatorHovered]       = ImVec4(0.40f, 0.40f, 0.50f, 0.60f);
    colors[ImGuiCol_SeparatorActive]        = ImVec4(0.50f, 0.50f, 0.60f, 1.00f);
    colors[ImGuiCol_ResizeGrip]             = ImVec4(0.30f, 0.30f, 0.40f, 0.30f);
    colors[ImGuiCol_ResizeGripHovered]      = ImVec4(0.40f, 0.40f, 0.50f, 0.50f);
    colors[ImGuiCol_ResizeGripActive]       = ImVec4(0.50f, 0.50f, 0.60f, 0.80f);
    colors[ImGuiCol_Tab]                    = ImVec4(0.14f, 0.14f, 0.18f, 0.90f);
    colors[ImGuiCol_TabHovered]             = ImVec4(0.22f, 0.22f, 0.30f, 0.94f);
    colors[ImGuiCol_TabActive]              = ImVec4(0.20f, 0.20f, 0.28f, 1.00f);
    colors[ImGuiCol_TabUnfocused]           = ImVec4(0.10f, 0.10f, 0.14f, 0.90f);
    colors[ImGuiCol_TabUnfocusedActive]     = ImVec4(0.16f, 0.16f, 0.22f, 1.00f);
    // Docking colors not available in non-docking branch
    colors[ImGuiCol_PlotLines]              = ImVec4(0.60f, 0.60f, 0.70f, 1.00f);
    colors[ImGuiCol_PlotLinesHovered]       = ImVec4(0.80f, 0.80f, 1.00f, 1.00f);
    colors[ImGuiCol_PlotHistogram]          = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_PlotHistogramHovered]   = ImVec4(0.50f, 0.75f, 1.00f, 1.00f);
    colors[ImGuiCol_TableHeaderBg]          = ImVec4(0.14f, 0.14f, 0.18f, 1.00f);
    colors[ImGuiCol_TableBorderStrong]      = ImVec4(0.25f, 0.25f, 0.30f, 1.00f);
    colors[ImGuiCol_TableBorderLight]       = ImVec4(0.20f, 0.20f, 0.25f, 0.70f);
    colors[ImGuiCol_TableRowBg]             = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    colors[ImGuiCol_TableRowBgAlt]          = ImVec4(1.00f, 1.00f, 1.00f, 0.03f);
    colors[ImGuiCol_TextSelectedBg]         = ImVec4(0.30f, 0.65f, 1.00f, 0.35f);
    colors[ImGuiCol_DragDropTarget]         = ImVec4(1.00f, 1.00f, 0.00f, 0.90f);
    colors[ImGuiCol_NavHighlight]           = ImVec4(0.30f, 0.65f, 1.00f, 1.00f);
    colors[ImGuiCol_NavWindowingHighlight]  = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
    colors[ImGuiCol_NavWindowingDimBg]      = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
    colors[ImGuiCol_ModalWindowDimBg]       = ImVec4(0.10f, 0.10f, 0.12f, 0.55f);
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

    applyStyle();

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
// Mini graph helper
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
    float gh = 60.0f;
    renderMiniGraph("Health", history.healthScore, gw, gh, 0.0f, 100.0f, IM_COL32(76, 175, 80, 220));
    ImGui::SameLine();
    renderMiniGraph("SSIM", history.ssim, gw, gh, 0.0f, 1.0f, IM_COL32(33, 150, 243, 220));
    renderMiniGraph("Disp", history.disparityMean, gw, gh, 0.0f, 200.0f, IM_COL32(255, 152, 0, 220));
    ImGui::SameLine();
    renderMiniGraph("Bright", history.brightnessDelta, gw, gh, 0.0f, 1.0f, IM_COL32(156, 39, 176, 220));
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
        if (ImGui::Button(label, ImVec2(bw, 24))) { m_vizMode = m; }
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
// Sync feedback popup (center overlay, not inside a window)
// ---------------------------------------------------------------------------
void Overlay::renderSyncFeedback() {
    ImGuiIO& io = ImGui::GetIO();
    if (m_syncFeedback.show) {
        m_syncFeedback.timer -= io.DeltaTime;
        float alpha = std::min(1.0f, m_syncFeedback.timer / 2.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.4f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::Begin("###SyncCaptured", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoInputs);
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
        ImVec2 center = ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.4f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::Begin("###SyncRefused", nullptr,
                     ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoInputs);
        ImGui::Text("Baseline Capture Refused");
        ImGui::TextWrapped("%s", m_syncRefused.reason.c_str());
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        if (m_syncRefused.timer <= 0.0f) m_syncRefused.show = false;
    }
}

// ===========================================================================
// NEW: Root window with app-style layout
// ===========================================================================
void Overlay::renderMainWindow() {
    ImGuiIO& io = ImGui::GetIO();

    // Fullscreen background window
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                             ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoSavedSettings;

    ImGui::Begin("MainWindow", nullptr, flags);
    ImGui::PopStyleVar(3);

    // Menu bar
    float cursorYBeforeMenu = ImGui::GetCursorPosY();
    renderMenuBar();
    float menuBarHeight = ImGui::GetCursorPosY() - cursorYBeforeMenu;
    float statusBarHeight = 24.0f;
    float contentY = menuBarHeight;

    // Toolbar row
    {
        AnalysisResult result;
        FrameTime ft;
        if (m_getResult) result = m_getResult();
        if (m_getTime) ft = m_getTime();

        ImGui::SetCursorScreenPos(ImVec2(0, contentY));
        float toolbarH = 34.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
        ImGui::BeginChild("Toolbar", ImVec2(io.DisplaySize.x, toolbarH), false, ImGuiWindowFlags_NoScrollbar);
        renderToolbar(result, ft);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        contentY += toolbarH;
    }

    // Content split: Hub panel (left) | Viz panel (right)
    {
        float contentH = io.DisplaySize.y - contentY - statusBarHeight;
        if (contentH > 0) {
            float splitterW = 6.0f;
            float hubW = std::max(280.0f, io.DisplaySize.x * m_splitterRatio);
            float vizW = io.DisplaySize.x - hubW - splitterW;

            // Hub panel
            ImGui::SetCursorScreenPos(ImVec2(0, contentY));
            ImGui::BeginChild("HubPanel", ImVec2(hubW, contentH), true);

            AnalysisResult result;
            FrameTime ft;
            MetricHistory history;
            if (m_getResult) result = m_getResult();
            if (m_getTime) ft = m_getTime();
            if (m_getHistory) history = m_getHistory();

            renderHubPanel(result, ft, history);
            ImGui::EndChild();

            // Splitter
            ImGui::SameLine(0, 0);
            ImGui::SetCursorScreenPos(ImVec2(hubW, contentY));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.18f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.30f, 0.65f, 1.00f, 0.30f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.65f, 1.00f, 0.50f));

            ImGui::Button("##splitter", ImVec2(splitterW, contentH));
            if (ImGui::IsItemActive() || ImGui::IsItemHovered()) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            }
            if (ImGui::IsItemActive()) {
                m_splitterRatio += io.MouseDelta.x / io.DisplaySize.x;
                m_splitterRatio = std::max(0.20f, std::min(0.60f, m_splitterRatio));
            }

            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();

            // Viz panel
            ImGui::SameLine(0, 0);
            ImGui::SetCursorScreenPos(ImVec2(hubW + splitterW, contentY));
            ImGui::BeginChild("VizPanel", ImVec2(vizW, contentH), true);
            renderVizPanel();
            ImGui::EndChild();
        }
    }

    // Status bar
    {
        AnalysisResult result;
        FrameTime ft;
        if (m_getResult) result = m_getResult();
        if (m_getTime) ft = m_getTime();

        ImGui::SetCursorScreenPos(ImVec2(0, io.DisplaySize.y - statusBarHeight));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 2));
        ImGui::BeginChild("StatusBar", ImVec2(io.DisplaySize.x, statusBarHeight), false, ImGuiWindowFlags_NoScrollbar);
        renderStatusBar(result, ft);
        ImGui::EndChild();
        ImGui::PopStyleVar();
    }

    ImGui::End();

    // Modal dialogs (rendered outside main window)
    if (m_showSettings) renderSettingsDialog();
    if (m_showHotkeys) renderHotkeyDialog();
    if (m_showAbout) renderAboutDialog();
}

// ---------------------------------------------------------------------------
// Menu Bar
// ---------------------------------------------------------------------------
void Overlay::renderMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Export Report", "Ctrl+R")) {
                if (m_exportReport) m_exportReport();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                PostQuitMessage(0);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Settings", "Ctrl+,", &m_showSettings);

            ImGui::Separator();
            ImGui::Text("Visualization Mode");
            ImGui::Separator();

            auto vizItem = [&](VisualizationMode mode, const char* label, const char* shortcut) {
                bool active = (m_vizMode.load() == mode);
                if (ImGui::MenuItem(label, shortcut, &active)) {
                    m_vizMode = mode;
                }
            };

            vizItem(VisualizationMode::Normal, "Normal", "F1");
            vizItem(VisualizationMode::StereoDifferenceOverlay, "Diff Overlay", "F3");
            vizItem(VisualizationMode::DifferenceHeatmap, "Heatmap", "F4");
            vizItem(VisualizationMode::EdgeComparison, "Edge Comparison", "Ctrl+F1");
            vizItem(VisualizationMode::FeatureMatchOverlay, "Feature Match", "Ctrl+F2");
            vizItem(VisualizationMode::HistogramView, "Histogram", "Ctrl+F3");
            vizItem(VisualizationMode::BlurMap, "Blur Map", "Ctrl+F4");
            vizItem(VisualizationMode::BlinkLeft, "Blink Left", "Ctrl+F5");
            vizItem(VisualizationMode::BlinkRight, "Blink Right", "Ctrl+F6");
            vizItem(VisualizationMode::DisparityHeatmap, "Disparity Heatmap", "Ctrl+F7");

            ImGui::Separator();
            if (ImGui::MenuItem("Reset Layout")) {
                resetLayout();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Settings...", "Ctrl+,")) {
                m_showSettings = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Keyboard Shortcuts...", "F1")) {
                m_showHotkeys = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("About Stereo Inspector...")) {
                m_showAbout = true;
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------
void Overlay::renderToolbar(const AnalysisResult& result, const FrameTime&) {
    bool hasBase = result.stereoModel.active;

    // Left side: action buttons
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.58f, 0.25f, 0.8f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.17f, 0.68f, 0.30f, 0.9f));
    if (ImGui::Button(hasBase ? "Resync##tb" : "Sync##tb", ImVec2(80, 24))) {
        if (m_doSync) m_doSync();
    }
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    if (hasBase) {
        if (ImGui::Button("Clear##tb", ImVec2(60, 24))) {
            if (m_doClearBaseline) m_doClearBaseline();
        }
        ImGui::SameLine();
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.15f, 0.20f, 0.9f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.22f, 0.28f, 0.9f));

    if (ImGui::Button("Screenshot##tb", ImVec2(90, 24))) {
        // Trigger screenshot via F6 equivalent
        PostMessage(m_hwnd, WM_KEYDOWN, VK_F6, 0);
        PostMessage(m_hwnd, WM_KEYUP, VK_F6, 0);
    }
    ImGui::SameLine();

    if (ImGui::Button("Frozen##tb", ImVec2(70, 24))) {
        m_frozen = !m_frozen;
    }
    ImGui::SameLine();

    if (ImGui::Button("Layout##tb", ImVec2(70, 24))) {
        resetLayout();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reset window layout to defaults");

    // Vertical separator
    ImGui::SameLine();
    ImVec2 sepPos = ImGui::GetCursorScreenPos();
    float sepHeight = ImGui::GetContentRegionAvail().y;
    ImGui::GetWindowDrawList()->AddLine(
        ImVec2(sepPos.x, sepPos.y),
        ImVec2(sepPos.x, sepPos.y + sepHeight),
        ImGui::GetColorU32(ImGuiCol_Separator));
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(1, 0));

    // Viz mode buttons inline on toolbar
    renderModeButtons(m_vizMode.load());
    ImGui::PopStyleColor(2);
}

// ---------------------------------------------------------------------------
// Status Bar
// ---------------------------------------------------------------------------
void Overlay::renderStatusBar(const AnalysisResult& result, const FrameTime& ft) {
    float aw = ImGui::GetContentRegionAvail().x;
    float x = 0;

    // Status
    ImVec4 sc;
    const char* statusLabel = "";
    switch (result.stereoStatus) {
        case StereoStatus::SAFE:    sc = ImVec4(0.3f, 0.85f, 0.3f, 1.0f); statusLabel = "SAFE"; break;
        case StereoStatus::WARNING: sc = ImVec4(1.0f, 0.6f, 0.0f, 1.0f); statusLabel = "WARN"; break;
        case StereoStatus::DESYNC:  sc = ImVec4(1.0f, 0.2f, 0.2f, 1.0f); statusLabel = "DESYNC"; break;
        case StereoStatus::UNKNOWN: sc = ImVec4(0.5f, 0.5f, 0.5f, 1.0f); statusLabel = "?"; break;
    }
    ImGui::TextColored(sc, "%s", statusLabel);
    x += 50;

    // Integrity
    ImGui::SameLine(x); ImGui::Text("Integrity: %.0f", result.stereoIntegrityScore);
    x += 100;

    // FPS
    ImGui::SameLine(x);
    ImVec4 fpsc = ft.fps >= 60.0f ? ImVec4(0.3f, 0.85f, 0.3f, 1.0f) :
                  ft.fps >= 30.0f ? ImVec4(1.0f, 0.6f, 0.0f, 1.0f) :
                  ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
    ImGui::TextColored(fpsc, "FPS: %.1f", ft.fps);
    x += 80;

    // Frame
    ImGui::SameLine(x); ImGui::Text("Frame: #%lld", (long long)result.frameNumber);
    x += 120;

    // Resolution
    if (m_getFrame) {
        cv::Mat f = m_getFrame();
        if (!f.empty()) {
            ImGui::SameLine(x); ImGui::Text("%dx%d", f.cols, f.rows);
            x += 100;
        }
    }

    // Baseline
    ImGui::SameLine(x);
    if (result.stereoModel.active) {
        ImGui::TextColored(ImVec4(0.3f, 0.85f, 0.3f, 1.0f), "Baseline: Locked");
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Baseline: None");
    }

    // Right-aligned: Scene confidence
    ImGui::SameLine(aw - 120);
    ImVec4 ssc = result.sceneConfidence.reliable ? ImVec4(0.4f, 0.7f, 1.0f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    ImGui::TextColored(ssc, "Scene: %.0f%%", result.sceneConfidence.overall * 100.0);
}

// ===========================================================================
// Hub Panel – tabbed content (left side)
// ===========================================================================
void Overlay::renderHubPanel(const AnalysisResult& result, const FrameTime& ft, const MetricHistory& history) {
    if (ImGui::BeginTabBar("HubTabs", ImGuiTabBarFlags_Reorderable)) {
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
        if (ImGui::BeginTabItem("Graphs")) {
            renderGraphsTab(history);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

// ===========================================================================
// Viz Panel – shows the current mode's rendered output (right side)
// ===========================================================================
void Overlay::renderVizPanel() {
    ImVec2 avail = ImGui::GetContentRegionAvail();

    if (m_vizSRV) {
        float aspect = (float)m_vizTexWidth / (float)std::max(m_vizTexHeight, 1);
        float imgW = avail.x;
        float imgH = imgW / aspect;
        if (imgH > avail.y - 24.0f) {
            imgH = avail.y - 24.0f;
            imgW = imgH * aspect;
        }
        // Center image
        float offsetX = std::max(0.0f, (avail.x - imgW) * 0.5f);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);
        ImGui::Image((ImTextureID)m_vizSRV.Get(), ImVec2(imgW, imgH));

        const char* modeName = VisualizationModeName(m_vizMode.load());
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 1.0f), "Mode: %s", modeName);
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "No visualization data");
    }
}

// ===========================================================================
// Settings Dialog
// ===========================================================================
void Overlay::renderSettingsDialog() {
    ImGui::SetNextWindowSize(ImVec2(520, 440), ImGuiCond_FirstUseEver);
    ImGui::Begin("Settings", &m_showSettings);

    CheckToggles toggles;
    AppConfig appCfg;
    bool haveConfig = false;
    if (m_getChecks) toggles = m_getChecks();
    if (m_getAppConfig) { appCfg = m_getAppConfig(); haveConfig = true; }

    auto toggle = [&](const char* label, bool& flag) {
        if (ImGui::Checkbox(label, &flag)) {
            if (m_updateChecks) m_updateChecks(toggles);
        }
    };

    if (ImGui::BeginTabBar("SettingsTabs")) {
        if (ImGui::BeginTabItem("Analysis Modules")) {
            if (ImGui::BeginChild("SettingsModules", ImVec2(0, 0), true)) {
            if (ImGui::CollapsingHeader("Image Analysis", ImGuiTreeNodeFlags_DefaultOpen)) {
                toggle("SSIM", toggles.ssim);
                toggle("Pixel Difference", toggles.pixelDiff);
                toggle("Histogram", toggles.histogram);
                toggle("Edge Detection", toggles.edge);
                toggle("ORB Features", toggles.orb);
                toggle("Optical Flow", toggles.opticalFlow);
                toggle("Blur Detection", toggles.blur);
                toggle("Brightness", toggles.brightness);
                toggle("Contrast", toggles.contrast);
                toggle("Bloom", toggles.bloom);
                toggle("Shadow", toggles.shadow);
                toggle("Stereo Offset", toggles.stereoOffset);
                toggle("OCR Text", toggles.ocr);
            }
            if (ImGui::CollapsingHeader("Correspondence", ImGuiTreeNodeFlags_DefaultOpen)) {
                toggle("Stereo Correspondence", toggles.correspondence);
                toggle("Disparity Metrics", toggles.disparityMetrics);
                toggle("Match Quality", toggles.matchQuality);
            }
            if (ImGui::CollapsingHeader("Asymmetry", ImGuiTreeNodeFlags_DefaultOpen)) {
                toggle("Master Asymmetry", toggles.asymmetry);
                ImGui::Indent(16);
                toggle("Lighting", toggles.lightingAsym);
                toggle("Bloom", toggles.bloomAsym);
                toggle("Shadow", toggles.shadowAsym);
                toggle("Post-Process", toggles.postProcessAsym);
                toggle("Texture", toggles.textureAsym);
                toggle("Blur", toggles.blurAsym);
                toggle("Chromatic", toggles.chromaticAsym);
                toggle("Contrast", toggles.contrastAsym);
                ImGui::Unindent(16);
            }
            if (ImGui::CollapsingHeader("Issue Detection", ImGuiTreeNodeFlags_DefaultOpen)) {
                toggle("Detect Issues", toggles.detectIssues);
                ImGui::Indent(16);
                toggle("Issue Classification", toggles.issueClassification);
                toggle("Issue Merging", toggles.issueMerging);
                toggle("Geometry Missing Check", toggles.geometryMissing);
                ImGui::Unindent(16);
            }
            if (ImGui::CollapsingHeader("Scoring", ImGuiTreeNodeFlags_DefaultOpen)) {
                toggle("Temporal Analysis", toggles.temporal);
                toggle("Scene Confidence", toggles.sceneConfidence);
                toggle("Health Score", toggles.healthScore);
                toggle("Baseline Comparison", toggles.baselineComparison);
            }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Thresholds")) {
            if (ImGui::BeginChild("SettingsThresholds", ImVec2(0, 0), true)) {
                if (haveConfig) {
                    auto& th = appCfg.thresholds;
                    ImGui::Text("Detection Thresholds");
                    ImGui::Separator();
                    float dragMinIssueConf = (float)th.minIssueConfidence;
                    float dragSceneConfThresh = (float)appCfg.sceneConfidenceThreshold;
                    float dragSsimWarn = (float)th.ssimWarning;
                    float dragSsimFail = (float)th.ssimFail;
                    float dragPixWarn = (float)th.pixelDiffWarning;
                    float dragPixFail = (float)th.pixelDiffFail;
                    float dragBrightWarn = (float)th.brightnessDeltaWarning;
                    float dragBrightFail = (float)th.brightnessDeltaFail;

                    ImGui::SetNextItemWidth(160); ImGui::DragInt("Diff Threshold", &th.diffThreshold, 1, 1, 255);
                    ImGui::SetNextItemWidth(160); ImGui::DragInt("Min Issue Area", &th.minIssueArea, 10, 10, 5000);
                    ImGui::SetNextItemWidth(160); if (ImGui::DragFloat("Min Issue Confidence", &dragMinIssueConf, 0.01f, 0.0f, 1.0f)) th.minIssueConfidence = dragMinIssueConf;
                    ImGui::SetNextItemWidth(160); if (ImGui::DragFloat("Scene Confidence Thresh", &dragSceneConfThresh, 0.01f, 0.0f, 1.0f)) appCfg.sceneConfidenceThreshold = dragSceneConfThresh;
                    ImGui::Dummy(ImVec2(0, 8));

                    ImGui::Text("Alignment Thresholds");
                    ImGui::Separator();
                    ImGui::SetNextItemWidth(160); if (ImGui::DragFloat("SSIM Warning", &dragSsimWarn, 0.01f, 0.0f, 1.0f)) th.ssimWarning = dragSsimWarn;
                    ImGui::SetNextItemWidth(160); if (ImGui::DragFloat("SSIM Fail", &dragSsimFail, 0.01f, 0.0f, 1.0f)) th.ssimFail = dragSsimFail;
                    ImGui::SetNextItemWidth(160); if (ImGui::DragFloat("PixDiff Warning %", &dragPixWarn, 0.1f, 0.0f, 100.0f)) th.pixelDiffWarning = dragPixWarn;
                    ImGui::SetNextItemWidth(160); if (ImGui::DragFloat("PixDiff Fail %", &dragPixFail, 0.1f, 0.0f, 100.0f)) th.pixelDiffFail = dragPixFail;
                    ImGui::SetNextItemWidth(160); if (ImGui::DragFloat("Brightness \xce\x94 Warning", &dragBrightWarn, 0.001f, 0.0f, 1.0f)) th.brightnessDeltaWarning = dragBrightWarn;
                    ImGui::SetNextItemWidth(160); if (ImGui::DragFloat("Brightness \xce\x94 Fail", &dragBrightFail, 0.001f, 0.0f, 1.0f)) th.brightnessDeltaFail = dragBrightFail;

                    ImGui::Dummy(ImVec2(0, 8));
                    ImGui::Text("Performance");
                    ImGui::Separator();
                    ImGui::SetNextItemWidth(160); ImGui::DragInt("Target FPS", &appCfg.targetFps, 1, 1, 240);

                    if (ImGui::Button("Apply Thresholds")) {
                        if (m_setAppConfig) m_setAppConfig(appCfg);
                        spdlog::info("Thresholds updated via settings dialog");
                    }
                } else {
                    ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "Config not available");
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    // Bottom buttons
    ImGui::Separator();
    if (ImGui::Button("Enable All")) {
        toggles = CheckToggles{};
        if (m_updateChecks) m_updateChecks(toggles);
    }
    ImGui::SameLine();
    if (ImGui::Button("Disable All")) {
        CheckToggles allOff;
        memset(&allOff, 0, sizeof(allOff));
        if (m_updateChecks) m_updateChecks(allOff);
    }
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        m_showSettings = false;
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Hotkey Reference Dialog
// ---------------------------------------------------------------------------
void Overlay::renderHotkeyDialog() {
    ImGui::SetNextWindowSize(ImVec2(420, 360), ImGuiCond_FirstUseEver);
    ImGui::Begin("Keyboard Shortcuts", &m_showHotkeys);

    if (ImGui::BeginTable("Hotkeys", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Action");
        ImGui::TableHeadersRow();

        auto hotkey = [](const char* key, const char* action) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); ImGui::Text("%s", key);
            ImGui::TableNextColumn(); ImGui::Text("%s", action);
        };

        hotkey("F1", "Toggle overlay visibility");
        hotkey("F2", "Freeze / unfreeze analysis");
        hotkey("F3", "Stereo Difference Overlay");
        hotkey("F4", "Difference Heatmap");
        hotkey("F5", "Toggle Normal / Heatmap");
        hotkey("F6", "Take screenshot");
        hotkey("F7", "Start / stop logging (CSV + JSON)");
        hotkey("F8", "Start / stop recording");
        hotkey("F9", "Set baseline (capture stereo model)");
        hotkey("Ctrl+F1", "Edge Comparison mode");
        hotkey("Ctrl+F2", "Feature Match Overlay mode");
        hotkey("Ctrl+F3", "Histogram View mode");
        hotkey("Ctrl+F4", "Blur Map mode");
        hotkey("Ctrl+F5", "Blink Left (left eye fullscreen)");
        hotkey("Ctrl+F6", "Blink Right (right eye fullscreen)");
        hotkey("Ctrl+F7", "Disparity Heatmap mode");

        ImGui::EndTable();
    }

    ImGui::Dummy(ImVec2(0, 4));
    if (ImGui::Button("Close")) {
        m_showHotkeys = false;
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// About Dialog
// ---------------------------------------------------------------------------
void Overlay::renderAboutDialog() {
    ImGui::SetNextWindowSize(ImVec2(380, 220), ImGuiCond_FirstUseEver);
    ImGui::Begin("About Stereo Inspector", &m_showAbout);

    ImGui::TextColored(ImVec4(0.3f, 0.65f, 1.0f, 1.0f), "Stereo Inspector");
    ImGui::SameLine();
    ImGui::Text("v1.0.0");
    ImGui::Separator();
    ImGui::Text("Real-time VR stereo inconsistency detection tool.");
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::TextWrapped("Captures side-by-side VR output via DXGI Desktop Duplication, "
                       "computes dense stereo correspondence, warps the right eye to the "
                       "left coordinate system, and compares aligned images to detect "
                       "genuine rendering defects.");
    ImGui::Dummy(ImVec2(0, 4));
    ImGui::Text("Built with: C++20, OpenCV, DirectX 11, Dear ImGui");
    ImGui::Text("Platform: Windows 10/11");
    ImGui::Separator();
    ImGui::Text("GitHub: github.com/agentnuclear/StereoInspector");

    if (ImGui::Button("Close")) {
        m_showAbout = false;
    }

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Summary Tab
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

    // Viz mode
    const char* modeName = VisualizationModeName(m_vizMode.load());
    ImGui::Text("Viz Mode:"); ImGui::SameLine(80);
    ImGui::TextColored(ImVec4(0.6f,0.6f,0.8f,1.0f), "%s", modeName);

    // Issue count
    int totalIssues = (int)(result.deviations.size() + result.detectedIssues.size());
    if (totalIssues > 0) {
        ImGui::Text("Issues:"); ImGui::SameLine(80);
        ImVec4 ic2 = totalIssues > 5 ? ImVec4(1.0f,0.3f,0.3f,1.0f) : ImVec4(1.0f,0.6f,0.0f,1.0f);
        ImGui::TextColored(ic2, "%d (%zu dev, %zu reg)",
            totalIssues, result.deviations.size(), result.detectedIssues.size());
    } else {
        ImGui::Text("Issues:"); ImGui::SameLine(80);
        ImGui::TextColored(ImVec4(0.3f,0.85f,0.3f,1.0f), "None");
    }
}

// ---------------------------------------------------------------------------
// Status Tab
// ---------------------------------------------------------------------------
void Overlay::renderStatusTab(const AnalysisResult& result, const FrameTime& ft) {
    renderControlsArea(result);

    ImGui::Separator();

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
            ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f), "No baseline \u2014 sync to enable comparison");
        }
    }

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
// Controls area
// ---------------------------------------------------------------------------
void Overlay::renderControlsArea(const AnalysisResult& result) {
    bool hasBase = result.stereoModel.active;
    float bw = ImGui::GetContentRegionAvail().x;

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

    ImGui::Dummy(ImVec2(0, 4));
    renderModeButtons(m_vizMode.load());
}

// ---------------------------------------------------------------------------
// Metrics Tab
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
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f), "%-9s %6s %6s %s", "", "Base", "Cur", "\u0394");
        ImGui::Separator();

        if (ImGui::CollapsingHeader("Alignment", ImGuiTreeNodeFlags_DefaultOpen)) {
            row("SSIM",       result.residual.alignedSSIM,           result.stereoModel.refAlignedSSIM,1.0,"%.4f");
            row("PixDiff %",  result.residual.alignedPixDiffPercent, result.stereoModel.refAlignedPixDiffPercent,1.0,"%.2f");
            row("Bright \u0394",result.residual.alignedBrightnessDelta,result.stereoModel.refAlignedBrightnessDelta,1.0,"%.4f");
            row("Edge Sim",   result.residual.alignedEdgeSimilarity,  result.stereoModel.refAlignedEdgeSimilarity,1.0,"%.4f");
            row("Hist Corr",  result.residual.alignedHistogramCorrelation,result.stereoModel.refAlignedHistogramCorrelation,1.0,"%.4f");
            row("Occlusion",  result.residual.occlusionRatio,         result.stereoModel.refOcclusionRatio,100.0,"%.1f%%");
        }

        if (ImGui::CollapsingHeader("Disparity & Asymmetry")) {
            row("Disp Mean",  result.disparity.meanDisparity,         result.stereoModel.refDisparityMean,1.0,"%.1f");
            row("Bloom Asym", result.asymmetry.bloomAsymmetry,        result.stereoModel.refBloomAsymmetry,1.0,"%.4f");
            row("Shadow",     result.asymmetry.shadowAsymmetry,       result.stereoModel.refShadowAsymmetry,1.0,"%.4f");
        }

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
// Issues Tab
// ---------------------------------------------------------------------------
void Overlay::renderIssuesTab(const AnalysisResult& result) {
    if (result.stereoStatus == StereoStatus::UNKNOWN) {
        ImGui::TextColored(ImVec4(0.5f,0.5f,0.5f,1.0f),
            "Stereo comparison unavailable \u2014 scene too dark or featureless.");
        return;
    }

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

    int validCount = 0;
    for (auto& iss : result.detectedIssues) { if (!iss.isInvalidRegion) validCount++; }

    if (validCount > 0) {
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::TextColored(ImVec4(1.0f,0.6f,0.2f,1.0f), "Region Issues (%d)", validCount);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 8));
        float availH = ImGui::GetContentRegionAvail().y;
        float listH = m_selectedIssueIndex >= 0 ? availH * 0.45f : availH * 0.85f;
        ImGui::BeginChild("IssueList", ImVec2(0, listH), true);

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

    if (result.deviations.empty() && result.detectedIssues.empty() && result.stereoStatus != StereoStatus::UNKNOWN) {
        if (!result.stereoModel.active && !result.issues.empty()) {
            ImGui::TextColored(ImVec4(1.0f,0.6f,0.0f,1.0f), "Issues (no baseline):");
            for (size_t i = 0; i < std::min((size_t)5, result.issues.size()); i++)
                ImGui::Text("  - %s", result.issues[i].c_str());
        } else {
            ImGui::TextColored(ImVec4(0.3f,0.85f,0.3f,1.0f), "No issues detected.");
        }
    }

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
// Graphs Tab
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

// ===========================================================================
// renderFrame – called every iteration of main loop
// ===========================================================================
void Overlay::renderFrame() {
    if (!m_initialized) return;

    m_d3dContext->OMSetRenderTargets(1, m_rtView.GetAddressOf(), nullptr);
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    m_d3dContext->ClearRenderTargetView(m_rtView.Get(), clearColor);

    if (!m_captureSafe.load()) {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        renderSyncFeedback();

        if (m_visible.load()) {
            renderMainWindow();
        }

        // Update visualization texture (always, even if hidden, for quick resume)
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

        ImGui::Render();
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
