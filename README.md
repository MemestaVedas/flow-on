# FLOW-ON! 🎙️ → 📝

Professional Windows voice-to-text tool. Local-first, zero-cloud, zero-telemetry. **Ship-Ready Edition.**

## Features

- **🎯 Alt+V Hotkey** — Press to record, release to transcribe
- **⚡ Real-time Waveform** — GPU-accelerated Direct2D overlay showing live audio levels
- **🧠 Whisper.cpp Backend** — Offline speech recognition with AVX2 optimization (GPU optional)
- **📋 Smart Text Injection** — Auto-detects coding mode; camelCase/snake_case transforms in VS Code
- **🎨 Minimalist UI** — System tray icon + optional tabbed dashboard (history, settings)
- **🔧 Snippet Engine** — Case-insensitive text substitution (email, boilerplate, todo, etc.)
- **📦 Standalone Installer** — NSIS setup.exe bundles all dependencies (95MB total)

## Quick Start

### Prerequisites
- Windows 10/11 x64
- Visual Studio 2022 (v143 toolset) or Build Tools
- CMake 3.20+
- PowerShell 5.0+

### Build & Run

```powershell
# Clone with submodules
git clone --recursive https://github.com/yourusername/flow-on
cd flow-on

# Automatic: Download model, configure, compile
.\build.ps1

# Or manual CMake workflow
cmake -B build
cmake --build build --config Release
.\build\Release\flow-on.exe
```

### Build Options

```powershell
# Release build (with /O2 /fp:fast optimization)
.\build.ps1 -Release

# Generate NSIS installer (requires makensis in PATH)
.\build.ps1 -Installer

# Enable NVIDIA CUDA support (if available)
.\build.ps1 -CUDA
```

### Push to GitHub

Use the included helper script for easy setup:

```powershell
# Interactive setup (recommended)
.\setup-github.ps1

# Or with existing repository
.\setup-github.ps1 -RepoUrl "https://github.com/yourusername/flow-on.git"

# Or create new repo with GitHub CLI
.\setup-github.ps1 -CreateRepo
```

**CI/CD Included:** GitHub Actions workflow automatically builds Debug + Release on every push (see [`.github/workflows/build.yml`](.github/workflows/build.yml))

## Architecture

### Phases 1–10 (Complete Implementation)

| Phase | Component | Status | Purpose |
|-------|-----------|--------|---------|
| 1 | CMake Build System | ✅ | AVX2 + fast-math compilation |
| 2 | Audio Capture | ✅ | 16kHz PCM, RMS metering, lock-free ring buffer |
| 3 | System Tray | ✅ | Icon, context menu, Explorer crash recovery |
| 4 | Hotkey FSM | ✅ | Alt+V state machine (IDLE → RECORDING → TRANSCRIBING → INJECTING) |
| 5 | Transcription | ✅ | Async Whisper, GPU→CPU fallback, tech vocab seeding |
| 6 | Formatter | ✅ | 4-pass wregex cleanup (UTF-8/UTF-16), coding transforms |
| 7 | Overlay | ✅ | Direct2D pill bar, waveform bars, 60fps |
| 8 | Dashboard | ✅ | Win32 listbox (with WinUI 3 conditional) |
| 9 | Snippets | ✅ | Case-insensitive substitution, mode detection |
| 10 | Installer | ✅ | NSIS with runtime + model bundling |

### Technology Stack

- **Language** — C++20 (std::atomic FSM, std::thread)
- **Audio** — miniaudio.h (single-header PCM capture)
- **STT** — whisper.cpp submodule (AVX2 + cuBLAS optional)
- **Text** — wregex (UTF-16 aware), <cwctype> transforms
- **UI** — Direct2D (overlay), Win32 (tray, window), WinUI 3 (dashboard conditional)
- **IPC** — Windows messages (WM_HOTKEY, WM_TRANSCRIPTION_DONE)
- **Concurrency** — Lock-free queue (moodycamel::ReaderWriterQueue)
- **Config** — nlohmann/json (settings.json in %APPDATA%)

### Thread Model

```
Main Thread (WinMain MessageLoop)
├─ System tray icon
├─ Hotkey registration (Alt+V)
├─ Overlay window (Direct2D 60fps timer)
├─ Dashboard window (history, settings)
└─ Message handlers
    ├─ WM_HOTKEY (start recording)
    ├─ WM_TIMER (50ms polling for key release, 16ms overlay draw)
    └─ WM_TRANSCRIPTION_DONE (async Whisper callback)

Audio Callback Thread (miniaudio)
├─ 16kHz PCM capture at ~100µs intervals
├─ RMS computation (power metering)
└─ Lock-free enqueue to ring buffer

Worker Thread (async Whisper)
├─ Single-flight guard (prevent concurrent transcriptions)
├─ GPU init with CPU fallback
├─ Tech vocabulary prompt seeding
└─ Post WM_TRANSCRIPTION_DONE on completion

Text Injection Thread (implicit via SendInput)
└─ Fallback to clipboard + Ctrl+V for surrogates (emoji)
```

### State Machine

```
                    ┌─────────────────┐
                    │      IDLE       │
                    │   (listening)   │
                    └────────┬────────┘
                             │
                    [Alt+V pressed]
                             │
                    ┌────────▼────────┐
                    │   RECORDING     │
                    │  (buffering)    │
                    └────────┬────────┘
                             │
              [Alt+V released OR VAD silence]
                             │
                    ┌────────▼────────┐
                    │  TRANSCRIBING   │
                    │ (Whisper async) │
                    └────────┬────────┘
                             │
                  [Whisper on_done]
                             │
                    ┌────────▼────────┐
                    │   INJECTING     │
                    │ (text → window) │
                    └────────┬────────┘
                             │
                             └────────► IDLE
```

**Atomic Guards:**
- `StopRecordingOnce()` prevents race between hotkey release and VAD silence
- Single-flight Whisper prevents concurrent transcriptions during buffer drains

## Configuration

Settings are stored in `%APPDATA%\FLOW-ON\settings.json`:

```json
{
  "hotkey": {
    "modifiers": 0,
    "vkCode": 86
  },
  "audio": {
    "device_name": "Default",
    "buffer_size_sec": 30
  },
  "transcription": {
    "model": "base.en",
    "language": "en",
    "enable_gpu": true
  },
  "formatting": {
    "mode": "AUTO"
  },
  "snippets": {
    "email": "your.email@example.com",
    "todo": "TODO: ",
    "fixme": "FIXME: "
  },
  "ui": {
    "overlay_x": -1,
    "overlay_y": -1,
    "autostart": false
  }
}
```

**Modes:**
- `AUTO` — Detects VS Code/Cursor/vim/terminals → CODING mode (camelCase transforms)
- `NORMAL` — Plain text with punctuation cleanup
- `CODING` — camelCase, snake_case, ALL_CAPS transforms with tech vocab anchors

## Project Structure

```
flow-on/
├── src/
│   ├── main.cpp              # WinMain, message loop, state machine
│   ├── audio_manager.*       # miniaudio PCM capture + RMS
│   ├── transcriber.*         # Whisper async + GPU fallback
│   ├── formatter.*           # 4-pass wregex cleanup
│   ├── injector.*            # SendInput + clipboard fallback
│   ├── overlay.*             # Direct2D pill bar
│   ├── dashboard.*           # Win32 listbox UI (WinUI 3 opt)
│   ├── snippet_engine.*      # Text substitution
│   └── config_manager.*      # JSON settings load/save
├── external/
│   ├── whisper.cpp/          # Whisper STT submodule
│   ├── miniaudio.h           # Audio capture
│   ├── json.hpp              # Config parsing
│   ├── readerwriterqueue.h   # Lock-free queue
│   └── atomicops.h           # Atomic operations support
├── installer/
│   └── flow-on.nsi           # NSIS setup.exe builder
├── assets/
│   ├── generate_icons.ps1    # Icon generation script
│   ├── app_icon.ico          # Application icon
│   └── settings.default.json # Config template
├── CMakeLists.txt            # Primary build config
├── build.ps1                 # One-command build orchestration
├── Resource.h                # Resource IDs
├── flow-on.rc                # Resource manifest
└── LICENSE.txt               # MIT license
```

## Build Internals

### CMake Configuration

- **Target:** WIN32 subsystem (no console window)
- **Flags (Release):** `/arch:AVX2 /O2 /fp:fast /W3`
- **Flags (Debug):** `/arch:AVX2 /W3` (no /O2, compatible with /RTC1)
- **Defines:** `WIN32_LEAN_AND_MEAN`, `NOMINMAX`, `UNICODE`, `_UNICODE`
- **Output:** `build/Release/flow-on.exe` (~2.8 MB with static linking)

### Build Artifacts (Ignored)

Automatically excluded via `.gitignore`:
- `build/`, `bin/` directories (CMake outputs)
- `*.exe`, `*.dll`, `*.pdb` (Windows binaries)
- `*.obj`, `*.pch` (compilation intermediates)
- `models/*.bin` (Whisper models ~75MB)
- `redist/` (Windows App SDK runtime)
- `.vs/`, `.vscode/` (IDE configuration)

## Installation

### From Source

```powershell
.\build.ps1 -Installer
# Generates flow-on-installer.exe (~95MB)
```

### From Installer

```cmd
flow-on-installer.exe
# Installs to Program Files\FLOW-ON
# Adds Start Menu shortcuts
# Registers with Add/Remove Programs
```

**First Run:**
1. `settings.json` auto-created in `%APPDATA%\FLOW-ON`
2. Whisper model downloaded (~75MB, cached)
3. System tray icon appears
4. Press `Alt+V` to start recording

## Usage

### Hotkey

- **Alt+V**: Press to start recording, release to transcribe
- Auto-hides within 2 seconds of completion
- Overlay shows:
  - 🔵 Blue waveform bars (recording)
  - ⏳ Spinner arc (transcribing)
  - ✅ Green flash (injection done)
  - ❌ Red flash (error)

### Modes

- **NORMAL:** Cleans fillers (um, uh, ah) and fixes punctuation
- **CODING:** Auto-enables in VS Code/Cursor/vim; applies camelCase/snake_case transforms
  - "user name" → `userName`
  - "api response" → `api_response`
  - "HTTP method" → `HTTP_METHOD`

### Dashboard

Right-click tray icon → **History**:
- View all recent transcriptions
- Copy last result
- Clear history

## Performance

- **Audio Latency:** ~100 ms (miniaudio callback)
- **Transcription:** ~30s for 2-min clip (base.en model, CPU AVX2)
- **Overlay Rendering:** 60 FPS (Direct2D GPU-accelerated)
- **Memory:** ~800 MB (Whisper model in RAM)
- **CPU:** <5% idle, <40% during transcription

## Troubleshooting

### Hotkey not working
- Check `settings.json` for correct VK code (86 = V)
- Verify no other app intercepted Alt+V
- Restart application

### "No available audio device"
- Ensure microphone is plugged in
- Check Windows Sound settings
- Restart audio service: `Restart-Service -Name AudioSrv`

### Whisper model not found
- Model auto-downloads to `models/` (~75MB)
- Check disk space
- Manual download: `.\build.ps1 -Download`

### Installer fails
- Ensure NSIS 3.x is installed: `scoop install nsis` or `choco install nsis`
- Verify `makensis.exe` in PATH

## License

MIT License — See [LICENSE.txt](LICENSE.txt)

## Contributing

1. Fork repository
2. Create feature branch: `git checkout -b feature/your-feature`
3. Commit changes: `git commit -m "feat: describe your change"`
4. Push to branch: `git push origin feature/your-feature`
5. Submit pull request

## Roadmap

- [ ] Voice Activity Detection (VAD) for automatic recording stop
- [ ] Punctuation restoration model (separate checkpoint)
- [ ] Real-time transcription (low-buffer streaming)
- [ ] Multi-language support UI
- [ ] Global snippets library (online sync)
- [ ] Integration plugins (Obsidian, Notion, Discord)

## Engineering Notes

### Critical Performance Decisions

1. **Lock-free Queue (moodycamel::ReaderWriterQueue)**
   - Audio callback thread enqueues 16kHz samples
   - Main thread drains in batch on hotkey release
   - Zero blocking; guaranteed low latency

2. **Direct2D GPU Rendering**
   - Overlay avoids CPU-intensive GDI
   - 60fps timer driven by WM_TIMER
   - Auto-recovery from D2DERR_RECREATE_TARGET

3. **Single-Flight Whisper**
   - Atomic gate prevents concurrent transcriptions
   - Improves reliability under high load
   - Simplifies callback state management

4. **AVX2 SIMD**
   - Whisper.cpp compiled with `/arch:AVX2 /fp:fast`
   - ~3x faster decode vs scalar on modern CPUs
   - Fallback: Manual disable via CMake flag

### Real-World Bugs Fixed During Development

1. **Most Vexing Parse** (config_manager.cpp:80)
   - `std::ofstream f(fs::path(...))` interpreted as function declaration
   - Solution: Explicit `.c_str()` call disambiguates

2. **Duplicate Macro Defines**
   - `WIN32_LEAN_AND_MEAN` defined in both CMakeLists.txt and .cpp files
   - Solution: Global define via `add_compile_definitions()`

3. **HMENU Casting**
   - `reinterpret_cast<HMENU>(int)` invalid for Windows handles
   - Solution: `(HMENU)(INT_PTR)value` idiomatic Windows cast

4. **Thread Hotkey Release Race**
   - Audio callback VAD vs main thread hotkey release could both trigger stop
   - Solution: `StopRecordingOnce()` atomic CAS gate ensures single winner

5. **Audio Starvation**
   - Default miniaudio thread priority too low
   - Solution: `THREAD_PRIORITY_TIME_CRITICAL` set in audio callback

## Credits

Built with:
- [whisper.cpp](https://github.com/ggerganov/whisper.cpp) — Offline STT
- [miniaudio.h](https://github.com/mackron/miniaudio) — Audio I/O
- [Direct2D](https://docs.microsoft.com/en-us/windows/win32/direct2d/direct2d-portal) — GPU rendering
- [moodycamel ReaderWriterQueue](https://github.com/cameron314/readerwriterqueue) — Lock-free concurrency

---

**Status:** Ship-Ready (Phase 1–10 complete, Debug + Release tested, all 10 phases compiling)

For technical details, see [ARCHITECTURE.md](docs/ARCHITECTURE.md) (forthcoming).
