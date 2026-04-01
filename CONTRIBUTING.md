# Contributing to voxtral-server

Thank you for your interest in contributing! This document covers the
development setup, coding standards, and pull request process.

## Development Setup

### Prerequisites

- CMake 3.20+
- C++20 compiler (GCC 12+, Clang 15+, or MSVC 2022+)
- Git

No external libraries need to be installed — all dependencies are fetched
automatically via CMake FetchContent.

### Building

```bash
git clone https://github.com/vbomfim/voxtral-server.git
cd voxtral-server
git submodule update --init --recursive

# Debug build with tests (default)
cmake -B build
cmake --build build -j$(nproc)

# Run tests
cd build && ctest --output-on-failure
```

### IDE Support

Generate `compile_commands.json` for clangd / VS Code:

```bash
cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
# Link or copy build/compile_commands.json to the project root
```

## Bug Reports

Use the [Bug Report template](https://github.com/vbomfim/voxtral-server/issues/new?template=bug_report.yml)
on GitHub. Include:
- Steps to reproduce
- Expected vs actual behavior
- Version / commit SHA
- Deployment method (Docker, K8s, source)
- Relevant logs

## Feature Requests

Use the [Feature Request template](https://github.com/vbomfim/voxtral-server/issues/new?template=feature_request.yml).
Describe the problem you're solving and your proposed approach.

## Pull Request Process

1. **Fork** the repository and create a feature branch from `main`
2. **Write tests first** — we follow TDD (Red → Green → Refactor)
3. **Make your changes** — keep commits small and focused
4. **Run the full test suite** — all 485+ tests must pass
5. **Update documentation** if you changed behavior or added features
6. **Open a PR** using the [PR template](.github/PULL_REQUEST_TEMPLATE.md)

### Commit Messages

Use the conventional format:

```
type: short description

Longer explanation if needed.
```

Types: `feat`, `fix`, `docs`, `test`, `ci`, `refactor`, `perf`, `chore`

Examples:
```
feat: add Japanese voice presets
fix: handle empty input in validation
docs: update API reference for /v1/voices
test: add edge cases for rate limiter overflow
ci: add arm64 to Docker build matrix
```

### CI Requirements

All of these must pass before a PR can be merged:

- **Build** — `cmake -B build && cmake --build build -j8`
- **Unit tests** — `cd build && ctest --output-on-failure`
- **CodeQL** — static security analysis (automated)
- **No secrets** — no credentials, tokens, or keys in code

## Coding Standards

### Language

- **C++20** — use standard library features (ranges, concepts, `std::format`
  when available)
- **No raw `new`/`delete`** — use smart pointers (`std::unique_ptr`,
  `std::shared_ptr`)
- **RAII everywhere** — resources are owned by objects with deterministic
  lifetimes

### Naming Conventions

| Element | Convention | Example |
|---------|-----------|---------|
| Types / Classes | PascalCase | `TtsServer`, `VoiceCatalog` |
| Functions / Methods | snake\_case | `find_voice()`, `is_ready()` |
| Variables | snake\_case | `model_path`, `queue_depth` |
| Constants | UPPER\_SNAKE | `MAX_INPUT_CHARS` |
| Namespaces | lowercase | `tts`, `tts::config` |
| Files | snake\_case | `inference_pool.cpp` |
| Booleans | is/has/can prefix | `is_ready()`, `has_permission` |

### File Structure

- **Headers** in `include/tts/` — public API only
- **Source** in `src/` — organized by module (config, logging, server, tts)
- **Tests** in `tests/` — one test file per module
- **Private headers** alongside their `.cpp` in `src/`

### Error Handling

- Use exceptions for unrecoverable errors (config validation, startup)
- Use return values (`bool`, `std::optional`, result types) for expected
  failures (auth, validation, queue full)
- Provide context in error messages — what failed and why
- Never swallow exceptions silently

### Security

- Never hardcode secrets — use environment variables
- Validate all input server-side
- Use constant-time comparison for security-sensitive strings
- Redact sensitive values in logs
- See [SECURITY.md](SECURITY.md) for the full model

## Testing

### Test Organization

| Target | Files | Purpose |
|--------|-------|---------|
| `tts_tests` | `tests/test_*.cpp` | Fast unit tests |
| `tts_qa_tests` | `tests/test_*_integration.cpp` | Integration + edge cases |
| `tts_e2e_tests` | `tests/test_binary_e2e.cpp` | Binary-level process tests |

### Writing Tests

- Use Google Test (GTest) — already configured
- Follow the existing pattern: `TEST(ModuleName, DescriptiveTestName)`
- Test the happy path, error paths, and edge cases
- Use `MockBackend` for tests that need a TTS backend

```cpp
TEST(VoiceCatalog, FindReturnsNullptrForUnknownVoice) {
    VoiceCatalog catalog;
    EXPECT_EQ(catalog.find("nonexistent_voice"), nullptr);
}
```

## License

By contributing, you agree that your contributions will be licensed
under the [MIT License](LICENSE).
