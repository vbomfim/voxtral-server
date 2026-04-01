/// @file test_metrics.cpp
/// @brief Developer unit tests for Prometheus metrics singleton.
///
/// Tests counter increments, histogram observations, gauge operations,
/// and Prometheus text format serialization.

#include <gtest/gtest.h>

#include "tts/metrics.hpp"

#include <string>

using namespace tts;

// ============================================================================
// Singleton access
// ============================================================================

TEST(Metrics, SingletonReturnsSameInstance) {
    auto& a = Metrics::instance();
    auto& b = Metrics::instance();
    EXPECT_EQ(&a, &b);
}

TEST(Metrics, RegistryIsNonNull) {
    EXPECT_NE(Metrics::instance().registry(), nullptr);
}

// ============================================================================
// Counter increments
// ============================================================================

TEST(Metrics, RequestsTotalIncrementsByStatus) {
    auto& m = Metrics::instance();
    double before = m.requests_total(200).Value();
    m.requests_total(200).Increment();
    EXPECT_DOUBLE_EQ(m.requests_total(200).Value(), before + 1.0);
}

TEST(Metrics, RequestsTotalDifferentStatusesAreIndependent) {
    auto& m = Metrics::instance();
    double before_200 = m.requests_total(200).Value();
    double before_400 = m.requests_total(400).Value();

    m.requests_total(200).Increment();

    EXPECT_DOUBLE_EQ(m.requests_total(200).Value(), before_200 + 1.0);
    EXPECT_DOUBLE_EQ(m.requests_total(400).Value(), before_400);
}

TEST(Metrics, VoiceRequestsTotalIncrementsPerVoice) {
    auto& m = Metrics::instance();
    double before = m.voice_requests_total("neutral_female").Value();
    m.voice_requests_total("neutral_female").Increment();
    EXPECT_DOUBLE_EQ(m.voice_requests_total("neutral_female").Value(),
                     before + 1.0);
}

TEST(Metrics, AuthRejectedCounterIncrements) {
    auto& m = Metrics::instance();
    double before = m.requests_rejected_auth.Value();
    m.requests_rejected_auth.Increment();
    EXPECT_DOUBLE_EQ(m.requests_rejected_auth.Value(), before + 1.0);
}

TEST(Metrics, RateLimitRejectedCounterIncrements) {
    auto& m = Metrics::instance();
    double before = m.requests_rejected_rate_limit.Value();
    m.requests_rejected_rate_limit.Increment();
    EXPECT_DOUBLE_EQ(m.requests_rejected_rate_limit.Value(), before + 1.0);
}

TEST(Metrics, InputCharactersTotalIncrementsWithValue) {
    auto& m = Metrics::instance();
    double before = m.input_characters_total.Value();
    m.input_characters_total.Increment(42.0);
    EXPECT_DOUBLE_EQ(m.input_characters_total.Value(), before + 42.0);
}

TEST(Metrics, AudioBytesGeneratedTotalIncrements) {
    auto& m = Metrics::instance();
    double before = m.audio_bytes_generated_total.Value();
    m.audio_bytes_generated_total.Increment(1024.0);
    EXPECT_DOUBLE_EQ(m.audio_bytes_generated_total.Value(), before + 1024.0);
}

TEST(Metrics, ErrorsTotalCounterIncrements) {
    auto& m = Metrics::instance();
    double before = m.errors_total.Value();
    m.errors_total.Increment();
    EXPECT_DOUBLE_EQ(m.errors_total.Value(), before + 1.0);
}

// ============================================================================
// Gauge operations
// ============================================================================

TEST(Metrics, ActiveJobsGaugeIncrementDecrement) {
    auto& m = Metrics::instance();
    m.active_jobs.Set(0.0);
    m.active_jobs.Increment();
    EXPECT_DOUBLE_EQ(m.active_jobs.Value(), 1.0);
    m.active_jobs.Decrement();
    EXPECT_DOUBLE_EQ(m.active_jobs.Value(), 0.0);
}

TEST(Metrics, QueueDepthGaugeSet) {
    auto& m = Metrics::instance();
    m.queue_depth.Set(5.0);
    EXPECT_DOUBLE_EQ(m.queue_depth.Value(), 5.0);
    m.queue_depth.Set(0.0);
    EXPECT_DOUBLE_EQ(m.queue_depth.Value(), 0.0);
}

// ============================================================================
// Histogram observations
// ============================================================================

TEST(Metrics, SynthesisDurationObservation) {
    auto& m = Metrics::instance();
    // Should not throw — observation is recorded
    m.synthesis_duration_seconds.Observe(2.5);
    m.synthesis_duration_seconds.Observe(0.1);
}

TEST(Metrics, InferenceDurationObservation) {
    auto& m = Metrics::instance();
    m.inference_duration_seconds.Observe(1.0);
}

TEST(Metrics, AudioDurationObservation) {
    auto& m = Metrics::instance();
    m.audio_duration_seconds.Observe(3.0);
}

TEST(Metrics, InputCharactersHistogramObservation) {
    auto& m = Metrics::instance();
    m.input_characters.Observe(100.0);
    m.input_characters.Observe(2000.0);
}

// ============================================================================
// Prometheus text serialization
// ============================================================================

TEST(Metrics, SerializeProducesNonEmptyText) {
    auto text = Metrics::instance().serialize();
    EXPECT_FALSE(text.empty());
}

TEST(Metrics, SerializeContainsMetricNames) {
    // Trigger labeled metrics so they appear in output
    Metrics::instance().requests_total(200).Increment();
    auto text = Metrics::instance().serialize();
    EXPECT_NE(text.find("opentts_requests_total"), std::string::npos);
    EXPECT_NE(text.find("opentts_active_jobs"), std::string::npos);
    EXPECT_NE(text.find("opentts_queue_depth"), std::string::npos);
    EXPECT_NE(text.find("opentts_synthesis_duration_seconds"), std::string::npos);
    EXPECT_NE(text.find("opentts_inference_duration_seconds"), std::string::npos);
    EXPECT_NE(text.find("opentts_input_characters_total"), std::string::npos);
    EXPECT_NE(text.find("opentts_errors_total"), std::string::npos);
}

TEST(Metrics, SerializeContainsHelpAndType) {
    auto text = Metrics::instance().serialize();
    EXPECT_NE(text.find("# HELP"), std::string::npos);
    EXPECT_NE(text.find("# TYPE"), std::string::npos);
}

TEST(Metrics, SerializeContainsHistogramBuckets) {
    auto text = Metrics::instance().serialize();
    // Histogram buckets should have _bucket suffix
    EXPECT_NE(text.find("_bucket"), std::string::npos);
    // And +Inf bucket
    EXPECT_NE(text.find("+Inf"), std::string::npos);
}

TEST(Metrics, SerializeContainsLabeledMetrics) {
    // Ensure we've incremented at least once
    Metrics::instance().requests_total(200).Increment();
    auto text = Metrics::instance().serialize();
    EXPECT_NE(text.find("status=\"200\""), std::string::npos);
}
