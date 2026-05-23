# SonicoVox

SonicoVox is a Windows tool that helps hearing-disabled gamers understand game audio more easily.

It adds transparent overlays on top of your screen so you can see where sounds are coming from and read live captions without tabbing out, changing your game, or sending audio to the cloud.

I started building this because I am hearing-disabled myself, and a lot of games assume players can reliably hear directional sound, voice lines, footsteps, or callouts. SonicoVox is my attempt to make that information more visible.

## Current Featureset

The current version includes a working ring overlay and live subtitle support.

### Audio ring overlay

SonicoVox captures system audio through WASAPI loopback and displays it as a transparent circular overlay. Louder sounds make parts of the ring react in real time, giving a quick visual sense of audio activity and direction.

Current overlay features:

- Transparent, click-through overlay
- Always-on-top window that does not steal focus
- Real-time audio visualization
- Stereo and surround layout support
- Adaptive gain for quieter sounds
- Smooth rendering with Direct3D 11 and DirectComposition
- Global close hotkey: `Ctrl + Shift + V`

### Live captions

SonicoVox can also display live subtitles using local speech recognition through Vosk.

Current caption features:

- Local speech-to-text processing
- Centered subtitles
- Partial result accumulation for smoother readability
- Text wrapping for longer lines
- No cloud transcription required

## Why local-first matters

SonicoVox is designed to run locally on your machine.

That means:

- No cloud dependency
- No telemetry
- No stored audio recordings
- No required account
- No admin privileges or kernel drivers

The goal is to make audio more accessible without turning the tool into a surveillance layer.

## Current status

SonicoVox is still in active development.

The ring overlay is currently the most complete part of the project. The next major step is building a proper control dashboard so users can turn features on or off, adjust overlay settings, and manage configuration without touching code.

Current executable:

```text
build/overlays/ring/Release/sv_overlay_ring.exe
