# voxtral-server

High-performance text-to-speech (TTS) inference server powered by
[Voxtral-4B-TTS](https://huggingface.co/mistralai/Voxtral-4B-TTS) via
[voxtral-tts.c](https://github.com/mudler/voxtral-tts.c).
OpenAI-compatible REST API, 20 multilingual voices, single-binary deployment.

> **Model license:** The Voxtral-4B-TTS model weights are released by Mistral AI
> under [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/).
> The model weights are **not for commercial use**. The server code itself is MIT-licensed.

---

## Features

- **OpenAI-compatible API** — drop-in replacement for `POST /v1/audio/speech`
- **20 multilingual voices** — English, French, German, Spanish, Portuguese,
  Italian, Dutch, Arabic, Hindi
- **Single binary** — C++20, no runtime dependencies beyond libc
- **GPU acceleration** — optional CUDA support for faster inference
- **Production-ready** — rate limiting, bearer-token auth, Prometheus metrics,
  structured JSON logging, health/readiness probes
- **Container-first** — distroless Docker images, Kubernetes manifests
- **485 unit tests** — comprehensive coverage across all modules

## Quick Start

### From Source

```bash
# Clone
git clone https://github.com/vbomfim/voxtral-server.git
cd voxtral-server
git submodule update --init --recursive

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# Run
export TTS_MODEL_PATH=/path/to/voxtral-4b
./build/tts_server
```

### Docker

```bash
# CPU
docker build -f docker/Dockerfile -t voxtral-server:cpu .
docker run -p 9090:9090 \
  -v /path/to/models:/models:ro \
  -e TTS_MODEL_PATH=/models/voxtral-4b \
  voxtral-server:cpu

# GPU (requires nvidia-container-toolkit)
docker build -f docker/Dockerfile.cuda -t voxtral-server:cuda .
docker run --gpus all -p 9090:9090 \
  -v /path/to/models:/models:ro \
  -e TTS_MODEL_PATH=/models/voxtral-4b \
  voxtral-server:cuda
```

### Docker Compose

```bash
# Place model in docker/models/ directory, then:
cd docker
TTS_API_KEY=your-secret docker compose up

# With TLS (nginx reverse proxy):
TTS_API_KEY=your-secret docker compose -f docker-compose.tls.yml up
```

### Kubernetes

```bash
# Local development
kubectl apply -f k8s/local/all-in-one.yaml
curl http://localhost:30090/health

# Production
kubectl apply -f k8s/namespace.yaml
kubectl apply -f k8s/configmap.yaml
kubectl apply -f k8s/serviceaccount.yaml
kubectl apply -f k8s/pvc.yaml
kubectl create secret generic voxtral-server-secrets \
  --from-literal=TTS_API_KEY=your-key -n opentts
kubectl apply -f k8s/deployment.yaml      # CPU
kubectl apply -f k8s/deployment-gpu.yaml   # or GPU
kubectl apply -f k8s/service.yaml
kubectl apply -f k8s/networkpolicy-default-deny.yaml
kubectl apply -f k8s/networkpolicy.yaml
```

## API Reference

### Synthesize Speech

```bash
curl -X POST http://localhost:9090/v1/audio/speech \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $TTS_API_KEY" \
  -d '{
    "model": "voxtral-4b",
    "input": "Hello, world!",
    "voice": "neutral_female",
    "response_format": "wav",
    "speed": 1.0
  }' \
  --output speech.wav
```

### Health Check

```bash
curl http://localhost:9090/health
# {"status":"ok","version":"0.1.0"}
```

### Readiness Probe

```bash
curl http://localhost:9090/ready
# {"ready":true,"queue_depth":0,"active_jobs":0}
```

### Prometheus Metrics

```bash
curl http://localhost:9090/metrics
```

### List Voices

```bash
curl http://localhost:9090/v1/voices
```

## Voices

| ID | Name | Language |
|----|------|----------|
| `ar_male` | Arabic Male | ar |
| `de_female` | German Female | de |
| `de_male` | German Male | de |
| `casual_female` | Casual Female | en |
| `casual_male` | Casual Male | en |
| `cheerful_female` | Cheerful Female | en |
| `neutral_female` | Neutral Female | en |
| `neutral_male` | Neutral Male | en |
| `es_female` | Spanish Female | es |
| `es_male` | Spanish Male | es |
| `fr_female` | French Female | fr |
| `fr_male` | French Male | fr |
| `hi_female` | Hindi Female | hi |
| `hi_male` | Hindi Male | hi |
| `it_female` | Italian Female | it |
| `it_male` | Italian Male | it |
| `nl_female` | Dutch Female | nl |
| `nl_male` | Dutch Male | nl |
| `pt_female` | Portuguese Female | pt |
| `pt_male` | Portuguese Male | pt |

## Configuration

Configuration uses TOML file (`config/server.toml`) with environment variable
overrides. Environment variables always win (12-factor app).

| Environment Variable | Default | Description |
|---------------------|---------|-------------|
| `TTS_CONFIG_PATH` | `config/server.toml` | Path to TOML config file |
| `TTS_HOST` | `0.0.0.0` | Bind address |
| `TTS_PORT` | `9090` | HTTP port |
| `TTS_MAX_CONNECTIONS` | `100` | Max concurrent connections |
| `TTS_MODEL_PATH` | *(required)* | Path to model directory |
| `TTS_DEFAULT_VOICE` | `neutral_female` | Default voice ID |
| `TTS_WORKERS` | `1` | Inference worker threads |
| `TTS_MAX_QUEUE_DEPTH` | `10` | Max queued requests |
| `TTS_MAX_INPUT_CHARS` | `4096` | Max input text characters |
| `TTS_REQUEST_TIMEOUT_SECONDS` | `300` | Inference timeout |
| `TTS_API_KEY` | *(empty)* | Bearer token for auth |
| `TTS_REQUIRE_AUTH` | `false` | Require authentication |
| `TTS_RATE_LIMIT_MAX` | `10` | Auth failure limit per window |
| `TTS_RATE_LIMIT_WINDOW` | `60` | Auth rate limit window (seconds) |
| `TTS_REQUEST_RATE_LIMIT_RPM` | `10` | Requests per minute per IP |
| `TTS_TRUST_PROXY` | `false` | Trust X-Forwarded-For header |
| `TTS_TRUSTED_PROXY_HOPS` | `1` | Number of trusted proxy hops |
| `TTS_LOG_LEVEL` | `info` | Log level (debug, info, warn, error) |
| `TTS_LOG_FORMAT` | `text` | Log format (`text` or `json`) |

## Model Download

The Voxtral-4B-TTS model weights are **not included** in this repository.

> ⚠️ **License:** The model weights are released under
> [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/) by Mistral AI.
> This means they are free for non-commercial use only.

Download from Hugging Face:
```bash
# Requires huggingface-cli (pip install huggingface-hub)
huggingface-cli download mistralai/Voxtral-4B-TTS-GGUF \
  --local-dir /path/to/models/voxtral-4b
```

## Building

### Requirements

- CMake 3.20+
- C++20 compiler (GCC 12+, Clang 15+, MSVC 2022+)
- Git (for FetchContent dependencies)

### Build Options

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTS=ON \
  -DBUILD_BENCHMARKS=OFF

cmake --build build -j$(nproc)
```

### Run Tests

```bash
cd build && ctest --output-on-failure
```

## Architecture

See [ARCHITECTURE.md](ARCHITECTURE.md) for the full component diagram,
threading model, and data flow.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup, coding standards,
and pull request process.

## Security

See [SECURITY.md](SECURITY.md) for the security model, vulnerability reporting,
and deployment hardening guide.

## License

- **Server code:** [MIT License](LICENSE)
- **Voxtral-4B-TTS model weights:** [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/) (Mistral AI)

See [NOTICE](NOTICE) for full attribution.
