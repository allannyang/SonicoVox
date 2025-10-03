#include "ring/speech_recognizer.hpp"
#include "ring/vosk_recognizer.hpp"

namespace sv::overlay::ring {

std::unique_ptr<ISpeechRecognizer> createRecognizer(RecognizerType type) {
    switch (type) {
    case RecognizerType::Vosk:
        return std::make_unique<VoskSpeechRecognizer>();
    default:
        return nullptr;
    }
}

} // namespace sv::overlay::ring
