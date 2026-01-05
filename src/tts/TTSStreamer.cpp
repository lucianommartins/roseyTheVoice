/**
 * TTSStreamer.cpp - Chunked TTS streaming
 */

#include <iostream>
#include <string>
#include <vector>
#include <functional>

namespace rtv::tts {

using AudioChunkCallback = std::function<void(const std::vector<float>&)>;

class TTSStreamer {
public:
    /**
     * Stream TTS output in chunks (sentence by sentence)
     */
    void streamText(const std::string& text, AudioChunkCallback on_chunk) {
        // TODO: Split by punctuation, synthesize chunks, call callback
        std::cout << "[TTSStreamer] Streaming: " << text.substr(0, 50) << "..." << std::endl;
    }
    
    void stop() {
        // TODO: Interrupt streaming (barge-in)
    }
};

} // namespace rtv::tts
