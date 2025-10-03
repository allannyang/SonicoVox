#pragma once

#include "ring/speech_recognizer.hpp"
#include <string>

// Forward declare Vosk types to avoid header dependency
struct VoskModel;
struct VoskRecognizer;

namespace sv::overlay::ring {

class VoskSpeechRecognizer : public ISpeechRecognizer {
public:
    VoskSpeechRecognizer();
    ~VoskSpeechRecognizer() override;

    bool initialize(const char* modelPath) override;
    bool processAudio(const int16_t* samples, size_t sampleCount) override;
    std::string getFinalTranscript() override;
    std::string getPartialTranscript() override;
    bool hasNewResult() override;
    void reset() override;
    const std::string& lastError() const override { return m_lastError; }

private:
    VoskModel* m_model = nullptr;
    VoskRecognizer* m_recognizer = nullptr;

    std::string m_finalText;
    std::string m_partialText;
    std::string m_lastError;
    bool m_hasNewResult = false;

    std::string parseJsonResult(const char* json);
};

} // namespace sv::overlay::ring
