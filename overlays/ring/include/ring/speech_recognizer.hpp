#pragma once

#include <string>
#include <memory>

namespace sv::overlay::ring {

// Abstract interface for speech recognition engines
// Allows easy swapping between Vosk, Whisper, etc.
class ISpeechRecognizer {
public:
    virtual ~ISpeechRecognizer() = default;

    // Initialize the recognizer with model path
    virtual bool initialize(const char* modelPath) = 0;

    // Process audio samples (16-bit PCM, mono recommended)
    // Returns true if new results available
    virtual bool processAudio(const int16_t* samples, size_t sampleCount) = 0;

    // Get final transcript (complete utterance)
    virtual std::string getFinalTranscript() = 0;

    // Get partial transcript (real-time preview while speaking)
    virtual std::string getPartialTranscript() = 0;

    // Check if there's a new final result since last call
    virtual bool hasNewResult() = 0;

    // Reset the recognizer state
    virtual void reset() = 0;

    // Get last error message
    virtual const std::string& lastError() const = 0;
};

// Factory function for creating recognizers
enum class RecognizerType {
    Vosk,
    // Future: Whisper, Silero, etc.
};

std::unique_ptr<ISpeechRecognizer> createRecognizer(RecognizerType type);

} // namespace sv::overlay::ring
