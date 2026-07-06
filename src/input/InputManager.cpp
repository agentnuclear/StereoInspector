#include "InputManager.h"
#include <spdlog/spdlog.h>

struct InputManager::Impl {
    std::unordered_map<int, std::pair<UINT, HotkeyCallback>> hotkeys;
};

InputManager::InputManager()
    : m_impl(std::make_unique<Impl>()) {}

InputManager::~InputManager() {
    shutdown();
}

bool InputManager::initialize() {
    if (m_initialized.exchange(true)) return true;

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = "StereoInspectorInput";

    if (!RegisterClassEx(&wc)) {
        DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            spdlog::error("Failed to register input window class: {}", err);
            m_initialized = false;
            return false;
        }
    }

    m_hwnd = CreateWindowEx(0, "StereoInspectorInput", "SI Input",
                            WS_OVERLAPPEDWINDOW, 0, 0, 0, 0,
                            nullptr, nullptr, wc.hInstance, this);

    if (!m_hwnd) {
        spdlog::error("Failed to create input window");
        m_initialized = false;
        return false;
    }

    SetWindowLongPtr(m_hwnd, GWLP_USERDATA, (LONG_PTR)this);

    spdlog::info("Input manager initialized");
    return true;
}

void InputManager::shutdown() {
    m_initialized = false;

    for (auto& [id, pair] : m_impl->hotkeys) {
        UnregisterHotKey(m_hwnd, id);
    }
    m_impl->hotkeys.clear();

    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }

    UnregisterClass("StereoInspectorInput", GetModuleHandle(nullptr));
}

bool InputManager::isInitialized() const {
    return m_initialized.load();
}

bool InputManager::m_registerHotkey(int id, UINT vk, UINT modifiers) {
    if (!m_hwnd) return false;
    return RegisterHotKey(m_hwnd, id, modifiers, vk) != 0;
}

void InputManager::registerHotkey(int id, UINT vk, HotkeyCallback callback, UINT modifiers) {
    if (m_registerHotkey(id, vk, modifiers)) {
        m_impl->hotkeys[id] = {vk, std::move(callback)};
        spdlog::debug("Registered hotkey {} with VK_{:02X}", id, vk);
    } else {
        spdlog::warn("Failed to register hotkey {} VK_{:02X}", id, vk);
    }
}

void InputManager::unregisterHotkey(int id) {
    UnregisterHotKey(m_hwnd, id);
    m_impl->hotkeys.erase(id);
}

void InputManager::setOverlayVisible(bool visible) { m_overlayVisible = visible; }
void InputManager::setFrozen(bool frozen) { m_frozen = frozen; }
void InputManager::setVisualizationMode(int mode) { m_visualizationMode = mode; }
void InputManager::triggerScreenshot() {}
void InputManager::toggleLogging() { m_logging = !m_logging; }
void InputManager::toggleRecording() { m_recording = !m_recording; }

bool InputManager::isOverlayVisible() const { return m_overlayVisible.load(); }
bool InputManager::isFrozen() const { return m_frozen.load(); }
int InputManager::visualizationMode() const { return m_visualizationMode.load(); }
bool InputManager::isLogging() const { return m_logging.load(); }
bool InputManager::isRecording() const { return m_recording.load(); }

LRESULT CALLBACK InputManager::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<InputManager*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    if (msg == WM_HOTKEY && self) {
        int id = (int)wParam;
        auto it = self->m_impl->hotkeys.find(id);
        if (it != self->m_impl->hotkeys.end()) {
            it->second.second();
            return 0;
        }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void InputManager::processMessages() {
    MSG msg;
    while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
}
