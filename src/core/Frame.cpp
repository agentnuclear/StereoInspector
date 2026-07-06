#include "Frame.h"
#include <queue>

struct FrameQueue::Impl {
    std::queue<std::shared_ptr<CaptureFrame>> queue;
    mutable std::mutex mutex;
    size_t maxSize;
};

FrameQueue::FrameQueue(size_t maxSize)
    : m_impl(std::make_unique<Impl>()) {
    m_impl->maxSize = maxSize;
}

FrameQueue::~FrameQueue() = default;

bool FrameQueue::push(std::shared_ptr<CaptureFrame> frame) {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->queue.size() >= m_impl->maxSize) {
        return false;
    }
    m_impl->queue.push(std::move(frame));
    return true;
}

std::shared_ptr<CaptureFrame> FrameQueue::pop() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    if (m_impl->queue.empty()) return nullptr;
    auto frame = std::move(m_impl->queue.front());
    m_impl->queue.pop();
    return frame;
}

std::shared_ptr<CaptureFrame> FrameQueue::tryPop() {
    return pop();
}

void FrameQueue::clear() {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    while (!m_impl->queue.empty()) m_impl->queue.pop();
}

size_t FrameQueue::size() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->queue.size();
}

bool FrameQueue::empty() const {
    std::lock_guard<std::mutex> lock(m_impl->mutex);
    return m_impl->queue.empty();
}

LatestFrameBuffer::LatestFrameBuffer() = default;
LatestFrameBuffer::~LatestFrameBuffer() = default;

void LatestFrameBuffer::store(std::shared_ptr<CaptureFrame> frame) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_frame = std::move(frame);
}

std::shared_ptr<CaptureFrame> LatestFrameBuffer::load() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_frame;
}
