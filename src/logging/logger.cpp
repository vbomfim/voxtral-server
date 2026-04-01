#include "logging/logger.hpp"
#include "config/config.hpp"

#include <chrono>
#include <ctime>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/formatter.h>
#include <spdlog/pattern_formatter.h>

namespace tts::logging {

// Thread-local request ID for log correlation
static thread_local std::string t_request_id;

void set_request_id(const std::string& request_id) {
    t_request_id = request_id;
}

const std::string& get_request_id() {
    return t_request_id;
}

/// Portable gmtime wrapper (gmtime_r on POSIX, gmtime_s on Windows).
static std::tm safe_gmtime(const std::time_t* time) {
    std::tm result{};
#ifdef _WIN32
    gmtime_s(&result, time);
#else
    gmtime_r(time, &result);
#endif
    return result;
}

/// Custom spdlog formatter that outputs one JSON object per log line.
/// Compatible with Datadog, Loki, CloudWatch, and ELK log parsers.
///
/// Output format:
///   {"timestamp":"2026-04-01T01:30:00.000Z","level":"info","component":"server","message":"..."}
class JsonFormatter final : public spdlog::formatter {
public:
    void format(const spdlog::details::log_msg& msg,
                spdlog::memory_buf_t& dest) override {
        nlohmann::json j;

        // ISO 8601 timestamp with millisecond precision
        j["timestamp"] = format_timestamp(msg.time);

        // Log level as lowercase string
        j["level"] = std::string(spdlog::level::to_string_view(msg.level).data());

        // Logger name as component (default to "server" for the global logger)
        if (msg.logger_name.size() > 0) {
            j["component"] = std::string(msg.logger_name.data(), msg.logger_name.size());
        } else {
            j["component"] = "server";
        }

        // Message body
        j["message"] = std::string(msg.payload.data(), msg.payload.size());

        // Request ID for correlation (if set)
        if (!t_request_id.empty()) {
            j["request_id"] = t_request_id;
        }

        auto str = j.dump() + "\n";
        dest.append(str.data(), str.data() + str.size());
    }

    [[nodiscard]] std::unique_ptr<spdlog::formatter> clone() const override {
        return std::make_unique<JsonFormatter>();
    }

private:
    static std::string format_timestamp(spdlog::log_clock::time_point tp) {
        auto epoch = tp.time_since_epoch();
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(epoch);
        auto secs = std::chrono::duration_cast<std::chrono::seconds>(millis);
        auto ms = millis - std::chrono::duration_cast<std::chrono::milliseconds>(secs);

        std::time_t tt = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::time_point(secs));
        std::tm gmt = safe_gmtime(&tt);

        // "2026-04-01T01:30:00.000Z"
        char buf[64];
        std::snprintf(buf, sizeof(buf),
                      "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                      gmt.tm_year + 1900, gmt.tm_mon + 1, gmt.tm_mday,
                      gmt.tm_hour, gmt.tm_min, gmt.tm_sec,
                      static_cast<int>(ms.count()));
        return buf;
    }
};

spdlog::level::level_enum parse_log_level(const std::string& level) {
    if (level == "trace")    return spdlog::level::trace;
    if (level == "debug")    return spdlog::level::debug;
    if (level == "info")     return spdlog::level::info;
    if (level == "warn")     return spdlog::level::warn;
    if (level == "error")    return spdlog::level::err;
    if (level == "critical") return spdlog::level::critical;
    return spdlog::level::info;
}

void initialize(const tts::config::ServerConfig& cfg) {
    spdlog::set_level(parse_log_level(cfg.log_level));
    if (cfg.log_format == "json") {
        spdlog::set_formatter(std::make_unique<JsonFormatter>());
    } else {
        // Explicitly reset to default text pattern for re-init idempotency
        spdlog::set_pattern("%+");
    }
}

std::unique_ptr<spdlog::formatter> make_json_formatter() {
    return std::make_unique<JsonFormatter>();
}

} // namespace tts::logging
