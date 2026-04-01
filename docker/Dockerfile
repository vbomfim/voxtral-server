# syntax=docker/dockerfile:1
# ---------------------------------------------------------------------------
# voxtral-server — CPU image (distroless)
#
# Multi-stage build:
#   1. Builder: Ubuntu 24.04 with build tools → compiles tts_server
#   2. Runtime: distroless/cc-debian12:nonroot → minimal attack surface
#
# Build:
#   docker build -f docker/Dockerfile -t voxtral-server:cpu .
#
# Run:
#   docker run -p 9090:9090 \
#     -v /path/to/models:/models:ro \
#     -e TTS_MODEL_PATH=/models/voxtral-4b \
#     voxtral-server:cpu
# ---------------------------------------------------------------------------

# ---- Build stage ----
FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    pkg-config \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

# Initialize submodules (if present)
RUN git submodule update --init --recursive 2>/dev/null || true

RUN mkdir -p build && cd build && \
    cmake .. \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_TESTS=OFF \
      -DBUILD_BENCHMARKS=OFF && \
    cmake --build . -j"$(nproc)"

# Collect shared library dependencies for distroless
RUN mkdir -p /runtime-libs && \
    ldd /build/build/tts_server | \
    grep "=> /" | awk '{print $3}' | \
    xargs -I{} cp --parents {} /runtime-libs/ && \
    cp --parents /lib/*/ld-linux-* /runtime-libs/ 2>/dev/null || true

# ---- Runtime stage (distroless) ----
FROM gcr.io/distroless/cc-debian12:nonroot

# Copy binary
COPY --from=builder /build/build/tts_server /usr/local/bin/tts_server

# Copy shared libraries not in distroless base
COPY --from=builder /runtime-libs/ /

# Copy default config
COPY --from=builder /build/config/server.toml /etc/voxtral/server.toml

EXPOSE 9090

ENV TTS_CONFIG_PATH=/etc/voxtral/server.toml

ENTRYPOINT ["tts_server"]
