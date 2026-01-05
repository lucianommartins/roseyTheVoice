/**
 * STTEngine.cpp - Whisper.cpp wrapper with preload
 */

#include <iostream>
#include <string>
#include <vector>
#include <memory>

// TODO: Include whisper.cpp when FetchContent is enabled
// #include "whisper.h"

namespace rtv::stt {

class STTEngine {
public:
    explicit STTEngine(const std::string& model_path = "models/whisper/ggml-medium-q5_0.bin")
        : model_path_(model_path)
        // , ctx_(whisper_init_from_file(model_path.c_str()))
    {
        // Preload model at startup - stays resident in RAM
        // params_ = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        // params_.language = "pt";
        // params_.n_threads = 8;
        // params_.print_progress = false;
        // params_.print_timestamps = false;
        
        std::cout << "[STTEngine] Model preloaded: " << model_path << std::endl;
    }
    
    ~STTEngine() {
        // if (ctx_) whisper_free(ctx_);
    }
    
    /**
     * Transcribe audio to text
     * @param audio Float samples at 16kHz mono
     * @return Transcribed text
     */
    std::string transcribe(const std::vector<float>& audio) {
        // whisper_full(ctx_, params_, audio.data(), audio.size());
        // 
        // std::string result;
        // for (int i = 0; i < whisper_full_n_segments(ctx_); ++i) {
        //     result += whisper_full_get_segment_text(ctx_, i);
        // }
        // return result;
        
        return "[STT placeholder - whisper not linked yet]";
    }
    
    /**
     * Get model info
     */
    std::string getModelInfo() const {
        return "whisper-medium-q5_0 (preloaded)";
    }

private:
    std::string model_path_;
    // whisper_context* ctx_ = nullptr;
    // whisper_full_params params_;
};

} // namespace rtv::stt
