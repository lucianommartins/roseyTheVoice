/**
 * STTEngine.cpp - Speech-to-Text Engine using whisper.cpp
 * 
 * Uses whisper.cpp for local, offline speech recognition.
 * Model is preloaded at startup and stays resident in RAM.
 */

#include "rtv/stt/STTEngine.hpp"

#include <iostream>
#include <string>
#include <vector>

#include "whisper.h"

namespace rtv::stt {

struct STTEngine::Impl {
    std::string model_path;
    std::string language;
    int n_threads;
    
    whisper_context* ctx = nullptr;
    whisper_full_params params;
    
    Impl(const std::string& path, const std::string& lang, int threads)
        : model_path(path), language(lang), n_threads(threads) {
        
        // Initialize whisper context from model file
        struct whisper_context_params cparams = whisper_context_default_params();
        ctx = whisper_init_from_file_with_params(model_path.c_str(), cparams);
        
        if (!ctx) {
            std::cerr << "[STTEngine] Failed to load model: " << model_path << std::endl;
            std::cerr << "[STTEngine] Download with: ./scripts/download_models.sh whisper-small" << std::endl;
            return;
        }
        
        // Configure params for greedy decoding (faster)
        params = whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
        params.language = language.c_str();
        params.n_threads = n_threads;
        params.print_progress = false;
        params.print_timestamps = false;
        params.print_realtime = false;
        params.print_special = false;
        params.translate = false;
        params.single_segment = false;
        params.no_context = true;
        
        std::cout << "[STTEngine] Model loaded: " << model_path << std::endl;
        std::cout << "[STTEngine] Language: " << language << ", Threads: " << n_threads << std::endl;
    }
    
    ~Impl() {
        if (ctx) {
            whisper_free(ctx);
            ctx = nullptr;
        }
    }
};

STTEngine::STTEngine(const std::string& model_path, const std::string& language, int n_threads)
    : impl_(std::make_unique<Impl>(model_path, language, n_threads)) {
}

STTEngine::~STTEngine() = default;

STTEngine::STTEngine(STTEngine&& other) noexcept = default;

std::string STTEngine::transcribe(const std::vector<float>& audio) {
    if (!impl_->ctx || audio.empty()) {
        return "";
    }
    
    int result = whisper_full(impl_->ctx, impl_->params, audio.data(), static_cast<int>(audio.size()));
    
    if (result != 0) {
        std::cerr << "[STTEngine] Transcription failed: " << result << std::endl;
        return "";
    }
    
    std::string text;
    const int n_segments = whisper_full_n_segments(impl_->ctx);
    
    for (int i = 0; i < n_segments; ++i) {
        const char* segment_text = whisper_full_get_segment_text(impl_->ctx, i);
        if (segment_text) {
            text += segment_text;
        }
    }
    
    return text;
}

bool STTEngine::isReady() const {
    return impl_ && impl_->ctx != nullptr;
}

std::string STTEngine::getModelInfo() const {
    if (!isReady()) {
        return "Model not loaded";
    }
    return "whisper (" + impl_->model_path + ")";
}

} // namespace rtv::stt
