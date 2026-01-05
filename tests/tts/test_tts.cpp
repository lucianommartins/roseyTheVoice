/**
 * test_tts.cpp - TTS Integration Tests
 */

#include <iostream>
#include <fstream>

// Include headers
#include "rtv/tts/TTSEngine.hpp"
#include "rtv/tts/TTSStreamer.hpp"

void testTTSEngine() {
    std::cout << "\n--- Test: TTSEngine (XTTS v2) ---\n";
    
    // XTTS requires a reference voice for synthesis
    std::string ref_audio = "models/tts/reference_voice.wav";
    std::ifstream f(ref_audio);
    
    if (!f.good()) {
        std::cout << "[SKIP] Reference voice not found: " << ref_audio << "\n";
        std::cout << "  Copy your voice sample: cp media/sample.wav models/tts/reference_voice.wav\n";
        return;
    }
    
    rtv::tts::TTSEngine engine("", ref_audio);
    
    if (!engine.isReady()) {
        std::cout << "[SKIP] XTTS not ready\n";
        std::cout << "  Install with: pip install TTS\n";
        std::cout << "  Setup with: ./scripts/download_models.sh tts\n";
        return;
    }
    
    std::cout << "Sample rate: " << engine.getSampleRate() << " Hz\n";
    
    std::string text = "Ola, eu sou a Rosey, sua assistente virtual.";
    std::cout << "Synthesizing: \"" << text << "\"\n";
    std::cout << "(This may take 10-15 seconds on CPU...)\n";
    
    auto audio = engine.synthesize(text);
    
    if (audio.empty()) {
        std::cout << "[FAIL] No audio generated\n";
        std::cout << "  Check if 'tts' command is available\n";
        return;
    }
    
    std::cout << "Generated " << audio.size() << " samples ("
              << (audio.size() / static_cast<float>(engine.getSampleRate())) 
              << " seconds)\n";
    
    // Save to WAV for manual verification
    std::string wav_file = "/tmp/rtv_tts_test.wav";
    std::ofstream wav(wav_file, std::ios::binary);
    
    if (wav.good()) {
        // Write WAV header
        int sample_rate = engine.getSampleRate();
        int num_samples = audio.size();
        int data_size = num_samples * 2;  // 16-bit
        int file_size = 36 + data_size;
        
        wav.write("RIFF", 4);
        wav.write(reinterpret_cast<char*>(&file_size), 4);
        wav.write("WAVE", 4);
        wav.write("fmt ", 4);
        int fmt_size = 16;
        wav.write(reinterpret_cast<char*>(&fmt_size), 4);
        short audio_format = 1;  // PCM
        wav.write(reinterpret_cast<char*>(&audio_format), 2);
        short channels = 1;
        wav.write(reinterpret_cast<char*>(&channels), 2);
        wav.write(reinterpret_cast<char*>(&sample_rate), 4);
        int byte_rate = sample_rate * 2;
        wav.write(reinterpret_cast<char*>(&byte_rate), 4);
        short block_align = 2;
        wav.write(reinterpret_cast<char*>(&block_align), 2);
        short bits_per_sample = 16;
        wav.write(reinterpret_cast<char*>(&bits_per_sample), 2);
        wav.write("data", 4);
        wav.write(reinterpret_cast<char*>(&data_size), 4);
        
        // Write samples
        for (float sample : audio) {
            int16_t s = static_cast<int16_t>(sample * 32767.0f);
            wav.write(reinterpret_cast<char*>(&s), 2);
        }
        
        std::cout << "Saved to: " << wav_file << "\n";
        std::cout << "Play with: aplay " << wav_file << "\n";
    }
    
    std::cout << "[PASS] TTSEngine works\n";
}

void testTTSWithVoiceCloning() {
    std::cout << "\n--- Test: Voice Cloning ---\n";
    
    std::string ref_audio = "models/tts/reference_voice.wav";
    std::ifstream f(ref_audio);
    
    if (!f.good()) {
        std::cout << "[SKIP] Reference voice not found: " << ref_audio << "\n";
        std::cout << "  Add a 6-30 second WAV file for voice cloning\n";
        return;
    }
    
    rtv::tts::TTSEngine engine("", ref_audio);
    
    if (!engine.isReady()) {
        std::cout << "[FAIL] Engine not ready\n";
        return;
    }
    
    std::string text = "Ola! Como posso ajudar voce hoje?";
    std::cout << "Synthesizing with cloned voice: \"" << text << "\"\n";
    
    auto audio = engine.synthesize(text);
    
    if (!audio.empty()) {
        std::cout << "[PASS] Voice cloning works (" << audio.size() << " samples)\n";
    } else {
        std::cout << "[FAIL] Voice cloning failed\n";
    }
}

int main() {
    std::cout << "=== TTS Integration Tests (XTTS v2) ===\n";
    std::cout << "Note: Requires Coqui TTS installed\n";
    std::cout << "  pip install TTS\n";
    std::cout << "  ./scripts/download_models.sh tts\n";
    
    testTTSEngine();
    testTTSWithVoiceCloning();
    
    std::cout << "\nTests complete!\n";
    return 0;
}
