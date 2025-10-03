# SonicoVox Settings Documentation

This document describes all configuration options available in `settings.json`.

## Table of Contents

- [File Location](#file-location)
- [Schema Validation](#schema-validation)
- [Configuration Sections](#configuration-sections)
  - [Overlays](#overlays)
  - [Audio](#audio)
  - [Hotkeys](#hotkeys)
  - [Speech-to-Text](#speech-to-text)
  - [Privacy](#privacy)
  - [UI](#ui)
  - [IPC](#ipc)
  - [Logging](#logging)

---

## File Location

**Default path**: `./config/settings.json`

The main application (`sv_app`) loads settings on startup and saves changes when modified through the UI. Overlay processes receive their configuration via IPC from the main app.

---

## Schema Validation

Settings are validated against `settings.schema.json`. Invalid values fall back to defaults. The schema enforces:

- Type constraints (number, string, boolean, object, array)
- Range validation (min/max for numeric values)
- Enum validation (allowed string values)
- Required fields

---

## Configuration Sections

### Overlays

Controls the appearance and behavior of screen overlays.

#### `overlays.ring`

**Ring visualizer overlay settings**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | boolean | `true` | Auto-start ring overlay when main app launches |
| `monitor` | integer | `0` | Monitor index (0 = primary, 1 = secondary, etc.) |
| `position.x` | number | `0.5` | Horizontal position (0.0 = left, 1.0 = right) |
| `position.y` | number | `0.5` | Vertical position (0.0 = top, 1.0 = bottom) |
| `scale` | number | `1.0` | Size multiplier (0.1–5.0) |
| `opacity` | number | `1.0` | Global opacity (0.0 = transparent, 1.0 = opaque) |
| `click_through` | boolean | `true` | Enable click-through mode (ignore mouse input) |

**Visual Appearance** (`overlays.ring.visual`)

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `base_radius_ratio` | number | `0.12` | Inner ring radius as ratio of min(screen width, height) |
| `ring_thickness_ratio` | number | `0.032` | Ring stroke thickness ratio |
| `amplitude_scale` | number | `0.085` | Audio amplitude response scaling |
| `color_scheme` | string | `"default"` | Color preset: `default`, `warm`, `cool`, `monochrome`, `rainbow`, `custom` |
| `colors.base_r` | number | `0.2` | Red channel (0.0–1.0) |
| `colors.base_g` | number | `0.4` | Green channel (0.0–1.0) |
| `colors.base_b` | number | `1.0` | Blue channel (0.0–1.0) |
| `colors.intensity_scale` | number | `0.5` | Color intensity scaling factor |

**Audio Processing** (`overlays.ring.audio`)

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `spectrum_bins` | integer | `64` | Number of directional spectrum bins (16–256) |
| `window_size` | integer | `2048` | FFT window size in samples (512–8192) |
| `hop_size` | integer | `512` | Analysis hop size in samples (128–4096) |
| `smoothing_factor` | number | `0.6` | Temporal smoothing (0.0 = none, 1.0 = max) |

#### `overlays.subtitles`

**Subtitle overlay settings** *(Planned - M3)*

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `enabled` | boolean | `false` | Auto-start subtitle overlay |
| `monitor` | integer | `0` | Monitor index |
| `anchor` | string | `"bottom_center"` | Anchor position (see enum below) |
| `max_lines` | integer | `2` | Maximum visible caption lines (1–10) |
| `font_size_pt` | integer | `24` | Font size in points (8–72) |
| `box_opacity` | number | `0.7` | Background box opacity |
| `margin_px` | integer | `32` | Distance from screen edge in pixels |
| `click_through` | boolean | `false` | Enable click-through mode |

**Anchor Positions**: `top_left`, `top_center`, `top_right`, `middle_left`, `middle_center`, `middle_right`, `bottom_left`, `bottom_center`, `bottom_right`

**Visual Settings** (`overlays.subtitles.visual`)

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `font_family` | string | `"Segoe UI"` | Font family name |
| `font_weight` | string | `"normal"` | Font weight: `light`, `normal`, `bold` |
| `text_color` | object | `{r:1, g:1, b:1, a:1}` | Text RGBA color |
| `background_color` | object | `{r:0, g:0, b:0, a:0.7}` | Background RGBA color |
| `outline_width` | integer | `2` | Text outline width in pixels |
| `shadow_offset` | integer | `2` | Text shadow offset in pixels |

---

### Audio

Global audio capture configuration.

#### `audio.capture`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `device_id` | string | `"default"` | Audio device ID or `"default"` for system default |
| `loopback_mode` | boolean | `true` | WASAPI loopback capture (system output) |
| `sample_rate` | integer | `48000` | Preferred sample rate (44100, 48000, or 96000 Hz) |
| `buffer_duration_ms` | integer | `333` | Capture buffer duration in milliseconds (100–1000) |

---

### Hotkeys

Global hotkey bindings registered via `RegisterHotKey`.

**Format**:

```json
"hotkey_name": {
  "modifiers": ["ctrl", "shift"],
  "key": "V",
  "enabled": true
}
```

**Default Hotkeys**:

| Name | Modifiers | Key | Enabled | Action |
|------|-----------|-----|---------|--------|
| `toggle_ring_overlay` | `Ctrl+Shift` | `V` | ✅ | Show/hide ring overlay |
| `toggle_subtitle_overlay` | `Ctrl+Shift` | `C` | ❌ | Show/hide subtitle overlay |
| `acknowledge_cue` | `Ctrl` | `Space` | ❌ | Acknowledge sound cue |
| `toggle_captions` | `Ctrl+Shift` | `T` | ❌ | Toggle caption recording |

**Modifiers**: `ctrl`, `alt`, `shift`, `win`

**Collision Handling**: If a hotkey fails to register (already in use), the main app will notify and offer rebinding.

---

### Speech-to-Text

Configuration for live caption generation.

#### `stt`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `engine` | string | `"whisper"` | STT engine: `whisper` or `none` |
| `model_path` | string | `"./models/ggml-base.en.bin"` | Path to Whisper model file |
| `language` | string | `"en"` | ISO 639-1 language code (e.g., `en`, `es`, `fr`) |
| `translate` | boolean | `false` | Translate to English |
| `vad_enabled` | boolean | `true` | Voice activity detection to gate processing |
| `vad_threshold` | number | `0.5` | VAD sensitivity (0.0–1.0) |

**Model Recommendations**:

- **Low latency**: `ggml-tiny.en.bin` (~75 MB)
- **Balanced**: `ggml-base.en.bin` (~140 MB)
- **High accuracy**: `ggml-small.en.bin` (~466 MB)

---

### Privacy

Data retention and telemetry controls.

#### `privacy`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `persist_audio` | boolean | `false` | Save captured audio to disk (disabled by default) |
| `persist_transcripts` | boolean | `false` | Save generated captions to disk (disabled by default) |
| `telemetry_enabled` | boolean | `false` | Anonymous usage telemetry (disabled by default) |

**Privacy Guarantees**:

- ✅ All processing is **local-first**
- ✅ No cloud services or network connections
- ✅ Audio is **never saved** unless explicitly enabled
- ✅ Transcripts are **ephemeral** unless explicitly enabled

---

### UI

Main application window settings.

#### `ui`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `theme` | string | `"dark"` | UI theme: `dark`, `light`, `system` |
| `font_size` | integer | `14` | UI font size in points (8–24) |
| `show_fps` | boolean | `false` | Display FPS counter |
| `show_stats` | boolean | `true` | Display stats panel |
| `window_position.x` | integer | `-1` | Window X position (-1 = auto) |
| `window_position.y` | integer | `-1` | Window Y position (-1 = auto) |
| `window_size.width` | integer | `800` | Window width in pixels (min 400) |
| `window_size.height` | integer | `600` | Window height in pixels (min 300) |

---

### IPC

Inter-process communication settings for app-to-overlay messaging.

#### `ipc`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `pipe_name_ring` | string | `\\\\.\\pipe\\sv_bus_ring` | Named pipe for ring overlay |
| `pipe_name_captions` | string | `\\\\.\\pipe\\sv_bus_captions` | Named pipe for caption overlay |
| `buffer_size` | integer | `8192` | Pipe buffer size in bytes (1024–65536) |
| `timeout_ms` | integer | `1000` | Pipe operation timeout in milliseconds |

**IPC Protocol**:

Messages use a binary framed format:

```
[Header: 12 bytes]
  - type: uint32 (little-endian)
  - version: uint32 (little-endian)
  - payload_size: uint32 (little-endian)
[Payload: variable length]
```

---

### Logging

Diagnostic logging configuration.

#### `logging`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `level` | string | `"info"` | Log level: `trace`, `debug`, `info`, `warning`, `error`, `critical` |
| `console_output` | boolean | `true` | Output logs to console (stdout/stderr) |
| `file_output` | boolean | `false` | Write logs to file |
| `log_directory` | string | `"./logs"` | Directory for log files |

**Log Levels**:

- **`trace`**: Verbose internal state
- **`debug`**: Detailed diagnostic info
- **`info`**: General informational messages (default)
- **`warning`**: Non-critical issues
- **`error`**: Recoverable errors
- **`critical`**: Fatal errors causing shutdown

---

## Example: Custom Ring Configuration

```json
{
  "overlays": {
    "ring": {
      "enabled": true,
      "monitor": 1,
      "position": {"x": 0.8, "y": 0.2},
      "scale": 1.5,
      "opacity": 0.85,
      "click_through": false,
      "visual": {
        "color_scheme": "custom",
        "colors": {
          "base_r": 1.0,
          "base_g": 0.2,
          "base_b": 0.2,
          "intensity_scale": 0.7
        }
      },
      "audio": {
        "spectrum_bins": 128,
        "smoothing_factor": 0.8
      }
    }
  }
}
```

This creates a **larger, semi-opaque red ring** in the **top-right of the secondary monitor** with **high temporal smoothing** and **128 bins** for finer angular resolution.

---

## Validation & Error Handling

**On startup**, the main app:

1. Loads `settings.json`
2. Validates against `settings.schema.json`
3. Falls back to defaults for invalid fields
4. Logs warnings for unrecognized keys

**On save**, the UI:

1. Validates all changes
2. Rejects invalid values before writing
3. Preserves unrecognized keys for forward compatibility

---

## Migration & Versioning

Settings use **semantic versioning** (`version` field). When the schema changes:

- **Patch** (1.0.0 → 1.0.1): Backward-compatible additions
- **Minor** (1.0.0 → 1.1.0): New optional fields
- **Major** (1.0.0 → 2.0.0): Breaking changes (auto-migration or reset)

The app performs **automatic migration** when possible and prompts for manual review when required.

---

## See Also

- [README.md](../README.md) - Project overview
- [settings.schema.json](./settings.schema.json) - JSON schema specification
- [settings.json](./settings.json) - Default configuration
