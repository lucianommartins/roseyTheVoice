/**
 * TTSStreamer.cpp - Parallel pipeline for streaming LLM tokens to audio
 * 
 * Synthesizes next sentence in background while current one plays.
 */

#include "rtv/tts/TTSStreamer.hpp"

#include <atomic>
#include <cctype>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <thread>
#include <vector>

namespace rtv::tts {

struct TTSStreamer::Impl {
    TTSEngine& engine;
    AudioCallback callback;
    std::string buffer;
    std::atomic<bool> speaking{false};
    std::atomic<bool> should_stop{false};
    std::mutex buffer_mutex;
    
    // Pipeline components
    std::queue<std::string> sentence_queue;
    std::queue<std::vector<float>> audio_queue;
    std::mutex queue_mutex;
    std::condition_variable sentence_cv;
    std::condition_variable audio_cv;
    std::thread synth_thread;
    std::atomic<bool> synth_running{false};
    std::atomic<bool> synth_in_progress{false};  // True while synthesizing a sentence
    int current_sample_rate = 24000;
    
    explicit Impl(TTSEngine& eng) : engine(eng) {}
    
    ~Impl() {
        stopSynthThread();
    }
    
    void startSynthThread() {
        if (synth_running) return;
        synth_running = true;
        synth_thread = std::thread([this]() { synthWorker(); });
    }
    
    void stopSynthThread() {
        if (!synth_running) return;
        synth_running = false;
        sentence_cv.notify_all();
        audio_cv.notify_all();
        if (synth_thread.joinable()) {
            synth_thread.join();
        }
    }
    
    // Worker thread: takes sentences, synthesizes, enqueues audio
    void synthWorker() {
        while (synth_running && !should_stop) {
            std::string sentence;
            
            // Wait for a sentence
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                sentence_cv.wait(lock, [this]() {
                    return !sentence_queue.empty() || !synth_running || should_stop;
                });
                
                if (!synth_running || should_stop) break;
                if (sentence_queue.empty()) continue;
                
                sentence = std::move(sentence_queue.front());
                sentence_queue.pop();
                synth_in_progress = true;  // Mark that we're synthesizing
            }
            
            // Synthesize (this is the slow part)
            auto audio = engine.synthesize(sentence);
            
            synth_in_progress = false;  // Done synthesizing this sentence
            
            if (!audio.empty() && !should_stop) {
                // Enqueue audio for playback
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    audio_queue.push(std::move(audio));
                }
                audio_cv.notify_one();
            }
        }
    }
    
    void feedToken(const std::string& token) {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        buffer += token;
        
        // Check for sentence boundary
        if (hasSentenceEnd(buffer)) {
            queueSentence();
        }
    }
    
    void queueSentence() {
        if (buffer.empty()) return;
        
        std::string text = buffer;
        buffer.clear();
        
        // Start synth thread if not running
        startSynthThread();
        
        // Queue for synthesis
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            sentence_queue.push(std::move(text));
        }
        sentence_cv.notify_one();
    }
    
    void flush() {
        // Queue any remaining text
        {
            std::lock_guard<std::mutex> lock(buffer_mutex);
            if (!buffer.empty()) {
                queueSentence();
            }
        }
        
        std::cout << "[TTSStreamer] flush() - waiting for synthesis..." << std::endl;
        
        // Wait for all synthesis to complete and play all audio
        speaking = true;
        
        while (!should_stop) {
            std::vector<float> audio;
            
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                
                // Wait for audio or check if done
                audio_cv.wait_for(lock, std::chrono::milliseconds(100), [this]() {
                    return !audio_queue.empty() || should_stop || 
                           (sentence_queue.empty() && audio_queue.empty() && !synth_in_progress);
                });
                
                if (should_stop) break;
                
                if (audio_queue.empty()) {
                    // Check if synthesis is complete (no sentences waiting AND nothing being synthesized)
                    if (sentence_queue.empty() && !synth_in_progress) {
                        std::cout << "[TTSStreamer] flush() - all sentences processed" << std::endl;
                        break;  // All done
                    }
                    continue;  // Still synthesizing
                }
                
                audio = std::move(audio_queue.front());
                audio_queue.pop();
            }
            
            // Play audio (blocking until queued)
            if (!audio.empty() && callback) {
                std::cout << "[TTSStreamer] flush() - playing " << audio.size() << " samples" << std::endl;
                callback(audio, current_sample_rate);
            }
        }
        
        std::cout << "[TTSStreamer] flush() - done" << std::endl;
        speaking = false;
    }
    
    bool hasSentenceEnd(const std::string& text) {
        // Only trigger on "punctuation + space" pattern
        // The flush() will handle any remaining text at the end
        for (size_t i = 0; i + 1 < text.size(); ++i) {
            char c = text[i];
            if ((c == '.' || c == '!' || c == '?') && text[i + 1] == ' ') {
                return true;
            }
        }
        return false;
    }
    
    void stop() {
        should_stop = true;
        engine.stop();
        
        // Clear queues
        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            while (!sentence_queue.empty()) sentence_queue.pop();
            while (!audio_queue.empty()) audio_queue.pop();
        }
        
        sentence_cv.notify_all();
        audio_cv.notify_all();
        
        {
            std::lock_guard<std::mutex> lock(buffer_mutex);
            buffer.clear();
        }
        
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
