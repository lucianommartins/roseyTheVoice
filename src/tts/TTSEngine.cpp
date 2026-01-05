/**
 * TTSEngine.cpp - Piper TTS wrapper
 */

#include <iostream>
#include <string>
#include <vector>

namespace rtv::tts {

class TTSEngine {
public:
    explicit TTSEngine(const std::string& voice_path = "models/piper/pt_BR-faber-medium.onnx") {
        // TODO: Load Piper model
        std::cout << "[TTSEngine] Piper loaded: " << voice_path << std::endl;
    }
    
    /**
     * Synthesize text to audio
     * @param text Text to speak
     * @return Audio samples (float, 22050Hz typical for Piper)
     */
    std::vector<float> synthesize(const std::string& text) {
        // TODO: Call Piper
        return {};
    }
};

} // namespace rtv::tts
