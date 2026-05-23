# SonicoVox

SonicoVox is a Windows tool that helps hearing-disabled gamers understand game audio more easily.

It adds transparent overlays on top of your screen so you can see where sounds are coming from and read live captions without tabbing out, changing your game, or sending audio to the cloud.

## What it does right now

The current version includes a working ring overlay and live subtitle support.

Audio ring overlay

SonicoVox captures system audio through WASAPI loopback and displays it as a transparent circular overlay. Louder sounds make parts of the ring react in real time, giving a quick visual sense of audio activity and direction.

Current overlay features:

* Transparent, click-through overlay
* Always-on-top window that does not steal focus
* Real-time audio visualization
* Stereo and surround layout support
* Adaptive gain for quieter sounds
* Smooth rendering with Direct3D 11 and DirectComposition
* Global close hotkey: Ctrl + Shift + V

Live captions

SonicoVox can also display live subtitles using local speech recognition through Vosk.

Current caption features:

* Local speech-to-text processing
* Centered subtitles
* Partial result accumulation for smoother readability
* Text wrapping for longer lines
* No cloud transcription required

Why local-first matters

SonicoVox is designed to run locally on your machine.

That means:

* No cloud dependency
* No telemetry
* No stored audio recordings
* No required account
* No admin privileges or kernel drivers

The goal is to make audio more accessible without turning the tool into a surveillance layer.

Current status

SonicoVox is still in active development.

The ring overlay is currently the most complete part of the project. The next major step is building a proper control dashboard so users can turn features on/off, adjust overlay settings, and manage configuration without touching code.

Current executable:

build/overlays/ring/Release/sv_overlay_ring.exe

Tech stack

SonicoVox uses:

* C++20
* Win32
* WASAPI loopback audio capture
* Direct3D 11
* DirectComposition
* CMake
* Python
* Vosk speech recognition

The project currently uses a multi-process architecture so overlays and speech recognition can run independently without taking down the entire app if one component stalls.

Build instructions

Requirements

* Windows 10/11
* Visual Studio 2022 with C++ desktop development tools
* CMake 3.21+
* Python 3.8+ for subtitles

Build

From a Developer Command Prompt or PowerShell with Visual Studio tools loaded:

cd path\to\sonicovox
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release

The release executable will be created at:

build\overlays\ring\Release\sv_overlay_ring.exe

Optional: enable live subtitles

Install Vosk:

pip install vosk

Download the small English Vosk model from the Vosk model page and extract it to:

models/vosk-model-small-en-us-0.15/

Expected structure:

sonicovox/
  models/
    vosk-model-small-en-us-0.15/
      am/
      conf/
      graph/
      ivector/

The models/ folder is excluded from git because the files are large.

Running SonicoVox

Launch the overlay executable:

build\overlays\ring\Release\sv_overlay_ring.exe

You should see a transparent circular overlay on your main monitor. It is click-through, so you can continue interacting with whatever is underneath it.

To close the overlay, press:

Ctrl + Shift + V

Roadmap

Planned next steps:

* Control dashboard for managing overlays
* Settings persistence
* Configurable overlay size, opacity, color, and position
* Multi-monitor support
* Improved direction-of-arrival analysis
* Dedicated caption overlay
* Whisper.cpp support
* Better packaging for non-developer users

Project structure

sonicovox/
  overlays/
    ring/
      include/ring/
      src/
      CMakeLists.txt
  CMakeLists.txt
  README.md

Contributing

SonicoVox is still early, but contributions are welcome once the first stable milestone is complete.

Areas that would be especially useful:

* Windows audio programming
* Overlay rendering
* Accessibility testing
* Speech recognition
* UI/UX for assistive tools
* Game accessibility feedback

License

TBD

Acknowledgments

SonicoVox builds on:

* Windows WASAPI
* Direct3D 11
* DirectComposition
* Vosk speech recognition

Built for gamers who should not have to miss information just because a game assumes everyone hears the same way.
