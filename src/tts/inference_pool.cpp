/// @file inference_pool.cpp
/// @brief InferencePool implementation — bounded queue + single worker thread.

#include "tts/inference_pool.hpp"

#include <spdlog/spdlog.h>

#include <chrono>
#include <stdexcept>
#include <utility>

namespace tts {

InferencePool::InferencePool(std::shared_ptr<ITtsBackend> backend,
                             int max_queue_depth,
                             int request_timeout_seconds)
    : backend_(std::move(backend))
    , max_queue_depth_(max_queue_depth)
    , request_timeout_seconds_(request_timeout_seconds) {
    if (!backend_) {
        throw std::invalid_argument("InferencePool requires a non-null backend");
    }
    if (max_queue_depth_ < 1) {
        throw std::invalid_argument("max_queue_depth must be >= 1");
    }
    if (request_timeout_seconds_ < 1) {
        throw std::invalid_argument("request_timeout_seconds must be >= 1");
    }

    worker_ = std::thread([this] { worker_loop(); });
}

InferencePool::~InferencePool() {
    shutdown();
}

bool InferencePool::submit(InferenceJob job) {
    {
        std::lock_guard lock(mutex_);
        if (!accepting_.load(std::memory_order_acquire)) {
            return false;
        }
        if (static_cast<int>(queue_.size()) >= max_queue_depth_) {
            spdlog::warn("InferencePool — queue full ({}/{} jobs), rejecting request",
                          queue_.size(), max_queue_depth_);
            return false;
        }
        queue_.push(std::move(job));
    }
    cv_.notify_one();
    return true;
}

void InferencePool::shutdown() {
    {
        std::lock_guard lock(mutex_);
        accepting_.store(false, std::memory_order_release);
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

int InferencePool::queue_depth() const {
    std::lock_guard lock(mutex_);
    return static_cast<int>(queue_.size());
}

int InferencePool::active_jobs() const {
    return active_.load(std::memory_order_acquire);
}

bool InferencePool::is_accepting() const {
    return accepting_.load(std::memory_order_acquire);
}

int InferencePool::expired_jobs() const {
    return expired_.load(std::memory_order_acquire);
}

void InferencePool::worker_loop() {
    while (true) {
        InferenceJob job;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] {
                return !accepting_.load(std::memory_order_acquire) || !queue_.empty();
            });

            if (!accepting_.load(std::memory_order_acquire) && queue_.empty()) {
                return;  // Shutdown: no more work
            }

            job = std::move(queue_.front());
            queue_.pop();
        }

        // Check deadline before starting expensive inference
        auto now = std::chrono::steady_clock::now();
        if (now >= job.deadline) {
            expired_.fetch_add(1, std::memory_order_relaxed);
            spdlog::warn("InferencePool — job expired in queue, skipping inference");
            if (job.on_error) {
                job.on_error("Request timed out in queue");
            }
            continue;
        }

        // Run inference
        active_.fetch_add(1, std::memory_order_acq_rel);
        try {
            auto result = backend_->synthesize(job.request);
            if (job.on_success) {
                job.on_success(std::move(result));
            }
        } catch (const std::exception& e) {
            spdlog::error("InferencePool — inference failed: {}", e.what());
            if (job.on_error) {
                job.on_error(std::string("Inference failed: ") + e.what());
            }
        }
        active_.fetch_sub(1, std::memory_order_acq_rel);
    }
}

}  // namespace tts
