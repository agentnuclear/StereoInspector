#include <Windows.h>
#include <shellapi.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/wincolor_sink.h>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>

#include "config/Config.h"
#include "capture/DxgiCapture.h"
#include "core/Frame.h"
#include "core/Types.h"
#include "analysis/Analyzer.h"
#include "analysis/DataCollector.h"
#include "analysis/modules/SsimAnalyzer.h"
#include "analysis/modules/PixelDiffAnalyzer.h"
#include "analysis/modules/HistogramAnalyzer.h"
#include "analysis/modules/EdgeAnalyzer.h"
#include "analysis/modules/OrbAnalyzer.h"
#include "analysis/modules/OpticalFlowAnalyzer.h"
#include "analysis/modules/BlurAnalyzer.h"
#include "analysis/modules/BrightnessAnalyzer.h"
#include "analysis/modules/ContrastAnalyzer.h"
#include "analysis/modules/BloomAnalyzer.h"
#include "analysis/modules/ShadowAnalyzer.h"
#include "analysis/modules/StereoOffsetAnalyzer.h"
#include "analysis/modules/OcrAnalyzer.h"
#include "overlay/Overlay.h"
#include "input/InputManager.h"
#include "logging/Logger.h"
#include "logging/ReportGenerator.h"
#include "visualization/Visualizer.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>

namespace {
    std::atomic<bool> g_running{true};
    std::atomic<bool> g_logging{false};
    std::atomic<bool> g_recording{false};
    std::atomic<int> g_screenshotTrigger{0};
}

static std::unique_ptr<AnalyzerPipeline> createAnalyzer(const AppConfig& config) {
    auto analyzer = std::make_unique<AnalyzerPipeline>(config);
    analyzer->registerAnalyzer(std::make_unique<SsimAnalyzer>());
    analyzer->registerAnalyzer(std::make_unique<PixelDiffAnalyzer>());
    analyzer->registerAnalyzer(std::make_unique<HistogramAnalyzer>());
    analyzer->registerAnalyzer(std::make_unique<EdgeAnalyzer>());
    analyzer->registerAnalyzer(std::make_unique<OrbAnalyzer>());
    analyzer->registerAnalyzer(std::make_unique<OpticalFlowAnalyzer>());
    analyzer->registerAnalyzer(std::make_unique<BlurAnalyzer>());
    analyzer->registerAnalyzer(std::make_unique<BrightnessAnalyzer>());
    analyzer->registerAnalyzer(std::make_unique<ContrastAnalyzer>());
    analyzer->registerAnalyzer(std::make_unique<BloomAnalyzer>());
    analyzer->registerAnalyzer(std::make_unique<ShadowAnalyzer>());
    analyzer->registerAnalyzer(std::make_unique<StereoOffsetAnalyzer>());
    if (config.enableOcr) {
        analyzer->registerAnalyzer(std::make_unique<OcrAnalyzer>());
    }
    spdlog::info("Registered {} analyzers", analyzer->analyzers().size());
    return analyzer;
}

static int runCollectMode(const std::string& outputDir, const AppConfig& config) {
    spdlog::info("Data collection mode: output={}", outputDir);

    LatestFrameBuffer frameBuffer;
    auto analyzer = createAnalyzer(config);
    DxgiCapture capture(config);

    DataCollector collector(outputDir);

    capture.start(frameBuffer);
    analyzer->start(frameBuffer);

    int idleFrames = 0;
    while (true) {
        auto frame = frameBuffer.load();
        if (!frame) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            idleFrames++;
            if (idleFrames > 300) { // 3 seconds of no frames
                spdlog::warn("No frames received, stopping collection");
                break;
            }
            continue;
        }
        idleFrames = 0;

        analyzer->waitForFrame();
        AnalysisResult result = analyzer->getLatestResult();

        if (!result.sceneConfidence.reliable) {
            spdlog::warn("Frame {}: low scene confidence ({:.2f}), skipping",
                         result.frameNumber, result.sceneConfidence.overall);
            continue;
        }

        if (!collector.onFrame(result, frame->leftEye, frame->rightEye)) {
            spdlog::info("Collection complete: {} frames written", collector.framesWritten());
            break;
        }

        if (collector.framesWritten() % 10 == 0) {
            spdlog::info("Collected {} frames...", collector.framesWritten());
        }
    }

    capture.stop();
    analyzer->stop();
    spdlog::info("Collection finished: {} frames written to {}", collector.framesWritten(), outputDir);
    return 0;
}

static void setupLogging() {
    try {
        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>("stereo_inspector.log", true);
        auto consoleSink = std::make_shared<spdlog::sinks::wincolor_stdout_sink_mt>();
        std::vector<spdlog::sink_ptr> sinks = {consoleSink, fileSink};
        auto logger = std::make_shared<spdlog::logger>("StereoInspector", sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::info);
        logger->set_pattern("[%H:%M:%S.%e] [%^%l%$] %v");
        spdlog::set_default_logger(logger);
        spdlog::info("Stereo Inspector v1.0.0 starting...");
    } catch (const std::exception& e) {
        spdlog::error("Failed to initialize logging: {}", e.what());
    }
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    setupLogging();

    // Parse command line for --collect PATH
    std::string collectDir;
    {
        int argc;
        LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv) {
            for (int i = 1; i < argc; i++) {
                std::wstring arg(argv[i]);
                if (arg == L"--collect" && i + 1 < argc) {
                    int len = WideCharToMultiByte(CP_UTF8, 0, argv[i + 1], -1, nullptr, 0, nullptr, nullptr);
                    collectDir.resize(len - 1);
                    WideCharToMultiByte(CP_UTF8, 0, argv[i + 1], -1, collectDir.data(), len, nullptr, nullptr);
                    i++;
                }
            }
            LocalFree(argv);
        }
    }

    AppConfig config = AppConfig::loadFromFile("config.json");

    if (!collectDir.empty()) {
        return runCollectMode(collectDir, config);
    }

    LatestFrameBuffer frameBuffer;
    Overlay overlay;
    InputManager inputManager;
    StereoLogger logger(config.logging);
    ReportGenerator reportGen(config.logging);
    DxgiCapture capture(config);
    auto analyzer = createAnalyzer(config);

    if (!overlay.initialize(hInstance, nCmdShow)) {
        spdlog::error("Failed to initialize overlay");
        return 1;
    }

    if (!inputManager.initialize()) {
        spdlog::error("Failed to initialize input manager");
        return 1;
    }

    overlay.setResultCallback([&]() { return analyzer->getLatestResult(); });
    overlay.setTimeCallback([&]() { return analyzer->getFrameTime(); });
    overlay.setFrameCallback([&]() {
        auto frame = frameBuffer.load();
        if (frame) return frame->frame.clone();
        return cv::Mat();
    });
    overlay.setHistoryCallback([&]() { return analyzer->getHistory(); });
    overlay.setLayoutCallback([&]() { return capture.currentLayout(); });
    overlay.setSyncCallback([&]() {
        auto frame = frameBuffer.load();
        if (frame && !frame->leftEye.empty() && !frame->rightEye.empty()) {
            analyzer->setBaseline(frame->leftEye, frame->rightEye);
        }
    });
    overlay.setClearBaselineCallback([&]() {
        analyzer->clearBaseline();
        spdlog::info("Baseline cleared via UI");
    });
    overlay.setConfigCallback([&](const CheckToggles& toggles) {
        analyzer->setCheckToggles(toggles);
        spdlog::debug("Check toggles updated");
    });
    overlay.setGetConfigCallback([&]() {
        return analyzer->getConfig().checks;
    });
    overlay.setGetAppConfigCallback([&]() -> AppConfig {
        return config;
    });
    overlay.setSetAppConfigCallback([&](const AppConfig& newConfig) {
        config = newConfig;
        spdlog::info("App config updated via settings dialog");
    });
    overlay.setExportReportCallback([&]() {
        auto result = analyzer->getLatestResult();
        std::vector<AnalysisResult> results = {result};
        reportGen.generate(results, {});
        spdlog::info("Report exported");
    });

    int hotkeyId = 1;
    inputManager.registerHotkey(hotkeyId++, VK_F1, [&]() {
        overlay.setVisible(!overlay.isVisible());
        spdlog::info("Overlay toggled: {}", overlay.isVisible() ? "visible" : "hidden");
    });
    inputManager.registerHotkey(hotkeyId++, VK_F2, [&]() {
        overlay.setFrozen(!overlay.isFrozen());
        spdlog::info("Analysis {}", overlay.isFrozen() ? "frozen" : "unfrozen");
    });
    inputManager.registerHotkey(hotkeyId++, VK_F3, [&]() {
        overlay.setVisualizationMode(VisualizationMode::StereoDifferenceOverlay);
        spdlog::info("Visualization: Stereo Difference Overlay");
    });
    inputManager.registerHotkey(hotkeyId++, VK_F4, [&]() {
        overlay.setVisualizationMode(VisualizationMode::DifferenceHeatmap);
        spdlog::info("Visualization: Difference Heatmap");
    });
    inputManager.registerHotkey(hotkeyId++, VK_F5, [&]() {
        auto cur = overlay.visualizationMode();
        if (cur == VisualizationMode::DifferenceHeatmap || cur == VisualizationMode::StereoDifferenceOverlay) {
            overlay.setVisualizationMode(VisualizationMode::Normal);
            spdlog::info("Visualization: Normal");
        } else {
            overlay.setVisualizationMode(VisualizationMode::DifferenceHeatmap);
            spdlog::info("Visualization: Heatmap");
        }
    });
    inputManager.registerHotkey(hotkeyId++, VK_F6, [&]() {
        g_screenshotTrigger = 1;
        spdlog::info("Screenshot triggered");
    });
    inputManager.registerHotkey(hotkeyId++, VK_F7, [&]() {
        g_logging = !g_logging;
        if (g_logging) {
            logger.start();
            spdlog::info("Logging started");
        } else {
            logger.stop();
            spdlog::info("Logging stopped");
            auto results = logger.getSessionResults();
            if (!results.empty()) {
                reportGen.generate(results, {});
            }
        }
    });
    inputManager.registerHotkey(hotkeyId++, VK_F8, [&]() {
        g_recording = !g_recording;
        spdlog::info("Recording {}", g_recording ? "started" : "stopped");
    });
    inputManager.registerHotkey(hotkeyId++, VK_F9, [&]() {
        auto frame = frameBuffer.load();
        if (frame && !frame->leftEye.empty() && !frame->rightEye.empty()) {
            analyzer->setBaseline(frame->leftEye, frame->rightEye);
        }
    });

    UINT ctrl = MOD_CONTROL | MOD_NOREPEAT;
    inputManager.registerHotkey(hotkeyId++, VK_F1, [&]() {
        overlay.setVisualizationMode(VisualizationMode::EdgeComparison);
        spdlog::info("Visualization: Edge Comparison");
    }, ctrl);
    inputManager.registerHotkey(hotkeyId++, VK_F2, [&]() {
        overlay.setVisualizationMode(VisualizationMode::FeatureMatchOverlay);
        spdlog::info("Visualization: Feature Match Overlay");
    }, ctrl);
    inputManager.registerHotkey(hotkeyId++, VK_F3, [&]() {
        overlay.setVisualizationMode(VisualizationMode::HistogramView);
        spdlog::info("Visualization: Histogram View");
    }, ctrl);
    inputManager.registerHotkey(hotkeyId++, VK_F4, [&]() {
        overlay.setVisualizationMode(VisualizationMode::BlurMap);
        spdlog::info("Visualization: Blur Map");
    }, ctrl);
    inputManager.registerHotkey(hotkeyId++, VK_F5, [&]() {
        overlay.setVisualizationMode(VisualizationMode::BlinkLeft);
        spdlog::info("Visualization: Blink Left");
    }, ctrl);
    inputManager.registerHotkey(hotkeyId++, VK_F6, [&]() {
        overlay.setVisualizationMode(VisualizationMode::BlinkRight);
        spdlog::info("Visualization: Blink Right");
    }, ctrl);
    inputManager.registerHotkey(hotkeyId++, VK_F7, [&]() {
        overlay.setVisualizationMode(VisualizationMode::DisparityHeatmap);
        spdlog::info("Visualization: Disparity Heatmap");
    }, ctrl);

    // Wire baseline captured callback → overlay sync feedback
    analyzer->setOnBaselineCaptured([&](uint64_t frameNum, float confidence, const std::string& timestamp) {
        overlay.showSyncFeedback(frameNum, confidence, timestamp);
        spdlog::info("Stereo model captured: frame={}, confidence={:.2f}", frameNum, confidence);
    });

    // Wire baseline refused callback → overlay refused feedback
    analyzer->setOnBaselineRefused([&](const std::string& reason) {
        overlay.showSyncRefused(reason);
        spdlog::warn("Baseline capture refused: {}", reason);
    });

    capture.start(frameBuffer);
    analyzer->start(frameBuffer);

    auto lastFrameTime = std::chrono::steady_clock::now();
    int frameCount = 0;
    double currentFps = 0.0;

    spdlog::info("Stereo Inspector is running");

    while (g_running) {
        inputManager.processMessages();

        if (!overlay.processMessages()) {
            g_running = false;
            break;
        }

        if (!overlay.isFrozen()) {
            overlay.renderFrame();
        }

        inputManager.processMessages();

        if (g_screenshotTrigger.exchange(0)) {
            auto frame = frameBuffer.load();
            if (frame && !frame->frame.empty()) {
                auto result = analyzer->getLatestResult();
                logger.captureScreenshot(frame->frame, result);
            }
        }

        if (g_recording && (frameCount % 30 == 0)) {
            auto frame = frameBuffer.load();
            if (frame && !frame->frame.empty()) {
                auto result = analyzer->getLatestResult();
                logger.captureScreenshot(frame->frame, result);
            }
        }

        if (g_logging) {
            auto result = analyzer->getLatestResult();
            logger.logFrame(result);
        }

        frameCount++;
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - lastFrameTime).count();
        if (elapsed >= 1.0) {
            currentFps = frameCount / elapsed;
            frameCount = 0;
            lastFrameTime = now;

            FrameTime ft = analyzer->getFrameTime();
            ft.fps = currentFps;
            ft.captureFps = capture.captureFps();
        }

        double targetMs = config.targetFps > 0 ? (1000.0 / config.targetFps) : 0;
        auto frameEnd = std::chrono::steady_clock::now();
        double frameMs = std::chrono::duration<double, std::milli>(
            frameEnd - now).count();
        if (frameMs < targetMs && targetMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds((int)(targetMs - frameMs)));
        }

        inputManager.processMessages();
    }

    spdlog::info("Shutting down...");

    capture.stop();
    analyzer->stop();

    if (g_logging) {
        logger.stop();
        auto results = logger.getSessionResults();
        if (!results.empty()) {
            reportGen.generate(results, {});
        }
    }

    overlay.shutdown();
    inputManager.shutdown();

    spdlog::info("Stereo Inspector shutdown complete");
    return 0;
}
