#pragma once

/// @file inference_pool.hpp
/// @brief Bounded inference queue with deadline propagation.
///
/// Single-worker thread pool for TTS inference. Mirrors the openasr
/// InferencePool pattern but adapted for TTS with:
///   - Deadline-based expiry (Delivery Guardian CRITICAL finding)
///   - Separate success/error callbacks
///   - Single worker thread for minimal memory footprint

#include "tts/backend.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace tts {

/// A single unit of work for the inference worker thread.
struct InferenceJob {
    SynthesisRequest request;

    /// Deadline for this job.  If left default-constructed (epoch sentinel),
    /// InferencePool::submit() fills it with `now + request_timeout_seconds`.
    std::chrono::steady_clock::time_point deadline{};

    std::function<void(SynthesisResult)> on_success;
    std::function<void(std::string error)> on_error;
};

/// Bounded, single-worker inference queue for TTS synthesis.
///
/// Thread-safe. submit() is called from HTTP request threads;
/// the worker thread picks jobs, checks deadlines, and runs inference.
///
/// Key behaviors:
///   - submit() returns false when queue is at capacity (caller returns 503)
///   - Worker checks deadline before starting inference — expired jobs get
///     on_error("Request timed out in queue") without wasting GPU time
///   - shutdown() stops accepting, drains in-flight work, joins worker
class InferencePool {
public:
    /// @param backend               Shared TTS backend for inference.
    /// @param max_queue_depth       Maximum pending jobs (beyond this, submit returns false).
    /// @param request_timeout_seconds  Default deadline offset from submission time.
    InferencePool(std::shared_ptr<ITtsBackend> backend,
                  int max_queue_depth,
                  int request_timeout_seconds);

    /// Destructor calls shutdown() if not already called.
    ~InferencePool();

    // Non-copyable, non-movable (owns a thread)
    InferencePool(const InferencePool&) = delete;
    InferencePool& operator=(const InferencePool&) = delete;
    InferencePool(InferencePool&&) = delete;
    InferencePool& operator=(InferencePool&&) = delete;

    /// Submit a job for asynchronous inference.
    /// @return true if enqueued; false if the queue is full.
    bool submit(InferenceJob job);

    /// Stop accepting new jobs, drain in-flight work, join worker thread.
    void shutdown();

    /// Number of jobs waiting in the queue (not yet picked up by the worker).
    [[nodiscard]] int queue_depth() const;

    /// Number of jobs currently being processed by the worker (0 or 1).
    [[nodiscard]] int active_jobs() const;

    /// Whether the pool is still accepting new jobs.
    [[nodiscard]] bool is_accepting() const;

    /// Number of jobs that expired before the worker could process them.
    [[nodiscard]] int expired_jobs() const;

private:
    void worker_loop();

    std::shared_ptr<ITtsBackend> backend_;
    int max_queue_depth_;
    int request_timeout_seconds_;

    std::queue<InferenceJob> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;

    std::thread worker_;
    std::atomic<bool> accepting_{true};
    std::atomic<int> active_{0};
    std::atomic<int> expired_{0};
};

}  // namespace tts
