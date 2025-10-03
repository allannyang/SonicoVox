# Vosk Integration Guide - Python Bridge

## Current Status
✅ **Full implementation complete** - C++ + Python integration ready
✅ **Build successful** - All code compiles and links
⚠️ **Vosk model needed** - Download to enable subtitles

## Quick Start (5 minutes)

### 1. Download Vosk Model
1. Download `vosk-model-small-en-us-0.15.zip` (~40MB) from:
   https://alphacephei.com/vosk/models
2. Extract to: `C:\Users\allan\OneDrive\Desktop\Code\sonicovox\models\vosk-model-small-en-us-0.15\`

Your directory should look like:
```
sonicovox/
  models/
    vosk-model-small-en-us-0.15/
      am/
      graph/
      conf/
      ...
```

### 2. Install Python Vosk (Already Done!)
You already have `pip install vosk` - you're good to go!

### 3. Run the Overlay
```bash
build\overlays\ring\Release\sv_overlay_ring.exe
```

Subtitles will automatically appear when speech is detected!

## How It Works

**Architecture:**
```
C++ Ring Overlay (sv_overlay_ring.exe)
  ↓ Captures audio from WASAPI loopback
  ↓ Sends int16 PCM via stdin
Python Worker (scripts/vosk_worker.py)
  ↓ Processes with Vosk library
  ↓ Returns JSON transcripts via stdout
C++ Subtitle Renderer
  ↓ Displays text with DirectWrite
```

## Features Already Implemented

✅ **Directional Hints** - Subtitles shift left/right based on stereo position
✅ **Adaptive Opacity** - Fades during loud audio (action sequences)
✅ **Partial Results** - Shows real-time transcription
✅ **Final Results** - Displays confirmed text
✅ **Directional Arrows** - Visual indicators for off-screen audio

## Configuration (Code)

```cpp
// In ring_app.cpp - already wired up!
SubtitleConfig config;
config.enabled = true;
config.directionalHints = true;   // Toggle arrows/positioning
config.adaptiveOpacity = true;    // Toggle opacity during action

m_speechService.setSubtitleConfig(config);
```

## Future Menu Integration
All toggles ready for UI:
```cpp
if (ImGui::Checkbox("Directional Hints", &config.directionalHints)) {
    m_speechService.setSubtitleConfig(config);
}
```

## Troubleshooting

**"Speech service initialization failed"**
- Check model path exists: `models/vosk-model-small-en-us-0.15/`
- Verify Python is in PATH: `python --version`
- Ensure `vosk` package installed: `pip list | grep vosk`

**No subtitles appear**
- Model must be exact path: `models/vosk-model-small-en-us-0.15/`
- Check DebugView for Python errors
- Verify audio is playing (ring should animate)
