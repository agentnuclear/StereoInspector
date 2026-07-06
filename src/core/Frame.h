#pragma once
#include "Types.h"
#include <mutex>
#include <atomic>
#include <memory>

class FrameQueue {
public:
    FrameQueue(size_t maxSize = 3);
    ~FrameQueue();

    bool push(std::shared_ptr<CaptureFrame> frame);
    std::shared_ptr<CaptureFrame> pop();
    std::shared_ptr<CaptureFrame> tryPop();
    void clear();
    size_t size() const;
    bool empty() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

class LatestFrameBuffer {
public:
    LatestFrameBuffer();
    ~LatestFrameBuffer();

    void store(std::shared_ptr<CaptureFrame> frame);
    std::shared_ptr<CaptureFrame> load() const;

private:
    mutable std::mutex m_mutex;
    std::shared_ptr<CaptureFrame> m_frame;
};
