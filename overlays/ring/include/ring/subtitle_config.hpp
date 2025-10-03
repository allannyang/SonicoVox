#pragma once

namespace sv::overlay::ring {

struct SubtitleConfig {
    // Feature toggles - easy to wire to menu later
    bool enabled = true;
    bool directionalHints = false;       // DISABLED: No arrows/shifting, keep centered
    bool adaptiveOpacity = true;         // Fade during intense audio

    // Visual settings
    float fontSize = 36.0f;              // Larger for visibility
    float bottomOffset = 0.15f;          // 15% from bottom of screen
    float maxWidthRatio = 0.7f;          // 70% of screen width
    float paddingX = 20.0f;
    float paddingY = 12.0f;

    // Colors (ARGB format)
    unsigned long backgroundColor = 0xB4000000;  // Semi-transparent black
    unsigned long textColor = 0xFFFFFFFF;        // White

    // Timing
    float fadeInMs = 150.0f;
    float fadeOutMs = 300.0f;
    float displayDurationMs = 3000.0f;   // How long to show final results

    // Behavior
    size_t maxLines = 2;
    size_t charsPerLine = 60;

    // Adaptive opacity settings (when enabled)
    float minOpacityDuringAction = 0.3f; // Fade to 30% during loud audio
    float opacityThreshold = 0.5f;       // Audio energy threshold to trigger fade
};

} // namespace sv::overlay::ring
