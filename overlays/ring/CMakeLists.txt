cmake_minimum_required(VERSION 3.21)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SV_RING_SOURCES
    src/main.cpp
    src/ring_app.cpp
    src/overlay_window.cpp
    src/audio_capture.cpp
    src/ring_renderer.cpp
    src/speech_service.cpp
    src/subtitle_renderer.cpp
    src/speech_recognizer.cpp
    src/vosk_recognizer.cpp
)
add_executable(sv_overlay_ring WIN32 ${SV_RING_SOURCES})

target_include_directories(sv_overlay_ring PRIVATE include)

target_compile_definitions(sv_overlay_ring
    PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0A00
)

target_link_libraries(sv_overlay_ring
    PRIVATE
        d3d11
        dxgi
        d3dcompiler
        Shcore
        User32
        Ole32
        Avrt
        Dcomp
)

target_compile_options(sv_overlay_ring PRIVATE /W4 /permissive- /EHsc)

