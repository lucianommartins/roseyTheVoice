/**
 * test_live_transcription.cpp - Live microphone transcription demo
 * 
 * Connects: AudioEngine → AEC3 → VAD → STT
 * Speak into the microphone and see real-time transcription!
 */

#include <iostream>
#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>

#include "rtv/audio/AudioEngine.hpp"
#include "rtv/audio/AudioPipeline.hpp"
#include "rtv/audio/VADProcessor.hpp"
#include "rtv/stt/STTEngine.hpp"

// Global flag for graceful shutdown
static std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running = false;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  RTV Live Transcription Demo" << std::endl;
    std::cout << "  Speak into the microphone!" << std::endl;
    std::cout << "  Press Ctrl+C to stop" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;
    
    // Setup signal handler
    std::signal(SIGINT, signal_handler);
    
    // Initialize STT Engine (load model once)
    std::cout << "[Init] Loading Whisper model..." << std::endl;
    rtv::stt::STTEngine stt("models/whisper/ggml-small-q5_1.bin", "pt", 8);
    
    if (!stt.isReady()) {
        std::cerr << "[Error] Could not load STT model!" << std::endl;
        std::cerr << "Run: ./scripts/download_models.sh whisper-small" << std::endl;
        return 1;
    }
    std::cout << "[Init] Model loaded: " << stt.getModelInfo() << std::endl;
    
    // Initialize Audio Pipeline (AEC3)
    std::cout << "[Init] Initializing AEC3..." << std::endl;
    rtv::audio::AudioPipeline pipeline(16000);
    
    // Initialize VAD Processor
    std::cout << "[Init] Initializing VAD..." << std::endl;
    rtv::audio::VADProcessor vad(16000, rtv::audio::VADMode::VeryAggressive, 20);
    vad.setSilenceTimeout(700);      // 700ms silence = end of speech
    vad.setMinSpeechDuration(300);   // Minimum 300ms to consider valid speech
    
    // Set up speech callback - transcribe when speech segment ends
    vad.setSpeechCallback([&stt](const std::vector<float>& samples, int duration_ms) {
        std::cout << "\n[VAD] Speech detected (" << duration_ms << "ms), transcribing..." << std::endl;
        
        auto start = std::chrono::high_resolution_clock::now();
        std::string text = stt.transcribe(samples);
        auto end = std::chrono::high_resolution_clock::now();
        
        auto inference_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        
        if (!text.empty()) {
            std::cout << "[STT] \"" << text << "\"" << std::endl;
            std::cout << "[STT] Inference time: " << inference_ms << "ms" << std::endl;
        } else {
            std::cout << "[STT] (no speech detected)" << std::endl;
        }
        std::cout << std::endl;
    });
    
    // Initialize Audio Engine
    std::cout << "[Init] Initializing AudioEngine..." << std::endl;
    rtv::audio::AudioConfig audio_config;
    audio_config.sample_rate = 16000;
    audio_config.frames_per_buffer = 320;  // 20ms frames
    rtv::audio::AudioEngine audio(audio_config);
    
    // Set up audio callback - process through AEC3 then VAD
    audio.setInputCallback([&pipeline, &vad](const float* samples, size_t count) {
        // Process through AEC3 (echo cancellation)
        std::vector<float> processed = pipeline.processCapture(samples, count);
        
        // Process through VAD (voice activity detection)
        vad.process(processed.data(), processed.size());
    });
    
    // Start audio capture
    std::cout << "[Init] Starting audio capture..." << std::endl;
    if (!audio.start()) {
        std::cerr << "[Error] Failed to start audio!" << std::endl;
        return 1;
    }
    
    std::cout << std::endl;
    std::cout << ">>> Listening... Speak now! <<<" << std::endl;
    std::cout << std::endl;
    
    // Main loop - just wait for Ctrl+C
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Show activity indicator when speaking
        if (vad.isSpeaking()) {
            std::cout << "\r[Recording...] " << vad.currentSpeechDuration() << "ms   " << std::flush;
        }
    }
    
    std::cout << std::endl;
    std::cout << "[Stopping] Shutting down..." << std::endl;
    
    audio.stop();
    
    std::cout << "[Done] Goodbye!" << std::endl;
    return 0;
}
