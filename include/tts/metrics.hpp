#pragma once

/// @file metrics.hpp
/// @brief Prometheus metrics singleton for the TTS server.
///
/// All metrics use the "opentts_" prefix. Thread-safe via prometheus-cpp
/// internal atomics. Mirrors the openasr Metrics singleton pattern.

#include <prometheus/counter.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>
#include <prometheus/text_serializer.h>

#include <memory>
#include <string>

namespace tts {

/// Prometheus metrics singleton — pre-initialized, thread-safe.
///
/// Usage:
///   Metrics::instance().requests_total(200).Increment();
///   Metrics::instance().active_jobs.Increment();
///   Metrics::instance().synthesis_duration_seconds.Observe(elapsed);
///   auto text = Metrics::instance().serialize();
class Metrics {
public:
    /// Meyer's singleton — thread-safe initialization (C++11 guarantee).
    static Metrics& instance();

    /// Access the underlying prometheus registry.
    std::shared_ptr<prometheus::Registry> registry() { return registry_; }

    /// Serialize all metrics to Prometheus text exposition format.
    [[nodiscard]] std::string serialize() const;

    // --- Counter accessors (by label) ---

    /// Get/create the requests_total counter for a given HTTP status code.
    prometheus::Counter& requests_total(int status_code);

    /// Get/create the voice_requests_total counter for a given voice ID.
    prometheus::Counter& voice_requests_total(const std::string& voice);

    // Non-copyable
    Metrics(const Metrics&) = delete;
    Metrics& operator=(const Metrics&) = delete;

private:
    Metrics();

    // *** Declaration order matters — members initialize in this order ***

    // 1. Registry (must be first — everything else registers with it)
    std::shared_ptr<prometheus::Registry> registry_;

    // 2. Labeled counter families
    prometheus::Family<prometheus::Counter>& requests_total_family_;
    prometheus::Family<prometheus::Counter>& voice_requests_total_family_;

public:
    // 3. Fixed counters (no labels)
    prometheus::Counter& requests_rejected_auth;
    prometheus::Counter& requests_rejected_rate_limit;
    prometheus::Counter& input_characters_total;
    prometheus::Counter& audio_bytes_generated_total;
    prometheus::Counter& errors_total;

    // 4. Gauges
    prometheus::Gauge& active_jobs;
    prometheus::Gauge& queue_depth;

    // 5. Histograms
    prometheus::Histogram& synthesis_duration_seconds;
    prometheus::Histogram& inference_duration_seconds;
    prometheus::Histogram& audio_duration_seconds;
    prometheus::Histogram& input_characters;
};

}  // namespace tts
