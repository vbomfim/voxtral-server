# Architecture

voxtral-server follows a **hexagonal architecture** (Ports & Adapters) where
business logic is isolated from transport and infrastructure concerns.

## Component Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                        HTTP Transport                           │
│  ┌──────────┐  ┌──────────────┐  ┌──────────┐  ┌────────────┐ │
│  │  Health   │  │   Speech     │  │  Voices  │  │  Metrics   │ │
│  │  /health  │  │ /v1/audio/   │  │ /v1/     │  │  /metrics  │ │
│  │  /ready   │  │   speech     │  │  voices  │  │            │ │
│  └────┬─────┘  └──────┬───────┘  └────┬─────┘  └──────┬─────┘ │
│       │               │               │               │        │
│       └───────────────┼───────────────┼───────────────┘        │
│                       ▼                                         │
│            ┌──────────────────┐                                 │
│            │  RequestHandler  │◄── Auth + RateLimit middleware  │
│            └────────┬─────────┘                                 │
└─────────────────────┼───────────────────────────────────────────┘
                      │
┌─────────────────────┼───────────────────────────────────────────┐
│                     ▼        Core Domain                        │
│           ┌──────────────────┐                                  │
│           │    TtsServer     │ orchestrates lifecycle            │
│           └────────┬─────────┘                                  │
│                    │                                            │
│       ┌────────────┼────────────────┐                           │
│       ▼            ▼                ▼                            │
│  ┌──────────┐ ┌──────────────┐ ┌──────────────┐                │
│  │  Voice   │ │  Inference   │ │   Validation │                │
│  │  Catalog │ │    Pool      │ │              │                │
│  │ (20      │ │ (bounded     │ │ (OpenAI-     │                │
│  │  voices) │ │  queue +     │ │  compatible) │                │
│  └──────────┘ │  workers)    │ └──────────────┘                │
│               └──────┬───────┘                                  │
│                      │                                          │
└──────────────────────┼──────────────────────────────────────────┘
                       │
┌──────────────────────┼──────────────────────────────────────────┐
│                      ▼        Ports (Interfaces)                │
│              ┌───────────────┐                                  │
│              │  ITtsBackend  │ ◄── abstract interface           │
│              └───────┬───────┘                                  │
│                      │                                          │
│          ┌───────────┼────────────┐                              │
│          ▼                       ▼                               │
│  ┌───────────────┐     ┌──────────────┐                         │
│  │ VoxtralBackend│     │ MockBackend  │                         │
│  │ (voxtral-tts.c)     │ (testing)    │                         │
│  └───────────────┘     └──────────────┘                         │
│                                                                 │
│                     Adapters (Implementations)                  │
└─────────────────────────────────────────────────────────────────┘
```

## Data Flow

### TTS Request Lifecycle

```
Client                          Server
  │                               │
  │  POST /v1/audio/speech        │
  │  Authorization: Bearer <key>  │
  │  {"model":"voxtral-4b",       │
  │   "input":"Hello",            │
  │   "voice":"neutral_female"}   │
  │──────────────────────────────►│
  │                               │
  │                    ┌──────────┤
  │                    │ 1. Auth  │ constant-time token comparison
  │                    │ 2. Rate  │ per-IP RPM check
  │                    │ 3. Valid │ model, input, voice, format, speed
  │                    │ 4. Queue │ submit to InferencePool
  │                    │ 5. Synth │ ITtsBackend::synthesize()
  │                    │ 6. WAV   │ encode audio response
  │                    └──────────┤
  │                               │
  │  200 OK                       │
  │  Content-Type: audio/wav      │
  │  <binary WAV data>            │
  │◄──────────────────────────────│
```

## Threading Model

```
┌─────────────────────────────────────────────┐
│              Main Thread                     │
│  • Config loading                           │
│  • Logging initialization                   │
│  • Server lifecycle                         │
└─────────────────────────────────────────────┘

┌─────────────────────────────────────────────┐
│           HTTP I/O Thread(s)                 │
│  • Accept connections                       │
│  • Parse requests                           │
│  • Auth + rate limit checks                 │
│  • Validation                               │
│  • Submit jobs to InferencePool             │
│  • Send responses                           │
└─────────────────────────────────────────────┘

┌─────────────────────────────────────────────┐
│         Inference Worker Thread(s)           │
│  • Dequeue jobs from bounded queue          │
│  • Run ITtsBackend::synthesize()            │
│  • Signal completion (promise/future)       │
│  • Deadline-based job expiry                │
└─────────────────────────────────────────────┘
```

### Concurrency Design

- **Bounded queue** — `InferencePool` uses a fixed-size queue
  (`max_queue_depth`) to prevent unbounded memory growth. Requests
  submitted when the queue is full receive HTTP 503.
- **Single worker** — Default is 1 inference worker. TTS models are
  memory-intensive; a single worker avoids GPU contention.
- **Deadline expiry** — Each `InferenceJob` has a deadline
  (`request_timeout_seconds`). Expired jobs are discarded before
  execution.
- **Thread-safe rate limiters** — `AuthRateLimiter` and
  `RequestRateLimiter` use per-instance mutexes. They are per-IP
  sliding-window counters with configurable memory caps.

## Module Layout

```
voxtral-server/
├── include/tts/         # Public headers (Ports)
│   ├── backend.hpp      # ITtsBackend interface
│   ├── server.hpp       # TtsServer facade
│   ├── auth.hpp         # Auth + rate limiting
│   ├── validation.hpp   # Request validation
│   ├── voices.hpp       # Voice catalog
│   ├── inference_pool.hpp  # Bounded inference queue
│   ├── metrics.hpp      # Prometheus metrics (singleton)
│   └── version.hpp.in   # Version template
├── src/
│   ├── main.cpp         # Entry point
│   ├── config/          # TOML + env var config loading
│   ├── logging/         # spdlog initialization
│   ├── server/          # HTTP handlers, auth, validation, metrics
│   └── tts/             # Backend implementations, voices, pool
├── tests/               # GTest unit + integration + E2E tests
├── config/              # Default server.toml
├── docker/              # Dockerfiles, compose, nginx
└── k8s/                 # Kubernetes manifests
```

## Security Model

See [SECURITY.md](SECURITY.md) for the full security model. Key points:

1. **Authentication** — Optional bearer-token auth with constant-time
   comparison. No plaintext passwords; key set via environment variable
   only.
2. **Rate limiting** — Per-IP sliding window for both auth failures and
   request throughput. Memory-capped to prevent state exhaustion.
3. **Input validation** — Server-side validation of all request fields
   with strict type/range/length checks.
4. **Container hardening** — Distroless base image, nonroot user (UID
   65532), read-only root filesystem, all capabilities dropped.
5. **Network isolation** — Kubernetes NetworkPolicy defaults to deny-all;
   explicit allow rules for ingress-nginx and monitoring only.
6. **Compiler hardening** — `-Werror`, stack protector, FORTIFY_SOURCE,
   PIE, RELRO+NOW on Linux.

## Build System

CMake 3.20+ with FetchContent for all dependencies (pinned to exact
commit SHAs). Three build targets:

| Target | Type | Purpose |
|--------|------|---------|
| `tts_core` | OBJECT library | Shared code (compiled once) |
| `tts_server` | Executable | Production binary |
| `tts_tests` | Test executable | GTest unit tests |

Dependencies are fetched at configure time:
- nlohmann/json v3.11.3 (JSON parsing)
- spdlog v1.15.1 (structured logging)
- toml++ v3.4.0 (config parsing)
- prometheus-cpp v1.3.0 (metrics)
- Google Test v1.15.2 (testing)
- Google Benchmark v1.9.1 (performance)
