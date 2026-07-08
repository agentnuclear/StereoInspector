#pragma once
#include "core/Types.h"
#include "config/Config.h"
#include "visualization/Visualizer.h"
#include <imgui.h>
#include <atomic>
#include <memory>
#include <thread>
#include <functional>
#include <d3d11.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

class Overlay {
public:
    Overlay();
    ~Overlay();

    bool initialize(HINSTANCE hInstance, int nCmdShow);
    void shutdown();
    bool isInitialized() const;

    using ResultCallback = std::function<AnalysisResult()>;
    using TimeCallback = std::function<FrameTime()>;
    using FrameCallback = std::function<cv::Mat()>;
    using HistoryCallback = std::function<MetricHistory()>;
    using LayoutCallback = std::function<StereoLayout()>;
    using SyncCallback = std::function<void()>;
    using ClearBaselineCallback = std::function<void()>;
    using ConfigCallback = std::function<void(const CheckToggles&)>;
    using GetConfigCallback = std::function<CheckToggles()>;
    using GetAppConfigCallback = std::function<AppConfig()>;
    using SetAppConfigCallback = std::function<void(const AppConfig&)>;
    using ExportReportCallback = std::function<void()>;

    void setResultCallback(ResultCallback resultFn);
    void setTimeCallback(TimeCallback timeFn);
    void setFrameCallback(FrameCallback frameFn);
    void setHistoryCallback(HistoryCallback historyFn);
    void setLayoutCallback(LayoutCallback layoutFn);
    void setSyncCallback(SyncCallback syncFn);
    void setClearBaselineCallback(ClearBaselineCallback clearFn);
    void setConfigCallback(ConfigCallback cb);
    void setGetConfigCallback(GetConfigCallback cb);
    void setGetAppConfigCallback(GetAppConfigCallback cb);
    void setSetAppConfigCallback(SetAppConfigCallback cb);
    void setExportReportCallback(ExportReportCallback cb);
    void setVisualizationMode(VisualizationMode mode);
    void setVisible(bool visible);
    void setFrozen(bool frozen);
    void showSyncFeedback(uint64_t frameNumber, float confidence, const std::string& timestamp);
    void showSyncRefused(const std::string& reason);
    void resetLayout();

    bool isVisible() const;
    bool isFrozen() const;
    VisualizationMode visualizationMode() const;

    void setCaptureSafe(bool safe);
    bool isCaptureSafe() const;

    void renderFrame();
    bool processMessages();

private:
    bool createWindow(HINSTANCE hInstance);
    bool createD3D11Device();
    bool createSwapChain();
    bool initImGui();
    void applyStyle();

    // Root window with menu + toolbar + panels + status bar
    void renderMainWindow();
    void renderMenuBar();
    void renderToolbar(const AnalysisResult& result, const FrameTime& ft);
    void renderStatusBar(const AnalysisResult& result, const FrameTime& ft);

    // Content panels
    void renderHubPanel(const AnalysisResult& result, const FrameTime& ft, const MetricHistory& history);
    void renderVizPanel();

    // Hub tabs
    void renderSummaryTab(const AnalysisResult& result, const FrameTime& ft);
    void renderStatusTab(const AnalysisResult& result, const FrameTime& ft);
    void renderMetricsTab(const AnalysisResult& result);
    void renderIssuesTab(const AnalysisResult& result);
    void renderGraphsTab(const MetricHistory& history);

    // Modal dialogs
    void renderSettingsDialog();
    void renderHotkeyDialog();
    void renderAboutDialog();

    // Shared UI helpers
    void renderModeButtons(VisualizationMode currentMode);
    void renderControlsArea(const AnalysisResult& result);
    void renderGraphPanel(const MetricHistory& history);
    void renderMiniGraph(const char* label, const HistoryBuffer& data,
                         float graphWidth, float graphHeight,
                         float minVal, float maxVal,
                         unsigned int lineColor);
    void renderSyncFeedback();
    void updateVizTexture(const cv::Mat& image);
    void cleanupD3D();

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd = nullptr;
    HINSTANCE m_hinstance = nullptr;
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_visible{true};
    std::atomic<bool> m_frozen{false};
    std::atomic<bool> m_captureSafe{false};
    std::atomic<VisualizationMode> m_vizMode{VisualizationMode::Normal};

    ComPtr<ID3D11Device> m_d3dDevice;
    ComPtr<ID3D11DeviceContext> m_d3dContext;
    ComPtr<IDXGISwapChain> m_swapChain;
    ComPtr<ID3D11RenderTargetView> m_rtView;
    ComPtr<ID3D11Texture2D> m_backBuffer;

    int m_screenWidth = 0;
    int m_screenHeight = 0;

    ResultCallback m_getResult;
    TimeCallback m_getTime;
    FrameCallback m_getFrame;
    HistoryCallback m_getHistory;
    LayoutCallback m_getLayout;
    SyncCallback m_doSync;
    ClearBaselineCallback m_doClearBaseline;
    ConfigCallback m_updateChecks;
    GetConfigCallback m_getChecks;
    GetAppConfigCallback m_getAppConfig;
    SetAppConfigCallback m_setAppConfig;
    ExportReportCallback m_exportReport;

    struct SyncFeedbackDisplay {
        bool show = false;
        float timer = 0.0f;
        uint64_t frameNumber = 0;
        float confidence = 0.0f;
        std::string timestamp;
    };
    SyncFeedbackDisplay m_syncFeedback;

    struct SyncRefusedDisplay {
        bool show = false;
        float timer = 0.0f;
        std::string reason;
    };
    SyncRefusedDisplay m_syncRefused;

    // Dialog state
    bool m_showSettings = false;
    bool m_showHotkeys = false;
    bool m_showAbout = false;

    // Hub tab tracking
    int m_selectedHubTab = 0;

    // Splitter state
    float m_splitterRatio = 0.35f;
    bool m_draggingSplitter = false;

    int m_selectedIssueIndex = -1;

    // Viz zoom state
    float m_vizZoom = 1.0f;
    float m_vizPanX = 0.0f;
    float m_vizPanY = 0.0f;

    // Issues filter
    char m_issueFilter[64] = "";

    ComPtr<ID3D11ShaderResourceView> m_vizSRV;
    ComPtr<ID3D11Texture2D> m_vizTexture;
    int m_vizTexWidth = 0;
    int m_vizTexHeight = 0;

    Visualizer m_visualizer;
};
