# Changelog

All notable changes to voxtral-server will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Docker images: CPU (distroless) and CUDA (nvidia/cuda) multi-stage builds
- Docker Compose: quick start and TLS variant with nginx reverse proxy
- Kubernetes manifests: namespace, deployments (CPU + GPU), services,
  NetworkPolicy (default-deny + allow rules), PDB, PVC, ResourceQuota,
  ServiceAccount, ConfigMap
- Kubernetes local dev: all-in-one manifest with initContainer model
  downloader, NodePort service, Ingress with TTS-appropriate timeouts
- CI/CD: GitHub Actions for build/test, Docker publish (Trivy + SBOM),
  CodeQL security analysis, benchmark tracking
- Dependabot for GitHub Actions and Docker base images
- GitHub issue templates (bug report, feature request) and PR template
- Full documentation: README, ARCHITECTURE.md, CONTRIBUTING.md,
  SECURITY.md, CODE_OF_CONDUCT.md, CHANGELOG.md

## [0.1.0] - 2026-03-15

### Added
- Project scaffold with CMake 3.20+ build system (C++20)
- TOML configuration with environment variable overrides (`TTS_*` prefix)
- Structured logging via spdlog (text + JSON formats)
- Abstract TTS backend interface (`ITtsBackend`) — hexagonal architecture
- VoxtralBackend stub (ready for voxtral-tts.c submodule)
- MockBackend for testing
- Voice catalog with 20 multilingual preset voices (10 languages)
- Bounded inference pool with deadline-based job expiry
- OpenAI-compatible request validation (`POST /v1/audio/speech`)
- Bearer token authentication with constant-time comparison
- Per-IP auth failure rate limiting (brute-force protection)
- Per-IP request rate limiting (RPM)
- X-Forwarded-For client IP extraction with configurable proxy trust
- Prometheus metrics (counters, gauges, histograms)
- Request handler with full middleware chain
- TtsServer facade wiring all components
- 485 unit tests (GTest) — unit, integration, and E2E
- Security-hardened compiler flags (FORTIFY_SOURCE, stack protector,
  PIE, RELRO, format security)
- WAV audio contract tests

[Unreleased]: https://github.com/vbomfim/voxtral-server/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/vbomfim/voxtral-server/releases/tag/v0.1.0
