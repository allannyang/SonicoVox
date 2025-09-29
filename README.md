# SonicoVox Overlay Prototype

This repository currently contains the Direct3D 11 ring overlay executable (sv_overlay_ring).
The overlay renders a semi-transparent circular spectrum visualizer on top of the desktop while remaining click-through, and can be dismissed using a global hotkey.

## Build

1. Open a Developer Command Prompt with Visual Studio 2022 tools available, or run the commands within PowerShell after installing CMake and the MSVC toolchain.
2. Configure the project: cmake -S . -B build.
3. Compile the release binary: cmake --build build --config Release.

The resulting executable is created at uild/overlays/ring/Release/sv_overlay_ring.exe.

## Run

- Launch the overlay with the compiled executable. It spawns a borderless transparent window that spans the primary monitor.
- The overlay listens to the system loopback audio device (WASAPI shared mode) and animates 64 spectral sectors in real time.
- Global hotkey: Ctrl+Shift+V closes the overlay process. The window is click-through by default so underlying applications remain interactive.

## Notes

- The renderer uses premultiplied alpha swap-chain composition to avoid halo artifacts and maintains DPI awareness per monitor (V2).
- DirectComposition is used when available for the transparent overlay swap chain; if unavailable the process falls back to a non-alpha swap chain (reduced visual fidelity).
- Audio capture currently focuses on the default render endpoint; future milestones can extend this to configurable devices and IPC with the main dashboard.
- The FFT implementation is a straightforward DFT over a Hann window to keep dependencies minimal; performance tuning or a dedicated FFT library can be slotted in later via AudioCapture.
