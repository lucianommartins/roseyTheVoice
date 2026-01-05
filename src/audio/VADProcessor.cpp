/**
 * VADProcessor.cpp - Voice Activity Detection via libfvad
 * 
 * Detects speech segments and accumulates audio for STT processing.
 * Requires libfvad to be installed.
 */

#include "rtv/audio/VADProcessor.hpp"

#include <fvad.h>
#include <iostream>
#include <algorithm>
#include <cmath>

namespace rtv::audio {

struct VADProcessor::Impl {
    Fvad* vad = nullptr;
    
    int sample_rate;
    int frame_ms;
    int frame_samples;  // Samples per frame
    
    // Frame accumulation
    std::vector<float> frameBuffer;
    
    // Speech segment accumulation
    std::vector<float> speechBuffer;
    bool inSpeech = false;
    int silenceFrames = 0;
    
    // Configuration
    int silenceTimeoutMs = 500;
    int minSpeechDurationMs = 200;
    int silenceTimeoutFrames;
    int minSpeechFrames;
    
    SpeechCallback callback;
    
    void updateThresholds() {
        silenceTimeoutFrames = silenceTimeoutMs / frame_ms;
        minSpeechFrames = minSpeechDurationMs / frame_ms;
    }
};

VADProcessor::VADProcessor(int sample_rate, VADMode mode, int frame_ms)
    : pImpl_(std::make_unique<Impl>())
{
    pImpl_->sample_rate = sample_rate;
    pImpl_->frame_ms = frame_ms;
    pImpl_->frame_samples = (sample_rate * frame_ms) / 1000;
    
    pImpl_->frameBuffer.reserve(pImpl_->frame_samples);
    pImpl_->speechBuffer.reserve(sample_rate * 30);  // 30 seconds max
    
    pImpl_->updateThresholds();
    
    // Initialize libfvad
    pImpl_->vad = fvad_new();
    if (!pImpl_->vad) {
        std::cerr << "[VADProcessor] Failed to create fvad instance" << std::endl;
        return;
    }
    
    if (fvad_set_sample_rate(pImpl_->vad, sample_rate) < 0) {
        std::cerr << "[VADProcessor] Invalid sample rate: " << sample_rate << std::endl;
        fvad_free(pImpl_->vad);
        pImpl_->vad = nullptr;
        return;
    }
    
    if (fvad_set_mode(pImpl_->vad, static_cast<int>(mode)) < 0) {
        std::cerr << "[VADProcessor] Invalid mode" << std::endl;
    }
    
    std::cout << "[VADProcessor] Initialized (sample_rate=" << sample_rate 
              << "Hz, frame=" << frame_ms << "ms, mode=" << static_cast<int>(mode) << ")" 
              << std::endl;
}

VADProcessor::~VADProcessor() {
    if (pImpl_->vad) {
        fvad_free(pImpl_->vad);
    }
}

void VADProcessor::process(const float* samples, size_t count) {
    if (!pImpl_->vad) return;
    
    // Accumulate samples into frame buffer
    for (size_t i = 0; i < count; ++i) {
        pImpl_->frameBuffer.push_back(samples[i]);
        
        // Process complete frame
        if (pImpl_->frameBuffer.size() >= static_cast<size_t>(pImpl_->frame_samples)) {
            processFrame();
        }
    }
}

void VADProcessor::processFrame() {
    if (!pImpl_->vad) return;
    
    // Convert float to int16 for libfvad
    std::vector<int16_t> frame16(pImpl_->frame_samples);
    for (int i = 0; i < pImpl_->frame_samples; ++i) {
        float sample = std::clamp(pImpl_->frameBuffer[i], -1.0f, 1.0f);
        frame16[i] = static_cast<int16_t>(sample * 32767.0f);
    }
    
    // Run VAD
    int result = fvad_process(pImpl_->vad, frame16.data(), pImpl_->frame_samples);
    bool isSpeech = (result == 1);
    
    if (isSpeech) {
        // Add frame to speech buffer
        pImpl_->speechBuffer.insert(
            pImpl_->speechBuffer.end(),
            pImpl_->frameBuffer.begin(),
            pImpl_->frameBuffer.end()
        );
        
        pImpl_->inSpeech = true;
        pImpl_->silenceFrames = 0;
    } else if (pImpl_->inSpeech) {
        // Still add frame (might be brief pause)
        pImpl_->speechBuffer.insert(
            pImpl_->speechBuffer.end(),
            pImpl_->frameBuffer.begin(),
            pImpl_->frameBuffer.end()
        );
        
        pImpl_->silenceFrames++;
        
        // Check for end of speech
        if (pImpl_->silenceFrames >= pImpl_->silenceTimeoutFrames) {
            // Calculate speech duration
            int speechFrames = static_cast<int>(pImpl_->speechBuffer.size()) / pImpl_->frame_samples;
            
            // Trigger callback if long enough
            if (speechFrames >= pImpl_->minSpeechFrames && pImpl_->callback) {
                int duration_ms = static_cast<int>(pImpl_->speechBuffer.size()) * 1000 / pImpl_->sample_rate;
                pImpl_->callback(pImpl_->speechBuffer, duration_ms);
            }
            
            // Reset state
            pImpl_->speechBuffer.clear();
            pImpl_->inSpeech = false;
            pImpl_->silenceFrames = 0;
        }
    }
    
    pImpl_->frameBuffer.clear();
}

void VADProcessor::setSpeechCallback(SpeechCallback callback) {
    pImpl_->callback = std::move(callback);
}

void VADProcessor::setSilenceTimeout(int timeout_ms) {
    pImpl_->silenceTimeoutMs = timeout_ms;
    pImpl_->updateThresholds();
}

void VADProcessor::setMinSpeechDuration(int min_ms) {
    pImpl_->minSpeechDurationMs = min_ms;
    pImpl_->updateThresholds();
}

bool VADProcessor::isSpeaking() const {
    return pImpl_->inSpeech;
}

int VADProcessor::currentSpeechDuration() const {
    return static_cast<int>(pImpl_->speechBuffer.size()) * 1000 / pImpl_->sample_rate;
}

void VADProcessor::reset() {
    pImpl_->frameBuffer.clear();
    pImpl_->speechBuffer.clear();
    pImpl_->inSpeech = false;
    pImpl_->silenceFrames = 0;
    
    if (pImpl_->vad) {
        fvad_reset(pImpl_->vad);
    }
}

} // namespace rtv::audio
