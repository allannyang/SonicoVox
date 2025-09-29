#include "ring/ring_app.hpp"

#include <shellscalingapi.h>
#include <windows.h>

using namespace sv::overlay::ring;

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE prevInstance, PWSTR cmdLine, int showCmd) {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) {
        MessageBoxW(nullptr, L"Failed to initialize COM for SonicoVox overlay.", L"SonicoVox", MB_ICONERROR | MB_OK);
        return static_cast<int>(hr);
    }

    if (!SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
        SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    }

    RingApp app;
    int result = app.run(instance, prevInstance, cmdLine, showCmd);

    CoUninitialize();
    return result;
}