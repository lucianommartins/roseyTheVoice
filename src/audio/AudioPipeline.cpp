/**
 * AudioPipeline.cpp - WebRTC AEC3 integration for echo cancellation
 * 
 * Enables barge-in by removing speaker echo from microphone input.
 */

#include "rtv/audio/AudioPipeline.hpp"

#include "audio_processing/aec3/echo_canceller3.h"
#include "audio_processing/audio_buffer.h"

#include <iostream>
#include <algorithm>
#include <cmath>

namespace rtv::audio {

// AEC3 processes in 10ms frames
constexpr int FRAME_MS = 10;

struct AudioPipeline::Impl {
    std::unique_ptr<webrtc::EchoCanceller3> aec3;
    std::unique_ptr<webrtc::AudioBuffer> render_buffer;
    std::unique_ptr<webrtc::AudioBuffer> capture_buffer;
    
    int sample_rate;
    int num_channels;
    int samples_per_frame;  // Samples per 10ms frame
    
    // Frame accumulation buffers
    std::vector<float> render_accumulator;
    std::vector<float> capture_accumulator;
    
    // Metrics
    float erle = 0.0f;
    bool echo_detected = false;
    bool initialized = false;
};

AudioPipeline::AudioPipeline(int sample_rate, int num_channels)
    : pImpl_(std::make_unique<Impl>())
    , sample_rate_(sample_rate)
    , num_channels_(num_channels)
{
    pImpl_->sample_rate = sample_rate;
    pImpl_->num_channels = num_channels;
    pImpl_->samples_per_frame = (sample_rate * FRAME_MS) / 1000;
    
    // Validate sample rate
    if (sample_rate != 16000 && sample_rate != 32000 && sample_rate != 48000) {
        std::cerr << "[AudioPipeline] Invalid sample rate: " << sample_rate 
                  << " (must be 16000, 32000, or 48000)" << std::endl;
        return;
    }
    
    // Create AEC3 config
    webrtc::EchoCanceller3Config config;
    
    try {
        // Create echo canceller
        pImpl_->aec3 = std::make_unique<webrtc::EchoCanceller3>(
            config,
            sample_rate,
            num_channels,  // render channels
            num_channels   // capture channels
        );
        
        // Create audio buffers
        pImpl_->render_buffer = std::make_unique<webrtc::AudioBuffer>(
            sample_rate,
            num_channels,
            sample_rate,
            num_channels,
            sample_rate,
            num_channels
        );
        
        pImpl_->capture_buffer = std::make_unique<webrtc::AudioBuffer>(
            sample_rate,
            num_channels,
            sample_rate,
            num_channels,
            sample_rate,
            num_channels
        );
        
        pImpl_->initialized = true;
        
        std::cout << "[AudioPipeline] AEC3 initialized (sample_rate=" << sample_rate 
                  << "Hz, frame=" << pImpl_->samples_per_frame << " samples)" << std::endl;
                  
    } catch (const std::exception& e) {
        std::cerr << "[AudioPipeline] AEC3 initialization failed: " << e.what() << std::endl;
    }
}

AudioPipeline::~AudioPipeline() = default;

bool AudioPipeline::isInitialized() const {
    return pImpl_->initialized;
}

void AudioPipeline::feedRenderAudio(const float* samples, size_t count) {
    if (!pImpl_->initialized) return;
    
    // Accumulate samples
    for (size_t i = 0; i < count; ++i) {
        pImpl_->render_accumulator.push_back(samples[i]);
    }
    
    // Process complete frames
    while (pImpl_->render_accumulator.size() >= static_cast<size_t>(pImpl_->samples_per_frame)) {
        // Copy to audio buffer
        float* const* buffer_data = pImpl_->render_buffer->channels_f();
        for (int i = 0; i < pImpl_->samples_per_frame; ++i) {
            buffer_data[0][i] = pImpl_->render_accumulator[i];
        }
        
        // Analyze render (reference) signal
        pImpl_->aec3->AnalyzeRender(pImpl_->render_buffer.get());
        
        // Remove processed samples
        pImpl_->render_accumulator.erase(
            pImpl_->render_accumulator.begin(),
            pImpl_->render_accumulator.begin() + pImpl_->samples_per_frame
        );
    }
}

std::vector<float> AudioPipeline::processCapture(const float* samples, size_t count) {
    if (!pImpl_->initialized) {
        // Return input unchanged if not initialized
        return std::vector<float>(samples, samples + count);
    }
    
    std::vector<float> output;
    output.reserve(count);
    
    // Accumulate samples
    for (size_t i = 0; i < count; ++i) {
        pImpl_->capture_accumulator.push_back(samples[i]);
    }
    
    // Process complete frames
    while (pImpl_->capture_accumulator.size() >= static_cast<size_t>(pImpl_->samples_per_frame)) {
        // Copy to audio buffer
        float* const* buffer_data = pImpl_->capture_buffer->channels_f();
        for (int i = 0; i < pImpl_->samples_per_frame; ++i) {
            buffer_data[0][i] = pImpl_->capture_accumulator[i];
        }
        
        // Analyze and process capture (microphone) signal
        pImpl_->aec3->AnalyzeCapture(pImpl_->capture_buffer.get());
        pImpl_->aec3->ProcessCapture(pImpl_->capture_buffer.get(), false);
        
        // Copy processed audio to output
        const float* const* processed = pImpl_->capture_buffer->channels_const_f();
        for (int i = 0; i < pImpl_->samples_per_frame; ++i) {
            output.push_back(processed[0][i]);
        }
        
        // Remove processed samples
        pImpl_->capture_accumulator.erase(
            pImpl_->capture_accumulator.begin(),
            pImpl_->capture_accumulator.begin() + pImpl_->samples_per_frame
        );
    }
    
    return output;
}

float AudioPipeline::getERLE() const {
    return pImpl_->erle;
}

bool AudioPipeline::isEchoDetected() const {
    return pImpl_->echo_detected;
}

void AudioPipeline::reset() {
    pImpl_->render_accumulator.clear();
    pImpl_->capture_accumulator.clear();
    pImpl_->erle = 0.0f;
    pImpl_->echo_detected = false;
    
    std::cout << "[AudioPipeline] Reset" << std::endl;
}

} // namespace rtv::audio
