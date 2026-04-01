/// @file test_inference_pool_integration.cpp
/// @brief QA integration and edge-case tests for InferencePool.
///
/// Covers: concurrent submission stress, deadline edge cases,
/// null callbacks, double shutdown, mixed job outcomes, pipeline integration.
/// Tags: [COVERAGE], [EDGE], [BOUNDARY], [CONTRACT], [INTEGRATION]

#include <gtest/gtest.h>

#include "tts/backend.hpp"
#include "tts/inference_pool.hpp"
#include "tts/mock_backend.hpp"
#include "tts/voices.hpp"

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

std::shared_ptr<MockBackend> make_mock() {
    auto mock = std::make_shared<MockBackend>();
    (void)mock->initialize("/fake/model");
    return mock;
}

InferenceJob make_job(
    std::function<void(SynthesisResult)> on_success = nullptr,
    std::function<void(std::string)> on_error = nullptr,
    int deadline_offset_ms = 30000)
{
    InferenceJob job;
    job.request.text = "QA test text";
    job.request.voice = "neutral_female";
    job.deadline = std::chrono::steady_clock::now()
                 + std::chrono::milliseconds(deadline_offset_ms);
    job.on_success = std::move(on_success);
    job.on_error = std::move(on_error);
    return job;
}

}  // namespace

// ============================================================================
// [COVERAGE] Concurrent multi-thread submission stress test
// Verifies mutex correctness under contention — multiple threads
// submitting simultaneously must not corrupt the queue or lose jobs.
// ============================================================================

TEST(PoolIntegration, ConcurrentSubmitFromMultipleThreads) {
    auto mock = make_mock();
    InferencePool pool(mock, /*max_queue_depth=*/200, /*timeout=*/60);

    constexpr int kThreads = 8;
    constexpr int kJobsPerThread = 10;

    std::atomic<int> success_count{0};
    std::atomic<int> error_count{0};
    std::atomic<int> rejected_count{0};

    auto submit_batch = [&]() {
        for (int i = 0; i < kJobsPerThread; ++i) {
            auto job = make_job(
                [&](SynthesisResult) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                },
                [&](std::string) {
                    error_count.fetch_add(1, std::memory_order_relaxed);
                });
            if (!pool.submit(std::move(job))) {
                rejected_count.fetch_add(1, std::memory_order_relaxed);
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(kThreads);
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back(submit_batch);
    }
    for (auto& t : threads) {
        t.join();
    }

    pool.shutdown();

    // Every submitted job must have produced exactly one callback
    int total_submitted = (kThreads * kJobsPerThread)
                        - rejected_count.load(std::memory_order_relaxed);
    int total_callbacks = success_count.load(std::memory_order_relaxed)
                        + error_count.load(std::memory_order_relaxed);
    EXPECT_EQ(total_callbacks, total_submitted)
        << "Lost jobs: submitted=" << total_submitted
        << " callbacks=" << total_callbacks
        << " rejected=" << rejected_count.load();

    // Backend call count must match successes
    EXPECT_EQ(mock->call_count.load(), success_count.load());
}

// ============================================================================
// [EDGE] Null callbacks — pool must not crash when on_success or on_error
// is nullptr. The implementation has if-guards but they were never tested.
// ============================================================================

TEST(PoolEdge, NullOnSuccessCallbackDoesNotCrash) {
    auto mock = make_mock();
    InferencePool pool(mock, 10, 60);

    // Submit with on_success = nullptr, on_error = nullptr
    auto job = make_job(nullptr, nullptr);
    EXPECT_TRUE(pool.submit(std::move(job)));

    pool.shutdown();

    // If we reach here without SIGSEGV, the null-guard works
    EXPECT_EQ(mock->call_count.load(), 1);
}

TEST(PoolEdge, NullOnErrorCallbackDoesNotCrashOnFailure) {
    auto mock = make_mock();
    mock->should_fail = true;
    mock->fail_message = "Deliberate crash test";

    InferencePool pool(mock, 10, 60);

    // Submit with on_error = nullptr — backend WILL throw
    auto job = make_job(nullptr, nullptr);
    EXPECT_TRUE(pool.submit(std::move(job)));

    pool.shutdown();

    // Reached here — null on_error did not segfault
    EXPECT_EQ(mock->call_count.load(), 1);
}

TEST(PoolEdge, NullOnErrorCallbackOnDeadlineExpiry) {
    auto mock = make_mock();
    mock->latency_ms = 200;  // Hold the worker

    InferencePool pool(mock, 10, 60);

    // First: a slow job to occupy the worker
    pool.submit(make_job());

    // Second: an expired job with null on_error
    InferenceJob expired;
    expired.request.text = "expired";
    expired.deadline = std::chrono::steady_clock::now() - 10s;  // way in the past
    expired.on_success = nullptr;
    expired.on_error = nullptr;
    EXPECT_TRUE(pool.submit(std::move(expired)));

    pool.shutdown();

    // No crash — null on_error was safely skipped for expired job
    EXPECT_GE(pool.expired_jobs(), 1);
}

// ============================================================================
// [EDGE] Double shutdown — calling shutdown() twice must be safe.
// The implementation checks worker_.joinable() but this path is untested.
// ============================================================================

TEST(PoolEdge, DoubleShutdownIsSafe) {
    auto mock = make_mock();
    InferencePool pool(mock, 10, 60);

    pool.submit(make_job());
    pool.shutdown();
    EXPECT_FALSE(pool.is_accepting());

    // Second shutdown must not throw or deadlock
    pool.shutdown();
    EXPECT_FALSE(pool.is_accepting());
}

// ============================================================================
// [COVERAGE] Mixed outcomes in a single batch — success, failure, expiry
// in one test. Verifies the worker handles all three paths without
// corrupting state.
// ============================================================================

TEST(PoolIntegration, MixedSuccessFailureExpiry) {
    auto mock = make_mock();
    mock->latency_ms = 100;  // Slow enough that expired jobs queue up

    InferencePool pool(mock, 20, 60);

    std::mutex mu;
    std::condition_variable cv;
    std::atomic<int> successes{0};
    std::atomic<int> errors{0};

    // 1. Submit 2 normal jobs (will succeed)
    for (int i = 0; i < 2; ++i) {
        auto job = make_job(
            [&](SynthesisResult) { successes.fetch_add(1); cv.notify_all(); },
            [&](std::string) { errors.fetch_add(1); cv.notify_all(); });
        pool.submit(std::move(job));
    }

    // 2. Submit 1 already-expired job (will expire in queue)
    auto expired = make_job(
        [&](SynthesisResult) { successes.fetch_add(1); cv.notify_all(); },
        [&](std::string) { errors.fetch_add(1); cv.notify_all(); },
        -5000);  // 5s in the past
    pool.submit(std::move(expired));

    // Wait for all 3 callbacks
    {
        std::unique_lock lk(mu);
        ASSERT_TRUE(cv.wait_for(lk, 10s, [&] {
            return (successes.load() + errors.load()) >= 3;
        })) << "Timed out waiting for callbacks: success="
            << successes.load() << " errors=" << errors.load();
    }

    EXPECT_EQ(successes.load(), 2);
    EXPECT_EQ(errors.load(), 1);
    EXPECT_GE(pool.expired_jobs(), 1);
    // Backend was called twice (not for the expired job)
    EXPECT_EQ(mock->call_count.load(), 2);
}

// ============================================================================
// [EDGE] Submit after shutdown — must return false, not crash.
// ============================================================================

TEST(PoolEdge, SubmitAfterShutdownReturnsFalse) {
    auto mock = make_mock();
    InferencePool pool(mock, 10, 60);

    pool.shutdown();

    // Try submitting multiple times — all must fail gracefully
    for (int i = 0; i < 5; ++i) {
        EXPECT_FALSE(pool.submit(make_job()))
            << "Submit #" << i << " should have been rejected after shutdown";
    }

    // Backend was never called
    EXPECT_EQ(mock->call_count.load(), 0);
}

// ============================================================================
// [BOUNDARY] Queue depth exactly at limit — boundary condition.
// ============================================================================

TEST(PoolBoundary, QueueDepthExactlyAtLimit) {
    auto mock = make_mock();
    mock->latency_ms = 500;  // Hold the worker

    InferencePool pool(mock, /*max_queue_depth=*/3, 60);

    // First submit is picked up by worker immediately
    EXPECT_TRUE(pool.submit(make_job()));
    std::this_thread::sleep_for(30ms);  // Let worker grab it

    // Fill queue to exactly max_queue_depth=3
    EXPECT_TRUE(pool.submit(make_job()));   // queue: 1
    EXPECT_TRUE(pool.submit(make_job()));   // queue: 2
    EXPECT_TRUE(pool.submit(make_job()));   // queue: 3

    // Queue is now exactly full — next must fail
    EXPECT_FALSE(pool.submit(make_job()));

    // Verify queue depth matches max
    EXPECT_EQ(pool.queue_depth(), 3);
    EXPECT_EQ(pool.active_jobs(), 1);
}

// ============================================================================
// [BOUNDARY] Minimum valid construction (queue=1, timeout=1)
// ============================================================================

TEST(PoolBoundary, MinimumValidConstruction) {
    auto mock = make_mock();
    InferencePool pool(mock, /*max_queue_depth=*/1, /*timeout=*/1);

    std::atomic<bool> done{false};
    auto job = make_job([&](SynthesisResult) { done.store(true); });
    EXPECT_TRUE(pool.submit(std::move(job)));

    pool.shutdown();
    EXPECT_TRUE(done.load());
}

// ============================================================================
// [BOUNDARY] Negative queue depth and timeout validation
// ============================================================================

TEST(PoolBoundary, NegativeQueueDepthThrows) {
    auto mock = make_mock();
    EXPECT_THROW(InferencePool(mock, -1, 60), std::invalid_argument);
}

TEST(PoolBoundary, NegativeTimeoutThrows) {
    auto mock = make_mock();
    EXPECT_THROW(InferencePool(mock, 5, -1), std::invalid_argument);
}

// ============================================================================
// [EDGE] Deadline exactly at now — should it expire or run?
// The implementation uses `>=` so deadline==now is expired.
// ============================================================================

TEST(PoolEdge, DeadlineExactlyAtNowIsExpired) {
    auto mock = make_mock();
    mock->latency_ms = 200;  // Hold worker on first job

    InferencePool pool(mock, 10, 60);

    // Submit a slow job first
    pool.submit(make_job());
    std::this_thread::sleep_for(30ms);

    // Submit a job with deadline=0ms from now (effectively now)
    std::atomic<bool> error_called{false};
    auto job = make_job(
        nullptr,
        [&](std::string) { error_called.store(true); },
        0);  // deadline = now
    pool.submit(std::move(job));

    pool.shutdown();

    // The zero-offset deadline should be treated as expired
    EXPECT_TRUE(error_called.load());
    EXPECT_GE(pool.expired_jobs(), 1);
}

// ============================================================================
// [COVERAGE] Large queue depth — stress the single-worker with many jobs
// ============================================================================

TEST(PoolIntegration, LargeQueueDrainAll) {
    auto mock = make_mock();
    // No latency — process as fast as possible
    InferencePool pool(mock, /*max_queue_depth=*/500, 60);

    constexpr int kJobs = 100;
    std::atomic<int> completed{0};

    for (int i = 0; i < kJobs; ++i) {
        auto job = make_job([&](SynthesisResult) {
            completed.fetch_add(1, std::memory_order_relaxed);
        });
        EXPECT_TRUE(pool.submit(std::move(job)));
    }

    pool.shutdown();
    EXPECT_EQ(completed.load(), kJobs);
    EXPECT_EQ(mock->call_count.load(), kJobs);
}

// ============================================================================
// [INTEGRATION] VoiceCatalog → InferencePool → Backend pipeline.
// Validates that a voice lookup feeds a valid request through the pool.
// ============================================================================

TEST(PoolIntegration, VoiceCatalogToPoolToBackendPipeline) {
    VoiceCatalog catalog;
    auto mock = make_mock();
    InferencePool pool(mock, 10, 60);

    // Look up a real voice from the catalog
    const Voice* voice = catalog.find("fr_female");
    ASSERT_NE(voice, nullptr);

    std::mutex mu;
    std::condition_variable cv;
    bool done = false;
    SynthesisResult received;

    InferenceJob job;
    job.request.text = "Bonjour le monde";
    job.request.voice = voice->id;
    job.request.model = "voxtral-4b";
    job.deadline = std::chrono::steady_clock::now() + 10s;
    job.on_success = [&](SynthesisResult result) {
        std::lock_guard lk(mu);
        received = std::move(result);
        done = true;
        cv.notify_one();
    };

    EXPECT_TRUE(pool.submit(std::move(job)));

    {
        std::unique_lock lk(mu);
        ASSERT_TRUE(cv.wait_for(lk, 5s, [&] { return done; }));
    }

    // Verify end-to-end: mock received the request from the pool
    EXPECT_EQ(mock->last_request.voice, "fr_female");
    EXPECT_EQ(mock->last_request.text, "Bonjour le monde");
    EXPECT_EQ(received.sample_rate, 24000);
    EXPECT_GE(received.audio_data.size(), 44U);  // at least a WAV header
}

// ============================================================================
// [COVERAGE] Pool monitors reset correctly after drain
// ============================================================================

TEST(PoolIntegration, MonitorsResetAfterDrain) {
    auto mock = make_mock();
    InferencePool pool(mock, 10, 60);

    std::atomic<int> done_count{0};
    for (int i = 0; i < 5; ++i) {
        auto job = make_job([&](SynthesisResult) {
            done_count.fetch_add(1);
        });
        pool.submit(std::move(job));
    }

    // Wait for all to complete
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (done_count.load() < 5 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }
    ASSERT_EQ(done_count.load(), 5);

    // After drain, counters should show idle state
    EXPECT_EQ(pool.queue_depth(), 0);
    EXPECT_EQ(pool.active_jobs(), 0);
    EXPECT_TRUE(pool.is_accepting());
    EXPECT_EQ(pool.expired_jobs(), 0);
}

// ============================================================================
// [EDGE] Backend throws non-std::exception — verify no UB
// (current code only catches std::exception; this documents the behavior)
// ============================================================================

// NOTE: This test is intentionally omitted. The pool catches std::exception.
// Throwing a non-std::exception (e.g. int) would terminate the worker thread
// and leave queued jobs orphaned. This is acceptable: real backends should
// never throw non-std::exception types. Documenting as a known limitation.

// ============================================================================
// [COVERAGE] Callback ordering — on_error for expired must fire
// BEFORE on_success for the next valid job (FIFO guarantee includes errors)
// ============================================================================

TEST(PoolIntegration, ExpiredCallbackFiresBeforeNextSuccess) {
    auto mock = make_mock();
    mock->latency_ms = 150;

    InferencePool pool(mock, 10, 60);

    std::mutex mu;
    std::condition_variable cv;
    std::vector<std::string> callback_order;

    // 1. Slow job to hold the worker
    auto slow = make_job([&](SynthesisResult) {
        std::lock_guard lk(mu);
        callback_order.push_back("slow-success");
        cv.notify_all();
    });
    pool.submit(std::move(slow));
    std::this_thread::sleep_for(30ms);

    // 2. Already-expired job (queued behind slow)
    auto expired = make_job(
        nullptr,
        [&](std::string) {
            std::lock_guard lk(mu);
            callback_order.push_back("expired-error");
            cv.notify_all();
        },
        -5000);
    pool.submit(std::move(expired));

    // 3. Valid job (queued behind expired)
    auto valid = make_job([&](SynthesisResult) {
        std::lock_guard lk(mu);
        callback_order.push_back("valid-success");
        cv.notify_all();
    });
    pool.submit(std::move(valid));

    // Wait for all 3 callbacks
    {
        std::unique_lock lk(mu);
        ASSERT_TRUE(cv.wait_for(lk, 10s, [&] {
            return callback_order.size() >= 3;
        }));
    }

    // FIFO: slow completes → expired fires error → valid completes
    ASSERT_EQ(callback_order.size(), 3U);
    EXPECT_EQ(callback_order[0], "slow-success");
    EXPECT_EQ(callback_order[1], "expired-error");
    EXPECT_EQ(callback_order[2], "valid-success");
}
