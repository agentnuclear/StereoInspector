#pragma once
#include <Windows.h>
#include <functional>
#include <unordered_map>
#include <atomic>
#include <vector>
#include <memory>

class InputManager {
public:
    using HotkeyCallback = std::function<void()>;

    InputManager();
    ~InputManager();

    bool initialize();
    void shutdown();
    bool isInitialized() const;

    void registerHotkey(int id, UINT vk, HotkeyCallback callback, UINT modifiers = MOD_NOREPEAT);
    void unregisterHotkey(int id);

    void setOverlayVisible(bool visible);
    void setFrozen(bool frozen);
    void setVisualizationMode(int mode);
    void triggerScreenshot();
    void toggleLogging();
    void toggleRecording();

    bool isOverlayVisible() const;
    bool isFrozen() const;
    int visualizationMode() const;
    bool isLogging() const;
    bool isRecording() const;

    void processMessages();

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    HWND m_hwnd = nullptr;
    std::atomic<bool> m_initialized{false};
    std::atomic<bool> m_overlayVisible{true};
    std::atomic<bool> m_frozen{false};
    std::atomic<int> m_visualizationMode{0};
    std::atomic<bool> m_logging{false};
    std::atomic<bool> m_recording{false};

    bool m_registerHotkey(int id, UINT vk, UINT modifiers = MOD_NOREPEAT);
};
