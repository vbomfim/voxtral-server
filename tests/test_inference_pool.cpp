/// @file test_inference_pool.cpp
/// @brief Developer unit tests for InferencePool.
///
/// Tests queue management, deadline expiry, shutdown, and callback delivery.
/// Uses MockBackend for deterministic behavior.

#include <gtest/gtest.h>

#include "tts/inference_pool.hpp"
#include "tts/mock_backend.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace tts;
using namespace std::chrono_literals;

// ============================================================================
// Helpers
// ============================================================================

namespace {

/// Create a shared MockBackend, pre-initialized.
std::shared_ptr<MockBackend> make_mock() {
    auto mock = std::make_shared<MockBackend>();
    (void)mock->initialize("/fake/model");
    return mock;
}

/// Create an InferenceJob with a deadline offset from now.
InferenceJob make_job(
    std::function<void(SynthesisResult)> on_success = nullptr,
    std::function<void(std::string)> on_error = nullptr,
    int deadline_offset_ms = 30000)
{
    InferenceJob job;
    job.request.text = "Test text";
    job.request.voice = "neutral_female";
    job.deadline = std::chrono::steady_clock::now()
                 + std::chrono::milliseconds(deadline_offset_ms);
    job.on_success = std::move(on_success);
    job.on_error = std::move(on_error);
    return job;
}

}  // namespace

// ============================================================================
// Construction and destruction
// ============================================================================

TEST(InferencePool, ConstructAndDestroy) {
    auto mock = make_mock();
    { InferencePool pool(mock, /*max_queue_depth=*/5, /*timeout=*/60); }
    SUCCEED();
}

TEST(InferencePool, NullBackendThrows) {
    EXPECT_THROW(
        InferencePool(nullptr, 5, 60),
        std::invalid_argument);
}

TEST(InferencePool, InvalidQueueDepthThrows) {
    auto mock = make_mock();
    EXPECT_THROW(
        InferencePool(mock, 0, 60),
        std::invalid_argument);
}

TEST(InferencePool, InvalidTimeoutThrows) {
    auto mock = make_mock();
    EXPECT_THROW(
        InferencePool(mock, 5, 0),
        std::invalid_argument);
}

// ============================================================================
// Submit and receive result
// ============================================================================

TEST(InferencePool, SubmitAndReceiveResult) {
    auto mock = make_mock();
    InferencePool pool(mock, 10, 60);

    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    SynthesisResult received;

    auto job = make_job(
        [&](SynthesisResult result) {
            std::lock_guard lk(mu);
            received = std::move(result);
            done = true;
            cv.notify_one();
        });

    EXPECT_TRUE(pool.submit(std::move(job)));

    {
        std::unique_lock lk(mu);
        ASSERT_TRUE(cv.wait_for(lk, 5s, [&] { return done; }))
            << "Callback not invoked within 5 seconds";
    }

    EXPECT_EQ(received.sample_rate, 24000);
    EXPECT_EQ(mock->call_count.load(), 1);
}

// ============================================================================
// Queue full returns false
// ============================================================================

TEST(InferencePool, QueueFullReturnsFalse) {
    auto mock = make_mock();
    mock->latency_ms = 200;  // Slow inference to fill the queue

    InferencePool pool(mock, /*max_queue_depth=*/2, 60);

    // Submit 2 jobs to fill the queue (1 being processed + 1 waiting)
    // First job will be picked up by worker, second stays in queue
    EXPECT_TRUE(pool.submit(make_job()));
    // Give worker a moment to pick up first job
    std::this_thread::sleep_for(20ms);
    EXPECT_TRUE(pool.submit(make_job()));
    EXPECT_TRUE(pool.submit(make_job()));

    // Now the queue should be full (2 pending + 1 active)
    // The 4th submit should fail
    EXPECT_FALSE(pool.submit(make_job()));
}

// ============================================================================
// Deadline expiry
// ============================================================================

TEST(InferencePool, DeadlineExpiryTriggersError) {
    auto mock = make_mock();
    mock->latency_ms = 200;  // First job takes 200ms

    InferencePool pool(mock, 10, 60);

    // Submit a slow job first
    std::mutex mu;
    std::condition_variable cv;
    int success_count = 0;
    int error_count = 0;
    std::string error_msg;

    auto slow_job = make_job(
        [&](SynthesisResult) {
            std::lock_guard lk(mu);
            ++success_count;
            cv.notify_one();
        });
    EXPECT_TRUE(pool.submit(std::move(slow_job)));

    // Submit an already-expired job (deadline in the past)
    auto expired_job = make_job(
        nullptr,
        [&](std::string err) {
            std::lock_guard lk(mu);
            error_msg = std::move(err);
            ++error_count;
            cv.notify_one();
        },
        -1000  // 1 second in the past
    );
    EXPECT_TRUE(pool.submit(std::move(expired_job)));

    // Wait for both callbacks
    {
        std::unique_lock lk(mu);
        ASSERT_TRUE(cv.wait_for(lk, 5s, [&] { return success_count >= 1 && error_count >= 1; }))
            << "Expected both callbacks within 5s, got success=" << success_count
            << " error=" << error_count;
    }

    EXPECT_EQ(error_msg, "Request timed out in queue");
    EXPECT_GE(pool.expired_jobs(), 1);
}

// ============================================================================
// Shutdown behavior
// ============================================================================

TEST(InferencePool, ShutdownStopsAccepting) {
    auto mock = make_mock();
    InferencePool pool(mock, 10, 60);

    EXPECT_TRUE(pool.is_accepting());
    pool.shutdown();
    EXPECT_FALSE(pool.is_accepting());
    EXPECT_FALSE(pool.submit(make_job()));
}

TEST(InferencePool, ShutdownDrainsInFlight) {
    auto mock = make_mock();
    mock->latency_ms = 50;

    InferencePool pool(mock, 10, 60);

    std::atomic<int> completed{0};

    for (int i = 0; i < 3; ++i) {
        auto job = make_job([&](SynthesisResult) {
            completed.fetch_add(1, std::memory_order_relaxed);
        });
        pool.submit(std::move(job));
    }

    pool.shutdown();

    // After shutdown, all submitted jobs should have been processed
    EXPECT_EQ(completed.load(), 3);
}

// ============================================================================
// queue_depth() and active_jobs() accuracy
// ============================================================================

TEST(InferencePool, QueueDepthAccurate) {
    auto mock = make_mock();
    mock->latency_ms = 300;  // Hold the worker busy

    InferencePool pool(mock, 10, 60);

    // Submit first job — worker picks it up
    pool.submit(make_job());
    std::this_thread::sleep_for(30ms);  // Let worker grab it

    // Submit more jobs — these queue up
    pool.submit(make_job());
    pool.submit(make_job());

    // Queue depth should be 2 (worker has 1 active, 2 pending)
    EXPECT_EQ(pool.queue_depth(), 2);
    EXPECT_EQ(pool.active_jobs(), 1);
}

TEST(InferencePool, ActiveJobsZeroWhenIdle) {
    auto mock = make_mock();
    InferencePool pool(mock, 10, 60);

    // No jobs submitted
    EXPECT_EQ(pool.active_jobs(), 0);
    EXPECT_EQ(pool.queue_depth(), 0);
}

// ============================================================================
// Multiple sequential jobs processed in order
// ============================================================================

TEST(InferencePool, JobsProcessedInOrder) {
    auto mock = make_mock();
    InferencePool pool(mock, 10, 60);

    std::mutex mu;
    std::condition_variable cv;
    std::vector<std::string> order;
    int done_count = 0;
    const int total = 5;

    for (int i = 0; i < total; ++i) {
        InferenceJob job;
        job.request.text = "job-" + std::to_string(i);
        job.request.voice = "neutral_female";
        job.deadline = std::chrono::steady_clock::now() + 30s;
        job.on_success = [&, i](SynthesisResult) {
            std::lock_guard lk(mu);
            order.push_back("job-" + std::to_string(i));
            ++done_count;
            cv.notify_one();
        };
        pool.submit(std::move(job));
    }

    {
        std::unique_lock lk(mu);
        ASSERT_TRUE(cv.wait_for(lk, 10s, [&] { return done_count >= total; }))
            << "Not all jobs completed within 10 seconds";
    }

    // Single worker = FIFO order
    ASSERT_EQ(order.size(), static_cast<size_t>(total));
    for (int i = 0; i < total; ++i) {
        EXPECT_EQ(order[static_cast<size_t>(i)], "job-" + std::to_string(i));
    }
}

// ============================================================================
// Backend failure calls on_error
// ============================================================================

TEST(InferencePool, BackendFailureCallsOnError) {
    auto mock = make_mock();
    mock->should_fail = true;
    mock->fail_message = "GPU explosion";

    InferencePool pool(mock, 10, 60);

    std::mutex mu;
    std::condition_variable cv;
    bool error_received = false;
    std::string error_msg;

    auto job = make_job(
        nullptr,
        [&](std::string err) {
            std::lock_guard lk(mu);
            error_msg = std::move(err);
            error_received = true;
            cv.notify_one();
        });

    pool.submit(std::move(job));

    {
        std::unique_lock lk(mu);
        ASSERT_TRUE(cv.wait_for(lk, 5s, [&] { return error_received; }))
            << "on_error not invoked within 5 seconds";
    }

    EXPECT_NE(error_msg.find("GPU explosion"), std::string::npos);
}

// ============================================================================
// is_accepting initial state
// ============================================================================

TEST(InferencePool, IsAcceptingInitially) {
    auto mock = make_mock();
    InferencePool pool(mock, 10, 60);
    EXPECT_TRUE(pool.is_accepting());
}

// ============================================================================
// expired_jobs counter starts at zero
// ============================================================================

TEST(InferencePool, ExpiredJobsStartsAtZero) {
    auto mock = make_mock();
    InferencePool pool(mock, 10, 60);
    EXPECT_EQ(pool.expired_jobs(), 0);
}
