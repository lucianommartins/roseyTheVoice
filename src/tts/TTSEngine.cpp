/**
 * TTSEngine.cpp - XTTS v2 wrapper for female voice synthesis
 * 
 * Uses Coqui XTTS v2 via Python subprocess for voice cloning.
 * Requires: pip install TTS
 */

#include "rtv/tts/TTSEngine.hpp"

#include <atomic>
#include <array>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <cstdlib>

namespace rtv::tts {

struct TTSEngine::Impl {
    std::string model_name;
    std::string reference_audio;
    int sample_rate = 24000;  // XTTS default
    float speed = 1.0f;
    std::atomic<bool> should_stop{false};
    bool ready = false;
    
    Impl(const std::string& model, const std::string& ref_audio)
        : model_name(model.empty() ? "tts_models/multilingual/multi-dataset/xtts_v2" : model)
        , reference_audio(ref_audio) {
        
        // Check if reference audio exists
        if (!reference_audio.empty()) {
            std::ifstream f(reference_audio);
            if (f.good()) {
                ready = true;
                std::cout << "[TTSEngine] XTTS ready with voice: " << reference_audio << std::endl;
            } else {
                std::cerr << "[TTSEngine] Reference audio not found: " << reference_audio << std::endl;
            }
        } else {
            // Use default XTTS without cloning
            ready = true;
            std::cout << "[TTSEngine] XTTS ready (default voice)" << std::endl;
        }
    }
    
    std::vector<float> synthesize(const std::string& text) {
        std::vector<float> audio;
        
        if (!ready || text.empty()) return audio;
        
        // Create temp files
        std::string temp_wav = "/tmp/rtv_tts_" + std::to_string(std::rand()) + ".wav";
        std::string temp_text = "/tmp/rtv_tts_text_" + std::to_string(std::rand()) + ".txt";
        
        // Write text to file (avoids shell escaping issues)
        {
            std::ofstream tf(temp_text);
            tf << text;
        }
        
        // Build XTTS command
        std::ostringstream cmd;
        cmd << "tts --model_name " << model_name
            << " --text \"$(cat " << temp_text << ")\""
            << " --language_idx pt"
            << " --out_path " << temp_wav;
        
        if (!reference_audio.empty()) {
            cmd << " --speaker_wav " << reference_audio;
        }
        
        cmd << " 2>/dev/null";
        
        int result = std::system(cmd.str().c_str());
        
        // Cleanup text file
        std::remove(temp_text.c_str());
        
        if (result != 0) {
            std::cerr << "[TTSEngine] XTTS failed with code: " << result << std::endl;
            std::remove(temp_wav.c_str());
            return audio;
        }
        
        // Read WAV file and convert to float samples
        audio = readWavToFloat(temp_wav);
        
        // Cleanup
        std::remove(temp_wav.c_str());
        
        return audio;
    }
    
    std::vector<float> readWavToFloat(const std::string& wav_path) {
        std::vector<float> samples;
        std::ifstream wav(wav_path, std::ios::binary);
        
        if (!wav.good()) return samples;
        
        // Skip WAV header (44 bytes for standard PCM)
        char header[44];
        wav.read(header, 44);
        
        // Extract sample rate from header
        sample_rate = *reinterpret_cast<int*>(header + 24);
        
        // Read 16-bit samples
        std::vector<int16_t> raw_samples;
        int16_t sample;
        while (wav.read(reinterpret_cast<char*>(&sample), 2)) {
            raw_samples.push_back(sample);
        }
        
        // Convert to float
        samples.reserve(raw_samples.size());
        for (int16_t s : raw_samples) {
            samples.push_back(static_cast<float>(s) / 32768.0f);
        }
        
        return samples;
    }
    
    void synthesizeStreaming(const std::string& text, TTSChunkCallback callback) {
        should_stop = false;
        
        // Split text by sentences
        auto sentences = splitSentences(text);
        
        for (const auto& sentence : sentences) {
            if (should_stop) break;
            
            auto audio = synthesize(sentence);
            if (!audio.empty()) {
                if (!callback(audio, sample_rate)) {
                    break;
                }
            }
        }
    }
    
    std::vector<std::string> splitSentences(const std::string& text) {
        std::vector<std::string> sentences;
        std::regex sentence_regex(R"([^.!?]+[.!?]+\s*)");
        
        auto begin = std::sregex_iterator(text.begin(), text.end(), sentence_regex);
        auto end = std::sregex_iterator();
        
        for (auto it = begin; it != end; ++it) {
            std::string sentence = it->str();
            if (!sentence.empty()) {
                sentences.push_back(sentence);
            }
        }
        
        if (sentences.empty() && !text.empty()) {
            sentences.push_back(text);
        }
        
        return sentences;
    }
};

TTSEngine::TTSEngine(const std::string& model_path, const std::string& config_path)
    : impl_(std::make_unique<Impl>(model_path, config_path)) {
}

TTSEngine::~TTSEngine() = default;

TTSEngine::TTSEngine(TTSEngine&&) noexcept = default;
TTSEngine& TTSEngine::operator=(TTSEngine&&) noexcept = default;

bool TTSEngine::isReady() const {
    return impl_->ready;
}

int TTSEngine::getSampleRate() const {
    return impl_->sample_rate;
}

std::vector<float> TTSEngine::synthesize(const std::string& text) {
    return impl_->synthesize(text);
}

void TTSEngine::synthesizeStreaming(const std::string& text, TTSChunkCallback callback) {
    impl_->synthesizeStreaming(text, std::move(callback));
}

void TTSEngine::setSpeed(float speed) {
    impl_->speed = speed;
}

void TTSEngine::stop() {
    impl_->should_stop = true;
}

} // namespace rtv::tts
