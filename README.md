# SonicoVox

**Real-time spatial audio visualization and live captioning for Windows.**

SonicoVox provides transparent screen overlays that visualize audio directionality and display live captions, helping you understand where sounds are coming from in games, streams, and applications—all without interrupting your workflow.

---

## Table of Contents

- [Overview](#overview)
- [Current Features](#current-features)
- [Architecture](#architecture)
- [Build Instructions](#build-instructions)
- [Usage](#usage)
- [Roadmap](#roadmap)
- [Technical Details](#technical-details)
- [Contributing](#contributing)

---

## Overview

SonicoVox is a **local-first, privacy-focused** audio enhancement suite for Windows that:

- **Visualizes spatial audio** with a real-time circular spectrum ring overlay
- **Provides live subtitles** for spoken content using Vosk speech recognition
- **Operates entirely in user-space** (no kernel drivers, no admin privileges)
- **Respects privacy** (no audio recording, no telemetry)
- **Stays out of your way** with click-through, topmost overlays

### Design Principles

- **Local-first**: All processing happens on your machine; no cloud dependencies
- **Modular architecture**: Swappable components connected via event bus and IPC
- **Fail-soft**: UI and overlays continue running even if analysis or STT stalls
- **Deterministic builds**: Reproducible with vcpkg manifest lockfiles
- **Privacy by default**: No audio persistence; transcripts disabled by default

---

## Current Features

### ✅ Ring Overlay with Live Subtitles (v0.2 - Implemented)

**Current Status:** Fully functional standalone overlay with speech recognition.

**Audio Visualization:**
- **Semi-transparent circular audio spectrum** with 64 directional bins
- **WASAPI loopback capture** of system audio (read-only, shared mode)
- **Channel layout detection** supporting stereo through 7.1+ configurations
- **Real-time FFT processing** with temporal smoothing
- **Adaptive gain** for quiet footsteps and directional sounds
- **Mono/stereo detection** with hysteresis for stable mode switching

**Live Subtitles:**
- **Real-time speech recognition** using Vosk (Python integration via subprocess)
- **Centered subtitle display** with automatic text wrapping
- **Smart accumulation** of partial results for smooth readability
- **Configurable display times** with quick cycling
- **48kHz sample rate support** for accurate transcription

**Overlay Features:**
- **Click-through overlay** (interact with applications beneath it)
- **Topmost window** without focus stealing
- **Per-Monitor V2 DPI awareness** for crisp rendering
- **DirectComposition** for flicker-free transparency
- **Global hotkey**: `Ctrl+Shift+V` to close overlay

**Executable:** `build/overlays/ring/Release/sv_overlay_ring.exe`

### 🚧 In Development

- **Main control dashboard** (ImGui) for overlay management
- **Settings persistence** (JSON configuration)
- **IPC layer** (named pipes) for app-to-overlay communication
- **Configurable visuals** (size, opacity, colors, position)
- **Multi-monitor support** with per-monitor overlay instances

### 📋 Planned Features

See [Roadmap](#roadmap) for milestone breakdown.

---

## Architecture

SonicoVox uses a **multi-process architecture** for robustness and isolation:

```
┌─────────────────────────────────────────────────────────┐
│                    sv_app.exe                           │
│              (Main Control Dashboard)                   │
│  ┌─────────────────────────────────────────────────┐   │
│  │  ImGui UI │ Settings │ Hotkeys │ Stats Display  │   │
│  └─────────────────────────────────────────────────┘   │
│                         │                               │
│                         │ IPC (Named Pipes)             │
└─────────────────────────┼───────────────────────────────┘
                          │
          ┌───────────────┴───────────────┐
          ▼                               ▼
┌──────────────────────┐       ┌──────────────────────┐
│  sv_overlay_ring.exe │       │sv_overlay_captions   │
│   (Ring Visualizer)  │       │        .exe          │
│                      │       │  (Live Captions)     │
│ ┌──────────────────┐ │       │ ┌──────────────────┐ │
│ │ WASAPI Capture   │ │       │ │ Whisper STT      │ │
│ │ FFT Analysis     │ │       │ │ DirectWrite Text │ │
│ │ D3D11 Renderer   │ │       │ │ D3D11 Renderer   │ │
│ └──────────────────┘ │       │ └──────────────────┘ │
└──────────────────────┘       └──────────────────────┘
   Topmost, Click-through        Topmost, Transparent
   Transparent Overlay            Caption Overlay
```

### Component Breakdown

#### Ring Overlay Process
- **`OverlayWindow`**: Creates borderless, layered, topmost window with DirectComposition
- **`AudioCapture`**: WASAPI loopback interface with channel layout parsing
- **`RingRenderer`**: D3D11 immediate-mode renderer with premultiplied alpha
- **`RingApp`**: Application controller coordinating components and message pump

#### Main App (Planned)
- **Event Bus**: In-process pub/sub for component communication
- **IPC Server**: Named pipe publisher for overlay events
- **Settings Manager**: JSON-based configuration with validation
- **UI Dashboard**: ImGui interface for overlay control

---

## Build Instructions

### Prerequisites

- **Windows 10/11** (version 1903 or later)
- **Visual Studio 2022** with C++ desktop development workload
- **CMake 3.21+** (included with VS 2022)
- **Python 3.8+** (for live subtitles feature)
- **vcpkg** (optional, for future dependencies)

### Build Steps

1. **Open Developer Command Prompt** (or PowerShell with VS tools):

   ```cmd
   # From Start Menu: "Developer Command Prompt for VS 2022"
   ```

2. **Configure the project**:

   ```cmd
   cd path\to\sonicovox
   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
   ```

3. **Build Release binary**:

   ```cmd
   cmake --build build --config Release
   ```

4. **Locate executable**:

   ```
   build\overlays\ring\Release\sv_overlay_ring.exe
   ```

### Build Outputs

- **Debug**: `build\overlays\ring\Debug\sv_overlay_ring.exe`
- **Release**: `build\overlays\ring\Release\sv_overlay_ring.exe`

### Setting Up Live Subtitles (Optional)

To enable the live subtitle feature:

1. **Install Python dependencies**:
   ```bash
   pip install vosk
   ```

2. **Download Vosk model** (~40MB):
   - Visit: https://alphacephei.com/vosk/models
   - Download: `vosk-model-small-en-us-0.15.zip`
   - Extract to: `models/vosk-model-small-en-us-0.15/`

3. **Verify structure**:
   ```
   sonicovox/
     models/
       vosk-model-small-en-us-0.15/
         am/
         conf/
         graph/
         ivector/
   ```

**Note:** The `models/` folder is excluded from git (large files). Each user must download the model separately.

---

## Usage

### Running the Ring Overlay

1. **Launch the overlay**:

   ```cmd
   build\overlays\ring\Release\sv_overlay_ring.exe
   ```

2. **What you'll see**:
   - A semi-transparent circular ring overlay appears centered on your primary monitor
   - The ring animates in real-time based on system audio output
   - The overlay is **click-through** by default—you can interact with windows beneath it

3. **Close the overlay**:
   - Press `Ctrl+Shift+V` (global hotkey works from any application)

### Visualizing Audio

The ring displays **64 directional spectrum bins** based on your audio channel layout:

- **Stereo**: Left at 330°, Right at 30°
- **5.1**: Front L/R, Center, Surround L/R, LFE (not visualized)
- **7.1+**: Full surround with side and rear channels

Louder sounds in a given direction produce brighter, taller sectors in the ring.

### System Requirements

- **OS**: Windows 10 1903+ or Windows 11
- **Audio**: Any WASAPI-compatible audio device
- **GPU**: DirectX 11 capable (hardware or WARP fallback)

---

## Roadmap

Development follows the milestone structure from the execution spec:

### Milestone 0: Scaffold & Overlay Control Dashboard (Current)
- [x] Ring overlay executable with WASAPI + D3D11 rendering
- [x] Global hotkey support
- [x] Click-through topmost window
- [ ] Main control app (ImGui dashboard)
- [ ] Settings persistence (JSON)
- [ ] IPC infrastructure (named pipes)
- [ ] Runtime overlay configuration

**Target**: End-to-end overlay lifecycle management via dashboard UI

### Milestone 1: Enhanced Ring Visualizer
- [ ] Per-channel RMS energy visualization
- [ ] Channel layout badge display
- [ ] Configurable ring size, opacity, position
- [ ] Multi-monitor support
- [ ] Event injection tool for testing

**Target**: Production-ready spatial audio visualization

### Milestone 2: Direction Analysis
- [ ] GCC-PHAT direction-of-arrival estimation
- [ ] Inner ring source dots with confidence halos
- [ ] EMA temporal smoothing with angle wrap-around
- [ ] Non-max suppression for discrete sources
- [ ] Onset-gated analysis to reduce music noise

**Target**: Identify discrete sound sources beyond channel layout

### Milestone 3: Live Captions
- [ ] Subtitle overlay process (`sv_overlay_captions.exe`)
- [ ] Whisper.cpp integration for speech-to-text
- [ ] VAD-gated audio segmentation
- [ ] Partial and final caption rendering
- [ ] DirectWrite text with font fallback

**Target**: Real-time speech transcription overlay

### Milestone 4: Stats & Analytics
- [ ] Event logging and KPI tracking
- [ ] CSV export with injection safeguards
- [ ] Latency histograms
- [ ] Caption accuracy metrics

**Target**: Performance monitoring and analysis

### Milestone 5: Settings & Hotkeys
- [ ] Comprehensive settings UI
- [ ] Global hotkey registration with collision detection
- [ ] Profile presets
- [ ] Validation and error recovery

**Target**: User-configurable experience

### Milestone 6: Visualizer Variants
- [ ] Radial waveform mode
- [ ] AI-enhanced ring (beta)
- [ ] Runtime mode switching

**Target**: Alternative visualization styles

### Milestone 7: Optional Add-ons
- [ ] Diarization interface
- [ ] Virtual surround architecture notes

**Target**: Advanced features for power users

### Milestone 8: Packaging & Distribution
- [ ] Portable ZIP distribution
- [ ] Signed MSI installer
- [ ] First-run model downloader
- [ ] SHA256 verification

**Target**: Production release

---

## Technical Details

### Window Composition

The ring overlay uses **layered windows with DirectComposition** for transparency:

```cpp
// Window creation with click-through and topmost flags
DWORD exStyle = WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TRANSPARENT;
CreateWindowExW(exStyle, className, title, WS_POPUP, ...);
SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
```

- **`WS_EX_TOPMOST`**: Always above normal windows
- **`WS_EX_LAYERED`**: Enables per-pixel alpha blending
- **`WS_EX_TOOLWINDOW`**: Hides from Alt+Tab and taskbar
- **`WS_EX_TRANSPARENT`**: Click-through (mouse events pass to windows below)

### Rendering Pipeline

1. **WASAPI Capture** (30 Hz event-driven, ~333ms buffer)
2. **Channel Energy Accumulation** (per-sample RMS)
3. **Spectrum Computation** (angle-based bin accumulation)
4. **Temporal Smoothing** (60% previous + 40% new)
5. **Spatial Interpolation** (64 bins → 128 render segments)
6. **D3D11 Rendering** (dynamic vertex buffer, premultiplied alpha)
7. **DWM Composition** (vsync'd present at 60 FPS)

### Audio Format Support

- **Sample rates**: 44.1 kHz, 48 kHz, 96 kHz (any WASAPI mix format)
- **Bit depths**: Float32, Int16
- **Channel layouts**: Mono, Stereo, 5.1, 7.1, 7.1.4 (Atmos), custom masks

### DPI Handling

Uses **Per-Monitor V2 DPI awareness**:

```cpp
SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
```

- Swap chain resized on `WM_DPICHANGED`
- Physical pixel calculations via `GetDpiForMonitor`

### Performance

- **CPU**: ~2-4% on modern processors (dedicated capture thread with MMCSS)
- **GPU**: <1% utilization (simple geometry, 128 triangles)
- **Memory**: ~15 MB working set

---

## Project Structure

```
/sonicovox
  /overlays
    /ring                      # Ring overlay executable
      /include/ring
        audio_capture.hpp      # WASAPI loopback interface
        overlay_window.hpp     # Transparent window management
        ring_app.hpp           # Application controller
        ring_renderer.hpp      # D3D11 rendering
      /src
        audio_capture.cpp      # Channel layout + FFT
        main.cpp               # Entry point
        overlay_window.cpp     # Window creation + DPI
        ring_app.cpp           # Message pump + lifecycle
        ring_renderer.cpp      # Vertex generation + shaders
      CMakeLists.txt           # Build configuration
  CMakeLists.txt               # Root build file
  README.md                    # This file
```

---

## Contributing

SonicoVox is currently in active development. Contributions welcome once Milestone 0 is complete.

### Development Guidelines

- **Code style**: Follow existing conventions (see `.clang-format` when added)
- **Modularity**: Keep components decoupled via interfaces
- **Testing**: Add unit tests for non-UI logic
- **Documentation**: Update README for user-facing changes

---

## License

TBD (To be determined)

---

## Acknowledgments

- **WASAPI**: Windows Core Audio APIs
- **Direct3D 11**: Microsoft DirectX SDK
- **DirectComposition**: Windows composition engine
- **Whisper.cpp** (planned): OpenAI Whisper inference

---

**Built with ❤️ for Windows audio enthusiasts, gamers, and accessibility advocates.**
