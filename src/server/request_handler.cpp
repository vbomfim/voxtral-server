/// @file request_handler.cpp
/// @brief Route handler implementations.

#include "request_handler.hpp"
#include "tts/metrics.hpp"
#include "tts/version.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <random>
#include <sstream>

using json = nlohmann::json;

namespace tts::server {

// ============================================================================
// UUID v4 generation (no external dep needed)
// ============================================================================

std::string generate_request_id() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFFU);

    auto r = [&]() { return dist(rng); };

    uint32_t a = r();
    uint32_t b = r();
    uint32_t c = r();
    uint32_t d = r();

    // Set version (4) and variant (10xx)
    b = (b & 0xFFFF0FFFU) | 0x00004000U;  // version 4
    c = (c & 0x3FFFFFFFU) | 0x80000000U;  // variant 10

    char buf[37];
    std::snprintf(buf, sizeof(buf),
        "%08x-%04x-%04x-%04x-%04x%08x",
        a,
        (b >> 16U) & 0xFFFFU,
        b & 0xFFFFU,
        (c >> 16U) & 0xFFFFU,
        c & 0xFFFFU,
        d);
    return buf;
}

// ============================================================================
// Security headers
// ============================================================================

void apply_security_headers(HttpResponse& resp, const std::string& request_id) {
    resp.headers["X-Content-Type-Options"] = "nosniff";
    resp.headers["Cache-Control"] = "no-store";
    resp.headers["X-Request-Id"] = request_id;
}

// ============================================================================
// Error response helpers
// ============================================================================

HttpResponse make_error_response(int status,
                                  const std::string& error_type,
                                  const std::string& error_code,
                                  const std::string& message,
                                  const std::string& param) {
    json err;
    err["error"]["message"] = message;
    err["error"]["type"] = error_type;
    err["error"]["param"] = param.empty() ? json(nullptr) : json(param);
    err["error"]["code"] = error_code;

    HttpResponse resp;
    resp.status = status;
    resp.headers["Content-Type"] = "application/json";
    resp.body = err.dump();
    return resp;
}

HttpResponse make_error_from_validation(const ValidationResult& v) {
    return make_error_response(
        v.http_status, v.error_type, v.error_code,
        v.error_message, v.error_param);
}

// ============================================================================
// GET /health
// ============================================================================

HttpResponse handle_health(const HandlerDeps& deps) {
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        now - deps.start_time).count();

    json body;
    body["status"] = "ok";
    body["version"] = std::string(tts::VERSION);
    body["uptime_seconds"] = uptime;

    HttpResponse resp;
    resp.status = 200;
    resp.headers["Content-Type"] = "application/json";
    resp.body = body.dump();
    return resp;
}

// ============================================================================
// GET /ready
// ============================================================================

HttpResponse handle_ready(const HandlerDeps& deps) {
    bool model_loaded = deps.backend && deps.backend->is_ready();
    bool pool_accepting = deps.pool != nullptr && deps.pool->is_accepting();
    bool ready = model_loaded && pool_accepting;

    int q_depth = deps.pool ? deps.pool->queue_depth() : 0;
    int q_capacity = deps.config ? deps.config->max_queue_depth : 0;

    json body;
    body["ready"] = ready;
    body["model_loaded"] = model_loaded;
    body["queue_depth"] = q_depth;
    body["queue_capacity"] = q_capacity;

    HttpResponse resp;
    resp.status = ready ? 200 : 503;
    resp.headers["Content-Type"] = "application/json";
    resp.body = body.dump();
    return resp;
}

// ============================================================================
// GET /v1/voices
// ============================================================================

HttpResponse handle_voices(const HandlerDeps& deps) {
    json voices_array = json::array();

    if (deps.voice_catalog) {
        for (const auto& v : deps.voice_catalog->all()) {
            json voice;
            voice["id"] = v.id;
            voice["name"] = v.name;
            voice["language"] = v.language;
            voices_array.push_back(voice);
        }
    }

    json body;
    body["voices"] = voices_array;

    HttpResponse resp;
    resp.status = 200;
    resp.headers["Content-Type"] = "application/json";
    resp.body = body.dump();
    return resp;
}

// ============================================================================
// GET /metrics
// ============================================================================

HttpResponse handle_metrics() {
    HttpResponse resp;
    resp.status = 200;
    resp.headers["Content-Type"] = "text/plain; version=0.0.4; charset=utf-8";
    resp.body = Metrics::instance().serialize();
    return resp;
}

// ============================================================================
// POST /v1/audio/speech
// ============================================================================

HttpResponse handle_speech(const HttpRequest& req, const HandlerDeps& deps) {
    auto synthesis_start = std::chrono::steady_clock::now();

    // 1. Validate Content-Type
    auto ct_result = validate_content_type(req.header("content-type"));
    if (!ct_result.valid) {
        Metrics::instance().requests_total(400).Increment();
        return make_error_from_validation(ct_result);
    }

    // 2. Validate body size
    auto sz_result = validate_body_size(req.body.size());
    if (!sz_result.valid) {
        Metrics::instance().requests_total(sz_result.http_status).Increment();
        return make_error_from_validation(sz_result);
    }

    // 3. Parse JSON body
    json parsed;
    try {
        parsed = json::parse(req.body);
    } catch (const json::parse_error&) {
        Metrics::instance().requests_total(400).Increment();
        return make_error_response(400, "invalid_request_error",
            "invalid_content_type", "Invalid JSON in request body.");
    }

    // 4. Extract and validate fields
    std::string model = parsed.value("model", "");
    auto model_v = validate_model(model);
    if (!model_v.valid) {
        Metrics::instance().requests_total(400).Increment();
        return make_error_from_validation(model_v);
    }

    std::string input = parsed.value("input", "");
    int max_chars = deps.config ? deps.config->max_input_chars : 4096;
    auto input_v = validate_input(input, max_chars);
    if (!input_v.valid) {
        Metrics::instance().requests_total(400).Increment();
        return make_error_from_validation(input_v);
    }

    std::string voice = parsed.value("voice", "");
    if (deps.voice_catalog) {
        auto voice_v = validate_voice(voice, *deps.voice_catalog);
        if (!voice_v.valid) {
            Metrics::instance().requests_total(400).Increment();
            return make_error_from_validation(voice_v);
        }
    }

    std::string response_format = parsed.value("response_format", "wav");
    auto fmt_v = validate_response_format(response_format);
    if (!fmt_v.valid) {
        Metrics::instance().requests_total(400).Increment();
        return make_error_from_validation(fmt_v);
    }

    float speed = parsed.value("speed", 1.0F);
    auto spd_v = validate_speed(speed);
    if (!spd_v.valid) {
        Metrics::instance().requests_total(400).Increment();
        return make_error_from_validation(spd_v);
    }

    // 5. Check pool readiness
    if (!deps.pool || !deps.pool->is_accepting()) {
        Metrics::instance().requests_total(503).Increment();
        return make_error_response(503, "server_error",
            "model_not_ready",
            "The model is not ready to accept requests. Try again later.");
    }

    // 6. Record metrics
    Metrics::instance().input_characters_total.Increment(
        static_cast<double>(input.size()));
    Metrics::instance().input_characters.Observe(
        static_cast<double>(input.size()));
    Metrics::instance().voice_requests_total(voice).Increment();

    // 7. Build synthesis request and submit to pool
    SynthesisRequest synth_req;
    synth_req.text = input;
    synth_req.voice = voice;
    synth_req.model = model;
    synth_req.response_format = response_format;
    synth_req.speed = speed;

    // Synchronous wait using promise/future pattern
    std::mutex result_mutex;
    std::condition_variable result_cv;
    bool result_ready = false;
    SynthesisResult synth_result;
    std::string synth_error;

    InferenceJob job;
    job.request = synth_req;
    job.on_success = [&](SynthesisResult r) {
        std::lock_guard<std::mutex> lock(result_mutex);
        synth_result = std::move(r);
        result_ready = true;
        result_cv.notify_one();
    };
    job.on_error = [&](std::string err) {
        std::lock_guard<std::mutex> lock(result_mutex);
        synth_error = std::move(err);
        result_ready = true;
        result_cv.notify_one();
    };

    Metrics::instance().active_jobs.Increment();
    Metrics::instance().queue_depth.Set(
        static_cast<double>(deps.pool->queue_depth()));

    if (!deps.pool->submit(std::move(job))) {
        Metrics::instance().active_jobs.Decrement();
        Metrics::instance().requests_total(503).Increment();
        return make_error_response(503, "server_error",
            "queue_full",
            "Server is at capacity. Please try again later.");
    }

    Metrics::instance().queue_depth.Set(
        static_cast<double>(deps.pool->queue_depth()));

    // 8. Wait for result
    {
        std::unique_lock<std::mutex> lock(result_mutex);
        result_cv.wait(lock, [&] { return result_ready; });
    }

    Metrics::instance().active_jobs.Decrement();
    Metrics::instance().queue_depth.Set(
        static_cast<double>(deps.pool->queue_depth()));

    // 9. Handle error
    if (!synth_error.empty()) {
        Metrics::instance().errors_total.Increment();
        Metrics::instance().requests_total(500).Increment();
        return make_error_response(500, "server_error",
            "inference_failed",
            "An error occurred during speech synthesis.");
    }

    // 10. Record timing and size metrics
    auto synthesis_end = std::chrono::steady_clock::now();
    double synthesis_secs = std::chrono::duration<double>(
        synthesis_end - synthesis_start).count();

    Metrics::instance().synthesis_duration_seconds.Observe(synthesis_secs);
    Metrics::instance().inference_duration_seconds.Observe(
        synth_result.inference_time_seconds);
    Metrics::instance().audio_duration_seconds.Observe(
        synth_result.duration_seconds);
    Metrics::instance().audio_bytes_generated_total.Increment(
        static_cast<double>(synth_result.audio_data.size()));
    Metrics::instance().requests_total(200).Increment();

    // 11. Return audio response
    HttpResponse resp;
    resp.status = 200;
    resp.headers["Content-Type"] = "audio/wav";
    resp.headers["Content-Length"] =
        std::to_string(synth_result.audio_data.size());
    resp.binary_body = std::move(synth_result.audio_data);
    return resp;
}

}  // namespace tts::server
