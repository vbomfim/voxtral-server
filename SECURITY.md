# Security Policy

## Reporting Vulnerabilities

**Do not open public issues for security vulnerabilities.**

Please report security issues by emailing **security@voxtral-server.dev**
(or by opening a private GitHub security advisory if enabled). Include:

- Description of the vulnerability
- Steps to reproduce
- Impact assessment
- Suggested fix (if any)

We will acknowledge receipt within 48 hours and aim to provide a fix
within 7 days for critical issues.

## Security Model

### Authentication

- **Bearer token** — API key authentication via `Authorization: Bearer <key>`
  header. Keys are set via `TTS_API_KEY` environment variable, never in
  config files or source code.
- **Constant-time comparison** — Token comparison uses `volatile`-based
  constant-time equality to prevent timing side-channel attacks (same
  pattern as mbedTLS and libsodium).
- **Header-only** — API keys are never accepted in query strings or
  request bodies to prevent logging and referrer leakage.
- **Optional** — Auth can be disabled for development (`TTS_REQUIRE_AUTH=false`).
  Always enable in production.

### Input Validation

All request fields are validated server-side before processing:

| Field | Validation |
|-------|-----------|
| `model` | Whitelist (`voxtral-4b`) |
| `input` | Non-empty, max 4096 chars (configurable) |
| `voice` | Must exist in VoiceCatalog (20 preset voices) |
| `response_format` | Whitelist (`wav`) |
| `speed` | Range 0.25–4.0 |
| Content-Type | Must be `application/json` |
| Body size | Max 1MB |

### DoS Protections

- **Auth rate limiting** — Per-IP sliding window for authentication
  failures. Default: 10 failures per 60-second window. IP is blocked
  until the window expires.
- **Request rate limiting** — Per-IP requests-per-minute cap. Default:
  10 RPM. Returns HTTP 429 when exceeded.
- **Bounded queue** — Inference queue has a fixed max depth (default 10).
  Returns HTTP 503 when full.
- **Request timeout** — Each inference job has a deadline (default 300s).
  Expired jobs are discarded.
- **Input length limit** — Max 4096 characters per request.
- **Body size limit** — Max 1MB request body.
- **Memory-capped rate limiters** — Rate limiter state is capped to
  prevent memory exhaustion from many unique IPs.

### Container Security

- **Distroless base** — CPU images use `gcr.io/distroless/cc-debian12:nonroot`,
  which contains only the C runtime and CA certificates. No shell, no
  package manager, no attack surface.
- **Nonroot user** — All containers run as UID 65532 (nonroot). The
  process has no elevated privileges.
- **Read-only filesystem** — Root filesystem is read-only. Writable
  mounts (GPU only: `/tmp`, `~/.nv`) use ephemeral `emptyDir` volumes.
- **No capabilities** — All Linux capabilities are dropped (`ALL`).
  `allowPrivilegeEscalation: false` is enforced.
- **Security options** — Docker Compose files include
  `no-new-privileges:true` and `cap_drop: ALL`.

### Network Security

- **TLS at ingress** — The server does not implement TLS natively (by
  design). TLS termination is handled by:
  - nginx reverse proxy (Docker Compose with `docker-compose.tls.yml`)
  - Kubernetes Ingress controller (nginx-ingress with TLS secret)
- **NetworkPolicy** — Kubernetes manifests include a default-deny policy.
  Explicit allow rules permit traffic only from:
  - `ingress-nginx` namespace (client requests)
  - `monitoring` namespace (Prometheus scraping)
  - `kube-system` namespace (DNS only, UDP/TCP port 53)
- **No egress** — The server makes no outbound connections. Egress is
  limited to DNS resolution only.

### Secret Management

- API keys: set via `TTS_API_KEY` environment variable only
- Kubernetes: use `Secret` resources (`kubectl create secret generic`)
- Docker Compose: use `${TTS_API_KEY}` shell variable expansion
- Config files: `api_key` field is commented out with a warning
- Logging: API key is redacted as `[REDACTED]` in all log output
- **Never** commit secrets to version control

### Compiler Hardening

The build system applies security-hardened compiler flags:

| Flag | Purpose |
|------|---------|
| `-Wall -Wextra -Wpedantic -Werror` | Treat all warnings as errors |
| `-Wformat=2 -Wformat-security` | Format string validation |
| `-Wconversion -Wsign-conversion` | Implicit conversion warnings |
| `-Wnull-dereference` | Null pointer checks |
| `-fstack-protector-strong` | Stack buffer overflow detection |
| `-fPIE` | Position-independent executable |
| `-D_FORTIFY_SOURCE=2` | Runtime buffer overflow checks (Release) |
| `-pie -Wl,-z,relro -Wl,-z,now` | Full RELRO (Linux only) |

### CI/CD Security

- **CodeQL** — Static analysis runs on every push and PR, plus weekly
  scheduled scans. Fails on severity `error`.
- **Dependabot** — Automated dependency updates for GitHub Actions and
  Docker base images.
- **Trivy** — Container image vulnerability scanning on every release.
  Fails on CRITICAL and HIGH severity.
- **SBOM** — Software Bill of Materials generated for every release
  image (SPDX format).
- **No secrets in CI** — API keys use GitHub Secrets; images are scanned
  for leaked credentials.

### Deployment Assumptions

This security model assumes:

1. **TLS is terminated at the ingress layer** — either by nginx reverse
   proxy or Kubernetes Ingress controller. The server itself communicates
   over plaintext HTTP within the trusted network.
2. **Network segmentation** — The server runs in an isolated namespace
   with NetworkPolicy. It has no internet access.
3. **Secret rotation** — API keys should be rotated periodically. The
   server reads keys from environment variables and can be restarted
   to pick up new values.
4. **Model integrity** — Model files are verified by checksum before
   use. In Kubernetes, model storage uses `ReadOnlyMany` PVC.

## Testing

- **485 unit tests** covering all modules
- **Constant-time comparison tests** — verify timing-safe equality
- **Rate limiter tests** — verify per-IP blocking and window expiry
- **Validation tests** — adversarial inputs, boundary values, injection attempts
- **E2E binary tests** — verify `--help`, `--version`, config error handling

## Supported Versions

| Version | Supported |
|---------|-----------|
| 0.1.x   | ✅ Current |
