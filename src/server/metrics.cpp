/// @file metrics.cpp
/// @brief Prometheus metrics singleton implementation.

#include "tts/metrics.hpp"

#include <string>

namespace tts {

Metrics& Metrics::instance() {
    static Metrics m;
    return m;
}

std::string Metrics::serialize() const {
    prometheus::TextSerializer serializer;
    auto collected = registry_->Collect();
    return serializer.Serialize(collected);
}

prometheus::Counter& Metrics::requests_total(int status_code) {
    return requests_total_family_.Add({{"status", std::to_string(status_code)}});
}

prometheus::Counter& Metrics::voice_requests_total(const std::string& voice) {
    return voice_requests_total_family_.Add({{"voice", voice}});
}

Metrics::Metrics()
    : registry_(std::make_shared<prometheus::Registry>())

    // Labeled counter families
    , requests_total_family_(prometheus::BuildCounter()
        .Name("opentts_requests_total")
        .Help("Total requests by HTTP status code")
        .Register(*registry_))
    , voice_requests_total_family_(prometheus::BuildCounter()
        .Name("opentts_voice_requests_total")
        .Help("Per-voice request count")
        .Register(*registry_))

    // Fixed counters
    , requests_rejected_auth(prometheus::BuildCounter()
        .Name("opentts_requests_rejected_auth_total")
        .Help("Auth failures")
        .Register(*registry_).Add({}))
    , requests_rejected_rate_limit(prometheus::BuildCounter()
        .Name("opentts_requests_rejected_rate_limit_total")
        .Help("Rate limit hits")
        .Register(*registry_).Add({}))
    , input_characters_total(prometheus::BuildCounter()
        .Name("opentts_input_characters_total")
        .Help("Total input characters processed")
        .Register(*registry_).Add({}))
    , audio_bytes_generated_total(prometheus::BuildCounter()
        .Name("opentts_audio_bytes_generated_total")
        .Help("Total audio bytes generated")
        .Register(*registry_).Add({}))
    , errors_total(prometheus::BuildCounter()
        .Name("opentts_errors_total")
        .Help("Inference errors")
        .Register(*registry_).Add({}))

    // Gauges
    , active_jobs(prometheus::BuildGauge()
        .Name("opentts_active_jobs")
        .Help("Currently processing jobs")
        .Register(*registry_).Add({}))
    , queue_depth(prometheus::BuildGauge()
        .Name("opentts_queue_depth")
        .Help("Jobs waiting in queue")
        .Register(*registry_).Add({}))

    // Histograms
    , synthesis_duration_seconds(prometheus::BuildHistogram()
        .Name("opentts_synthesis_duration_seconds")
        .Help("End-to-end synthesis duration in seconds")
        .Register(*registry_).Add({}, prometheus::Histogram::BucketBoundaries{
            1.0, 2.5, 5.0, 10.0, 30.0, 60.0, 120.0, 300.0, 600.0}))
    , inference_duration_seconds(prometheus::BuildHistogram()
        .Name("opentts_inference_duration_seconds")
        .Help("Inference-only duration in seconds")
        .Register(*registry_).Add({}, prometheus::Histogram::BucketBoundaries{
            1.0, 2.5, 5.0, 10.0, 30.0, 60.0, 120.0, 300.0, 600.0}))
    , audio_duration_seconds(prometheus::BuildHistogram()
        .Name("opentts_audio_duration_seconds")
        .Help("Generated audio duration in seconds")
        .Register(*registry_).Add({}, prometheus::Histogram::BucketBoundaries{
            1.0, 2.5, 5.0, 10.0, 30.0, 60.0, 120.0, 300.0, 600.0}))
    , input_characters(prometheus::BuildHistogram()
        .Name("opentts_input_characters")
        .Help("Input text length distribution")
        .Register(*registry_).Add({}, prometheus::Histogram::BucketBoundaries{
            50.0, 100.0, 250.0, 500.0, 1000.0, 2000.0, 4096.0}))
{}

}  // namespace tts
