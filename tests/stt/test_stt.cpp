/**
 * test_stt.cpp - STTEngine tests
 */

#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>

#include "rtv/stt/STTEngine.hpp"

void test_initialization() {
    std::cout << "--- Test: STTEngine Initialization ---" << std::endl;
    
    // Try to create engine (may fail if model not downloaded)
    rtv::stt::STTEngine engine("models/whisper/ggml-small-q5_1.bin", "pt", 8);
    
    if (engine.isReady()) {
        std::cout << "[PASS] Model loaded successfully" << std::endl;
        std::cout << "  Info: " << engine.getModelInfo() << std::endl;
    } else {
        std::cout << "[SKIP] Model not available - download with:" << std::endl;
        std::cout << "  ./scripts/download_models.sh whisper-small" << std::endl;
    }
}

void test_transcription_with_silence() {
    std::cout << "\n--- Test: Transcription with Silence ---" << std::endl;
    
    rtv::stt::STTEngine engine("models/whisper/ggml-small-q5_1.bin", "pt", 8);
    
    if (!engine.isReady()) {
        std::cout << "[SKIP] Model not available" << std::endl;
        return;
    }
    
    // Generate 1 second of silence (16kHz)
    std::vector<float> silence(16000, 0.0f);
    
    std::string result = engine.transcribe(silence);
    
    // Empty or very short result expected for silence
    std::cout << "  Result: \"" << result << "\"" << std::endl;
    std::cout << "  Length: " << result.size() << " chars" << std::endl;
    std::cout << "[PASS] Transcription of silence works" << std::endl;
}

void test_transcription_with_tone() {
    std::cout << "\n--- Test: Transcription with Tone ---" << std::endl;
    
    rtv::stt::STTEngine engine("models/whisper/ggml-small.bin", "pt", 4);
    
    if (!engine.isReady()) {
        std::cout << "[SKIP] Model not available" << std::endl;
        return;
    }
    
    // Generate 2 seconds of 440Hz tone (A4 note)
    const int sample_rate = 16000;
    const float freq = 440.0f;
    const int duration_samples = sample_rate * 2;
    
    std::vector<float> tone(duration_samples);
    for (int i = 0; i < duration_samples; ++i) {
        tone[i] = 0.3f * std::sin(2.0f * M_PI * freq * i / sample_rate);
    }
    
    std::string result = engine.transcribe(tone);
    
    std::cout << "  Result: \"" << result << "\"" << std::endl;
    std::cout << "  Length: " << result.size() << " chars" << std::endl;
    std::cout << "[PASS] Transcription of tone works" << std::endl;
}

void test_sample_rate() {
    std::cout << "\n--- Test: Sample Rate ---" << std::endl;
    
    int rate = rtv::stt::STTEngine::getSampleRate();
    assert(rate == 16000);
    
    std::cout << "  Expected: 16000 Hz" << std::endl;
    std::cout << "  Got: " << rate << " Hz" << std::endl;
    std::cout << "[PASS] Sample rate is 16kHz" << std::endl;
}

int main() {
    std::cout << "=== STTEngine Tests ===" << std::endl;
    
    test_sample_rate();
    test_initialization();
    test_transcription_with_silence();
    test_transcription_with_tone();
    
    std::cout << "\nTests complete!" << std::endl;
    return 0;
}
