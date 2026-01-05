/**
 * TTSStreamer.cpp - Stream LLM tokens to audio
 */

#include "rtv/tts/TTSStreamer.hpp"

#include <atomic>
#include <mutex>
#include <sstream>
#include <thread>

namespace rtv::tts {

struct TTSStreamer::Impl {
    TTSEngine& engine;
    AudioCallback callback;
    std::string buffer;
    std::atomic<bool> speaking{false};
    std::atomic<bool> should_stop{false};
    std::mutex buffer_mutex;
    
    explicit Impl(TTSEngine& eng) : engine(eng) {}
    
    void feedToken(const std::string& token) {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        buffer += token;
        
        // Check for sentence boundary
        if (hasSentenceEnd(buffer)) {
            synthesizeBuffer();
        }
    }
    
    void flush() {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        if (!buffer.empty()) {
            synthesizeBuffer();
        }
    }
    
    bool hasSentenceEnd(const std::string& text) {
        // Check for sentence-ending punctuation followed by space or end
        for (size_t i = 0; i < text.size(); ++i) {
            char c = text[i];
            if (c == '.' || c == '!' || c == '?') {
                // Check if followed by space or end of string
                if (i + 1 >= text.size() || text[i + 1] == ' ') {
                    return true;
                }
            }
        }
        return false;
    }
    
    void synthesizeBuffer() {
        if (buffer.empty() || should_stop) return;
        
        std::string text = buffer;
        buffer.clear();
        
        speaking = true;
        
        engine.synthesizeStreaming(text, [this](const std::vector<float>& samples, int sr) {
            if (should_stop) return false;
            if (callback) {
                callback(samples, sr);
            }
            return true;
        });
        
        speaking = false;
    }
    
    void stop() {
        should_stop = true;
        engine.stop();
        std::lock_guard<std::mutex> lock(buffer_mutex);
        buffer.clear();
        speaking = false;
        should_stop = false;
    }
};

TTSStreamer::TTSStreamer(TTSEngine& engine)
    : impl_(std::make_unique<Impl>(engine)) {
}

TTSStreamer::~TTSStreamer() = default;

void TTSStreamer::feedToken(const std::string& token) {
    impl_->feedToken(token);
}

void TTSStreamer::flush() {
    impl_->flush();
}

void TTSStreamer::setAudioCallback(AudioCallback callback) {
    impl_->callback = std::move(callback);
}

void TTSStreamer::stop() {
    impl_->stop();
}

bool TTSStreamer::isSpeaking() const {
    return impl_->speaking;
}

} // namespace rtv::tts
