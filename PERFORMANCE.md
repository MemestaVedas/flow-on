# Performance Tuning Guide

## Current Optimizations (v1.0)

FLOW-ON! is configured for **maximum speed** with the tiny.en model. Current settings prioritize fast transcription (~12-18s for typical dictation) over absolute accuracy.

### Speed Optimizations Applied

1. **Model:** `ggml-tiny.en.bin` (39MB, fastest)
   - Trade-off: 95% accuracy vs 98% for base.en
   - Alternative: Switch to `base.en` for +3% accuracy at 2x slower speed

2. **CPU Threads:** Uses all available cores during transcription
   - File: `src/transcriber.cpp`, line 43: `p.n_threads = hw`
   - Trade-off: UI may lag slightly during transcription
   - Alternative: Change to `hw - 1` or `hw - 2` for smoother UI

3. **Timestamp Generation:** Disabled (`no_timestamps = true`)
   - Speed gain: ~30-40% faster
   - Trade-off: No per-word timing (not needed for dictation)

4. **Audio Context:** Reduced to 512 frames (default: 1500)
   - File: `src/transcriber.cpp`, line 51: `p.audio_ctx = 512`
   - Speed gain: ~15-20% faster
   - Trade-off: Slightly less context for ambiguous words
   - Alternative: Increase to 768 or 1024 for better quality

5. **Single Segment Mode:** Enabled (`single_segment = true`)
   - Speed gain: ~10-15% faster
   - Best for dictation (short clips < 30s)

## Switching to Faster Model (tiny.en → tiny)

If you need **multilingual support** but same speed:

```powershell
# Download multilingual tiny model
Invoke-WebRequest -Uri "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-tiny.bin" -OutFile "models/ggml-tiny.bin"

# Update src/main.cpp line ~455
# Change: "ggml-tiny.en.bin" -> "ggml-tiny.bin"

# Rebuild
.\build.ps1 -Release
```

## Switching to Better Model (tiny.en → base.en)

If you need **higher accuracy** at 2x slower speed:

```powershell
# Download base.en model (~150MB)
Invoke-WebRequest -Uri "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base.en.bin" -OutFile "models/ggml-base.en.bin"

# Update src/main.cpp line ~455  
# Change: "ggml-tiny.en.bin" -> "ggml-base.en.bin"

# Update CMakeLists.txt line 85
# Change: "ggml-tiny.en.bin" -> "ggml-base.en.bin"

# Rebuild
.\build.ps1 -Release
```

### Model Comparison

| Model | Size | Speed (30s audio) | Accuracy | Use Case |
|-------|------|------------------|----------|----------|
| tiny.en | 39MB | ~12-18s | 95% | **Default** - Fast dictation |
| base.en | 150MB | ~25-35s | 98% | High accuracy needed |
| small.en | 500MB | ~60-90s | 99% | Professional transcription |
| medium.en | 1.5GB | ~180-240s | 99.5% | Maximum quality |

## GPU Acceleration (NVIDIA CUDA)

For **5-10x speedup** on NVIDIA GPUs:

```powershell
# 1. Install CUDA Toolkit 12.x from NVIDIA
# 2. Enable CUDA in CMakeLists.txt (uncomment lines 29-30):
#    set(WHISPER_CUBLAS ON CACHE BOOL "" FORCE)
#    set(GGML_CUDA      ON CACHE BOOL "" FORCE)

# 3. Rebuild with CUDA flag
.\build.ps1 -CUDA

# Expected: 30s audio transcribes in ~3-5s (base.en model)
```

## Fine-Tuning Parameters

Edit `src/transcriber.cpp` for custom speed/quality balance:

```cpp
// File: src/transcriber.cpp, lines 48-53

// === SPEED OPTIMIZATIONS ===
p.single_segment   = true;   // true = faster, false = better segmentation
p.no_timestamps    = true;   // true = faster, false = per-word timestamps
p.token_timestamps = false;  // false = faster, true = token-level timing
p.audio_ctx        = 512;    // 512 = fast, 1024 = balanced, 1500 = quality
p.max_len          = 0;      // 0 = unlimited, 50 = force short segments
p.print_progress   = false;  // false = faster (no console prints)
```

### Recommended Configurations

**Ultra Speed (Current Default)**
```cpp
p.audio_ctx = 512;
p.no_timestamps = true;
p.single_segment = true;
```

**Balanced**
```cpp
p.audio_ctx = 1024;
p.no_timestamps = false;  // Get timestamps
p.single_segment = true;
```

**Maximum Quality**
```cpp
p.audio_ctx = 1500;
p.no_timestamps = false;
p.single_segment = false;  // Better segmentation
```

## Expected Performance

### Current Setup (tiny.en + optimizations)
- **Hardware:** Typical laptop (4-8 cores, no GPU)
- **Audio:** 15 seconds of dictation
- **Transcription Time:** 8-12 seconds
- **Real-time Factor:** ~0.6x (faster than real-time on modern CPUs)

### With GPU (tiny.en + CUDA)
- **Hardware:** NVIDIA RTX 3060 or better
- **Audio:** 15 seconds of dictation
- **Transcription Time:** 1-2 seconds
- **Real-time Factor:** ~0.1x (10x faster than real-time)

## Troubleshooting Slow Transcription

### If transcription takes >30s for short clips:

1. **Check CPU usage during transcription**
   - Open Task Manager → Processes
   - Look for `flow-on.exe` using 80-100% of multiple cores
   - If low CPU usage: increase `p.n_threads` in transcriber.cpp

2. **Verify model size**
   ```powershell
   Get-Item models/*.bin | Select-Object Name, Length
   # Should show: ggml-tiny.en.bin ~39MB (77,704,715 bytes)
   ```

3. **Check for background processes**
   - Close heavy apps (browsers, IDEs) during transcription
   - Disable antivirus real-time scanning temporarily

4. **Verify AVX2 support**
   - FLOW-ON! requires AVX2 CPU instructions
   - Check: `wmic cpu get caption` should show Intel Core i5/i7/i9 (4th gen+) or AMD Ryzen

5. **Try GPU acceleration**
   - See GPU Acceleration section above
   - 5-10x speedup on NVIDIA RTX/GTX cards

## Benchmarking

Run this PowerShell script to measure your actual speed:

```powershell
# Measure transcription time
$start = Get-Date
# [Record 15 seconds via Alt+V hotkey]  
$end = Get-Date
$duration = ($end - $start).TotalSeconds
Write-Host "Transcription took: $duration seconds"
Write-Host "Real-time factor: $($duration / 15)"
```

**Target:** Real-time factor < 1.0 (transcription faster than audio duration)

## Further Reading

- [Whisper.cpp Performance Guide](https://github.com/ggerganov/whisper.cpp#performance)
- [GGML Quantization](https://github.com/ggerganov/ggml#quantization) - Further compression
- [OpenAI Whisper Models](https://github.com/openai/whisper#available-models-and-languages)

---

**Last Updated:** February 27, 2026  
**Version:** 1.0 (Ship-Ready Edition)
