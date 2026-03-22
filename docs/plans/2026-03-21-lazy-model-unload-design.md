# Lazy Model Loading and Idle Unload

## Goal
Reduce idle tray memory below 100 MB by avoiding a resident Whisper model when the app is idle. Keep the first transcription after idle within a tolerable 1–2 second delay, and keep back-to-back dictations fast.

## Approach
1. Load the Whisper model only when transcription starts.
2. Keep a short warm window after each transcription, then unload the model.
3. Restart the idle timer whenever the app returns to IDLE so unloading actually happens.

## Behavior
- Startup:
  - Verify the model file exists at `<exe-dir>\models\ggml-base.en.bin`.
  - Store the model path in the transcriber, but do not load it.
- Transcription:
  - `transcribeAsync` lazy-loads the model if needed and runs inference.
  - When transcription completes, the app returns to IDLE and restarts the idle timer.
- Idle:
  - A timer fires every 10 seconds while idle.
  - If the app has been idle for 45 seconds and the transcriber is not busy, unload the model.

## Error Handling
- If the model file is missing at startup, show the existing model-not-found dialog and exit.
- If a transcription attempt fails to initialize the model, show a busy/error tooltip and return to IDLE.

## Testing
- Verify Task Manager memory drops below 100 MB after ~45 seconds idle.
- Verify first transcription after idle loads in ~1–2 seconds.
- Verify back-to-back transcriptions stay fast and do not re-load the model.
- Verify the model-not-found dialog still appears on missing files.
