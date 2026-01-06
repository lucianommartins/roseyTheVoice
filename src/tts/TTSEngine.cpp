/**
 * TTSEngine.cpp - XTTS v2 wrapper using HTTP server
 * 
 * Connects to persistent Python XTTS server for fast inference.
 * Server keeps model and speaker embedding cached.
 */

#include "rtv/tts/TTSEngine.hpp"

#include <atomic>
#include <array>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <mutex>
#include <regex>
#include <sstream>
#include <cstdlib>
#include <thread>
#include <cstring>

// Simple HTTP client using system curl
namespace {

bool httpPost(const std::string& url, const std::string& json_body, 
              std::vector<uint8_t>& response) {
    std::string temp_file = "/tmp/rtv_tts_response_" + std::to_string(std::rand()) + ".wav";
    
    std::ostringstream cmd;
    cmd << "curl -s -X POST " << url
        << " -H 'Content-Type: application/json'"
        << " -d '" << json_body << "'"
        << " -o " << temp_file;
    
    int result = std::system(cmd.str().c_str());
    
    if (result != 0) {
        std::remove(temp_file.c_str());
        return false;
    }
    
    // Read response file
    std::ifstream file(temp_file, std::ios::binary | std::ios::ate);
    if (!file.good()) {
        std::remove(temp_file.c_str());
        return false;
    }
    
    size_t size = file.tellg();
    file.seekg(0, std::ios::beg);
    response.resize(size);
    file.read(reinterpret_cast<char*>(response.data()), size);
    
    std::remove(temp_file.c_str());
    return size > 44;  // WAV header is 44 bytes
}

bool httpGet(const std::string& url) {
    std::ostringstream cmd;
    cmd << "curl -s -o /dev/null -w '%{http_code}' " << url;
    
    FILE* pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) return false;
    
    char buffer[16];
    std::string result;
    while (fgets(buffer, sizeof(buffer), pipe)) {
        result += buffer;
    }
    pclose(pipe);
    
    return result.find("200") != std::string::npos;
}

} // anonymous namespace

namespace rtv::tts {

struct TTSEngine::Impl {
    std::string reference_audio;
    std::string server_url = "http://localhost:5050";
    int sample_rate = 24000;
    float speed = 1.0f;
    std::atomic<bool> should_stop{false};
    bool ready = false;
    bool server_available = false;
    
    Impl(const std::string& model, const std::string& ref_audio)
        : reference_audio(ref_audio) {
        
        // Check if reference audio exists
        if (!reference_audio.empty()) {
            std::ifstream f(reference_audio);
            if (f.good()) {
                ready = true;
                std::cout << "[TTSEngine] XTTS ready with voice: " << reference_audio << std::endl;
                
                // Check if server is running
                checkServer();
            } else {
                std::cerr << "[TTSEngine] Reference audio not found: " << reference_audio << std::endl;
            }
        } else {
            ready = true;
            std::cout << "[TTSEngine] XTTS ready (default voice)" << std::endl;
        }
    }
    
    void checkServer() {
        server_available = httpGet(server_url + "/health");
        if (server_available) {
            std::cout << "[TTSEngine] Connected to XTTS server at " << server_url << std::endl;
        } else {
            std::cout << "[TTSEngine] XTTS server not running. Start with:" << std::endl;
            std::cout << "  python3 scripts/xtts_server.py -r " << reference_audio << " --server" << std::endl;
            std::cout << "[TTSEngine] Falling back to CLI mode (slower)" << std::endl;
        }
    }
    
    std::vector<float> synthesize(const std::string& text) {
        if (!ready || text.empty()) return {};
        
        // Re-check if server is available (it might have been started after init)
        if (!server_available) {
            checkServer();
        }
        
        if (!server_available) {
            std::cerr << "[TTSEngine] Error: XTTS server not available!" << std::endl;
            return {};
        }
        
        return synthesizeViaServer(text);
    }
    
    std::vector<float> synthesizeViaServer(const std::string& text) {
        std::vector<float> audio;
        
        // Escape all special chars for JSON
        std::string escaped_text;
        for (char c : text) {
            if (c == '"') escaped_text += "\\\"";
            else if (c == '\\') escaped_text += "\\\\";
            else if (c == '\n') escaped_text += " ";  // Replace newline with space
            else if (c == '\r') continue;              // Skip carriage return
            else if (c == '\t') escaped_text += " ";  // Replace tab with space
            else if (c >= 0 && c < 32) continue;       // Skip other control chars
            else escaped_text += c;
        }
        
        std::string json = "{\"text\":\"" + escaped_text + "\"}";
        std::vector<uint8_t> response;
        
        if (!httpPost(server_url + "/synthesize", json, response)) {
            return audio;
        }
        
        // Parse WAV - find data chunk (header may be >44 bytes)
        if (response.size() < 44) return audio;
        
        // Verify RIFF header
        if (response[0] != 'R' || response[1] != 'I' || 
            response[2] != 'F' || response[3] != 'F') {
            std::cerr << "[TTS] Invalid WAV: no RIFF header" << std::endl;
            return audio;
        }
        
        // Read fmt chunk info
        uint16_t audio_format = *reinterpret_cast<uint16_t*>(&response[20]);
        uint16_t num_channels = *reinterpret_cast<uint16_t*>(&response[22]);
        sample_rate = *reinterpret_cast<int*>(&response[24]);
        uint16_t bits_per_sample = *reinterpret_cast<uint16_t*>(&response[34]);
        
        // Find "data" chunk
        size_t data_offset = 0;
        size_t data_size = 0;
        for (size_t i = 12; i < response.size() - 8; i++) {
            if (response[i] == 'd' && response[i+1] == 'a' && 
                response[i+2] == 't' && response[i+3] == 'a') {
                data_size = *reinterpret_cast<uint32_t*>(&response[i + 4]);
                data_offset = i + 8;
                break;
            }
        }
        
        if (data_offset == 0 || data_offset >= response.size()) {
            std::cerr << "[TTS] Invalid WAV: no data chunk" << std::endl;
            return audio;
        }
        
        // Handle different bit depths
        if (bits_per_sample == 16) {
            size_t num_samples = std::min(data_size / 2, (response.size() - data_offset) / 2);
            audio.reserve(num_samples);
            int16_t* samples = reinterpret_cast<int16_t*>(&response[data_offset]);
            for (size_t i = 0; i < num_samples; i++) {
                audio.push_back(static_cast<float>(samples[i]) / 32768.0f);
            }
        } else if (bits_per_sample == 32 && audio_format == 3) {
            // 32-bit float (IEEE)
            size_t num_samples = std::min(data_size / 4, (response.size() - data_offset) / 4);
            audio.reserve(num_samples);
            float* samples = reinterpret_cast<float*>(&response[data_offset]);
            for (size_t i = 0; i < num_samples; i++) {
                audio.push_back(samples[i]);
            }
        } else {
            std::cerr << "[TTS] Unsupported WAV format: " << bits_per_sample << " bits" << std::endl;
        }
        
        return audio;
    }
    
    std::vector<float> synthesizeFallback(const std::string& text) {
        std::vector<float> audio;
        
        std::string temp_wav = "/tmp/rtv_tts_" + std::to_string(std::rand()) + ".wav";
        std::string temp_text = "/tmp/rtv_tts_text_" + std::to_string(std::rand()) + ".txt";
        
        {
            std::ofstream tf(temp_text);
            tf << text;
        }
        
        std::ostringstream cmd;
        cmd << "tts --model_name tts_models/multilingual/multi-dataset/xtts_v2"
            << " --text \"$(cat " << temp_text << ")\""
            << " --language_idx pt"
            << " --out_path " << temp_wav;
        
        if (!reference_audio.empty()) {
            cmd << " --speaker_wav " << reference_audio;
        }
        
        cmd << " 2>/dev/null";
        
        int result = std::system(cmd.str().c_str());
        std::remove(temp_text.c_str());
        
        if (result != 0) {
            std::cerr << "[TTSEngine] TTS CLI failed with code: " << result << std::endl;
            std::remove(temp_wav.c_str());
            return audio;
        }
        
        audio = readWavToFloat(temp_wav);
        std::remove(temp_wav.c_str());
        
        return audio;
    }
    
    std::vector<float> readWavToFloat(const std::string& wav_path) {
        std::vector<float> samples;
        std::ifstream wav(wav_path, std::ios::binary);
        
        if (!wav.good()) return samples;
        
        char header[44];
        wav.read(header, 44);
        
        sample_rate = *reinterpret_cast<int*>(header + 24);
        
        std::vector<int16_t> raw_samples;
        int16_t sample;
        while (wav.read(reinterpret_cast<char*>(&sample), 2)) {
            raw_samples.push_back(sample);
        }
        
        samples.reserve(raw_samples.size());
        for (int16_t s : raw_samples) {
            samples.push_back(static_cast<float>(s) / 32768.0f);
        }
        
        return samples;
    }
    
    void synthesizeStreaming(const std::string& text, TTSChunkCallback callback) {
        should_stop = false;
        
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

bool TTSEngine::isReady() const { return impl_->ready; }
int TTSEngine::getSampleRate() const { return impl_->sample_rate; }

std::vector<float> TTSEngine::synthesize(const std::string& text) {
    return impl_->synthesize(text);
}

void TTSEngine::synthesizeStreaming(const std::string& text, TTSChunkCallback callback) {
    impl_->synthesizeStreaming(text, std::move(callback));
}

void TTSEngine::setSpeed(float speed) { impl_->speed = speed; }
void TTSEngine::stop() { impl_->should_stop = true; }

} // namespace rtv::tts
