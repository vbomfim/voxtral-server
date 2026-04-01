# voxtral-server

High-performance TTS inference server powered by [Voxtral-4B-TTS](https://huggingface.co/mistralai/Voxtral-4B-TTS) via [voxtral-tts.c](https://github.com/mudler/voxtral-tts.c).

> **Status:** Phase 1 — project scaffold, configuration, and logging.

## Building

```bash
cmake -B build
cmake --build build
```

## Running

```bash
# Minimal (requires model path)
TTS_MODEL_PATH=/path/to/model ./build/tts_server

# With custom config
./build/tts_server --config config/server.toml
```

## Configuration

Configuration is loaded from a TOML file (`config/server.toml` by default) and can be overridden by environment variables with the `TTS_` prefix. Environment variables always take precedence over file values.

See `config/server.toml` for all available options.

## Testing

```bash
cmake -B build -DBUILD_TESTS=ON
cmake --build build
cd build && ctest --output-on-failure
```

## Git Submodules (not yet added)

The following submodules will be added when upstream repos stabilize:

```bash
# When ready:
git submodule add https://github.com/uNetworking/uWebSockets.git third_party/uWebSockets
git submodule add https://github.com/mudler/voxtral-tts.c.git third_party/voxtral-tts.c
git submodule update --init --recursive
```

## License

This project is licensed under the MIT License — see [LICENSE](LICENSE).

The Voxtral-4B-TTS model weights are licensed under [CC BY-NC 4.0](https://creativecommons.org/licenses/by-nc/4.0/) by Mistral AI. See [NOTICE](NOTICE) for details.
