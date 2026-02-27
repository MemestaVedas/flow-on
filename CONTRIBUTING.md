# Contributing to FLOW-ON!

Thank you for considering contributions to FLOW-ON! This document provides guidelines for getting started.

## Development Setup

### Prerequisites
- Visual Studio 2022 (Community/Pro/Enterprise)
- CMake 3.20+
- Git with submodules support
- PowerShell 5.0+

### Clone & Configure

```powershell
# Clone with whisper.cpp submodule
git clone --recursive https://github.com/yourusername/flow-on.git
cd flow-on

# Configure Python env (optional, for build scripts)
python -m venv venv
.\venv\Scripts\Activate.ps1

# Build
.\build.ps1
```

## Code Organization

### Phases (by feature layer)

```
Phase 1: CMakeLists.txt + build infrastructure
├─ CMake files, build flags, dependency management

Phase 2: Audio subsystem (audio_manager.*)
├─ miniaudio PCM capture, RMS computation, ring buffer

Phase 3: System integration (main.cpp tray handling)
├─ Win32 tray icon, context menu, app lifecycle

Phase 4: Input handling (main.cpp hotkey FSM)
├─ Alt+V hotkey, atomic state machine

Phase 5: Transcription (transcriber.*)
├─ Async Whisper, GPU/CPU fallback, prompt seeding

Phase 6: Text processing (formatter.*, injector.*)
├─ wregex cleanup, SendInput, clipboard fallback

Phase 7: Visual feedback (overlay.*)
├─ Direct2D rendering, waveform, animations

Phase 8: User interface (dashboard.*)
├─ History UI, settings, WinUI 3 optional

Phase 9: Workflow optimization (snippet_engine.*)
├─ Text substitution, mode detection

Phase 10: Distribution (installer/, build.ps1)
├─ NSIS installer, bundling, versioning
```

Each phase is **loosely coupled**, allowing independent testing and review.

## Commit Message Convention

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
<type>(<scope>): <subject>

<body>

<footer>
```

### Types
- `feat` — New feature (phase progression, new component)
- `fix` — Bug fix (compiler errors, runtime issues, race conditions)
- `perf` — Performance optimization (SIMD, threading, rendering)
- `refactor` — Code restructuring (no functional change)
- `docs` — Documentation, READMEs, comments
- `ci` — CI/CD, build scripts, automation
- `test` — Test additions or fixes

### Scopes
- `audio` — audio_manager subsystem
- `transcriber` — Whisper integration
- `formatter` — Text processing
- `injector` — Text injection
- `overlay` — Direct2D rendering
- `dashboard` — Win32/WinUI3 UI
- `snippets` — Snippet engine
- `config` — Settings management
- `main` — Main loop, FSM, tray
- `build` — CMake, scripts, compilation
- `installer` — NSIS, packaging
- `docs` — Documentation

### Examples

```
feat(overlay): Add waveform visualization to Direct2D pill bar

- Compute 16 RMS values from rolling window
- Render as 16 vertical bars, height proportional to RMS
- Update at 60fps with atomic pushRMS() from audio thread

Fixes #42
```

```
perf(transcriber): Enable AVX2 compilation for Whisper

- Add /arch:AVX2 flag via cmake generator expression
- Reduces inference time by ~60% (base.en model)

Related-To: #15
```

## Testing & Validation

### Compilation

```powershell
# Debug build
cmake -B build
cmake --build build --config Debug

# Release build (with optimizations)
cmake --build build --config Release
```

### Smoke Testing

1. **Hotkey activation** — Alt+V triggers recording
2. **Audio capture** — Overlay shows waveform
3. **Transcription** — Whisper processes audio
4. **Text injection** — Result appears in text field
5. **Tray menu** — Right-click shows "History" and "Quit"

### Debugging

#### Visual Studio Debugger

```powershell
# Generate VS solution via CMake
cmake -B build -G "Visual Studio 17 2022"
# Open build/flow-on.sln in Visual Studio
# F5 to launch with debugger
```

#### Output Logging

To enable debug prints, rebuild with:

```powershell
cmake -B build -DENABLE_DEBUG_LOG=ON
cmake --build build --config Debug
```

(Currently no debug logging; add via printf/OutputDebugString as needed)

## Architecture Decision Records (ADRs)

### ADR-001: Lock-Free Queue (moodycamel::ReaderWriterQueue)

**Context:** Audio callback thread must not block; main thread drains samples in batch.

**Decision:** Use moodycamel SPSC queue (single producer = audio, single consumer = main).

**Rationale:**
- Zero-copy, lock-free design
- Bounded latency on audio callback (critical for real-time audio)
- Simple API (enqueue, try_dequeue)

**Consequences:**
- Small additional dependency (readerwriterqueue.h)
- Requires understanding of SPSC guarantees (one reader, one writer)
- atomicops.h must be present

### ADR-002: Direct2D for Overlay Rendering

**Context:** Overlay must render 60fps without blocking input thread.

**Decision:** Use Direct2D (GPU) instead of GDI (CPU).

**Rationale:**
- GPU-accelerated rendering scales to 60fps trivially
- D2D handles window composition automatically
- Can render semi-transparent shapes and animations efficiently

**Consequences:**
- Requires DXGI + D2D1 DLLs (always present on Win10+)
- Error recovery for D2DERR_RECREATE_TARGET (GPU device reset)
- More complex than GDI, but necessary for 60fps+ performance

### ADR-003: Atomic Bool for FSM State

**Context:** State transitions must be fast and race-free across multiple threads.

**Decision:** Use `std::atomic<uint32_t>` for FSM state (enum cast).

**Rationale:**
- Single atomic read/write per state change
- No locks or condition variables needed
- Enables single-flight patterns (atomic CAS)

**Consequences:**
- Must understand memory ordering (std::memory_order_seq_cst used)
- State transitions are immediate; callers must handle "rejected" transitions
- Difficult to debug without atomic inspector tools

### ADR-004: Async Whisper with Single-Flight Guard

**Context:** Transcription can take 10–30 seconds; must not block main thread.

**Decision:** Spawn detached thread per transcription with single-flight guard to prevent queuing.

**Rationale:**
- Non-blocking main loop (tray, overlay, hotkey all responsive)
- No concurrent Whisper contexts (simplifies error handling)
- Single-flight prevents pathological multi-record scenarios

**Consequences:**
- User must wait for transcription to complete (no queueing)
- Requires careful cleanup (detached thread + atomic gate)
- Difficult to cancel transcription mid-process

## Performance Budgets

(Aspirational; measure actual performance in your environment)

| Component | Budget | Actual |
|-----------|--------|--------|
| Audio capture latency | <50ms | ~100ms |
| Overlay render time | <16ms | ~2ms |
| Key release → injected text | <3s | ~2.5s (Whisper + formatting) |
| Memory usage (idle) | <200MB | ~150MB |
| Memory usage (recording) | <500MB | ~400MB (30s buffer) |
| CPU (idle) | <1% | <1% |
| CPU (recording) | <5% | ~3% |
| CPU (transcribing) | <50% | ~35% (single core) |

Measure via **Task Manager** (Processes tab, Whisper model will show as single thread usage).

## Known Limitations & TODOs

### Current

- [ ] VAD (Voice Activity Detection) — Currently manual hotkey release
- [ ] Real-time transcription — Currently batch mode (release-to-transcribe)
- [ ] Punctuation restoration — Currently crude regex (future separate model)
- [ ] Multi-language — Currently en-only (easy to add via settings)

### Future (Out of Scope)

- [ ] Cross-platform (macOS/Linux) — Windows-only (Win32 API)
- [ ] Web UI — Local app only (no server component)
- [ ] Cloud sync — Settings local only (no telemetry)

## Pull Request Process

1. **Create feature branch** — `git checkout -b feature/my-feature`
2. **Make changes** — Follow code style (see below)
3. **Test locally** — Verify Debug + Release builds pass
4. **Commit cleanly** — Use conventional commits; 1 logical change per commit
5. **Push** — `git push origin feature/my-feature`
6. **Open PR** — Provide context:
   - What problem does this solve?
   - How does it work?
   - Did you test it?
7. **Address review feedback** — Update commits as needed
8. **Merge** — Use "Squash and merge" for small PRs; "Create a merge commit" for large features

## Code Style

### C++20 Modern Style

- Prefer `std::atomic<T>` over `volatile`
- Use `std::thread` via lambdas instead of C-style threads
- Use `std::make_unique` for owned pointers
- Avoid raw `new`/`delete`; prefer RAII

### Win32 API

- Prefer `wide_string` (UTF-16) for Windows API calls
- Use `MultiByteToWideChar` for UTF-8 ↔ UTF-16 conversion
- Follow Windows naming: `HINSTANCE`, `HWND`, `LPARAM`, etc. (all caps)

### Threading

- Add `// Thread-safe: <reason>` comment above shared state
- Use atomic for simple counters; mutex for complex structures
- Prefer lock-free (atomic) over mutex where possible

### Comments

- **Why**, not what: Explain *intent*, not code
- Bad: `// Increment i`
- Good: `// Single-flight guard: CAS ensures only one thread wins the stop`

### File Organization

```cpp
// foo.h — Header file

#pragma once

#include <windows.h>
#include <atomic>

// Forward declarations (if needed)

/**
 * @brief Describe class/function purpose (1 sentence)
 * 
 * Thread-safety: [description]
 * Performance: [critical constraints]
 */
class Foo {
public:
    Foo();
    ~Foo();
    
    void doThing();
    void doOtherThing();

private:
    std::atomic<int> m_state;  // m_ prefix for members
    HWND m_hwnd;
};
```

```cpp
// foo.cpp — Implementation

#include "foo.h"
#include <iostream>

Foo::Foo() : m_state(0), m_hwnd(nullptr) {}

Foo::~Foo() {}

void Foo::doThing() {
    // ...
}
```

## Debugging Tips

### Hang on Hotkey

- Audio thread starved? Set `THREAD_PRIORITY_TIME_CRITICAL`
- Whisper never returns? Check GPU/Model path
- Overlay draws over injected text? Use Ctrl+Z to undo overlay damage

### Transcription Fails Silently

- Check `%APPDATA%\FLOW-ON\settings.json` for valid model path
- Whisper model must be in `models/` directory
- GPU fallback to CPU takes 10–30 seconds for first inference

### Text Injection Doesn't Work

- Check `InjectText()` returns true
- Surrogates (emoji)? Falls back to clipboard + Ctrl+V
- Target app must accept keyboard input (check active window)

### Overlay Flickers

- Direct2D device reset? Check for D2DERR_RECREATE_TARGET
- WM_TIMER firing too slowly? Ensure WM_TIMER is not being blocked
- Windows Aero? Try `Disable fullscreen optimizations` in exe properties

## Resources

- [whisper.cpp API](https://github.com/ggerganov/whisper.cpp/blob/master/whisper.h)
- [miniaudio docs](https://github.com/mackron/miniaudio#example-code)
- [Direct2D tutorial](https://docs.microsoft.com/en-us/windows/win32/direct2d/direct2d-tutorials)
- [Windows API Index](https://docs.microsoft.com/en-us/windows/win32/apiindex/windows-api-list)
- [NSIS Documentation](https://nsis.sourceforge.io/Docs/)

## Licensing

By contributing, you agree that your contributions will be licensed under the same MIT License as the project.

---

**Questions?** Open an issue or ask in pull request comments.

**Want to add a skill?** Great! See Phase 9 (snippet_engine.cpp) for patterns.

Happy coding! 🎙️
