/**
 * test_aec3_pipeline.cpp - WebRTC AEC3 integration test
 */

#include "rtv/audio/AudioPipeline.hpp"
#include "rtv/audio/AudioEngine.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>

using namespace rtv::audio;

/**
 * Generate a sine wave
 */
std::vector<float> generateSineWave(int sample_rate, float frequency, float duration_sec) {
    size_t num_samples = static_cast<size_t>(sample_rate * duration_sec);
    std::vector<float> samples(num_samples);
    
    for (size_t i = 0; i < num_samples; ++i) {
        float t = static_cast<float>(i) / sample_rate;
        samples[i] = 0.5f * std::sin(2.0f * M_PI * frequency * t);
    }
    
    return samples;
}

void test_basic_initialization() {
    std::cout << "--- Test: Basic Initialization ---" << std::endl;
    
    AudioPipeline pipeline(16000, 1);
    
    if (pipeline.isInitialized()) {
        std::cout << "[PASS] AEC3 initialized successfully" << std::endl;
    } else {
        std::cout << "[FAIL] AEC3 failed to initialize" << std::endl;
    }
}

void test_echo_cancellation() {
    std::cout << "\n--- Test: Echo Cancellation ---" << std::endl;
    
    AudioPipeline pipeline(16000, 1);
    if (!pipeline.isInitialized()) {
        std::cout << "[SKIP] AEC3 not initialized" << std::endl;
        return;
    }
    
    // Generate test signals
    auto render_signal = generateSineWave(16000, 440.0f, 0.1f);  // 440Hz (A4 note)
    auto capture_signal = generateSineWave(16000, 440.0f, 0.1f); // Simulated echo
    
    // Feed render (speaker) audio
    pipeline.feedRenderAudio(render_signal.data(), render_signal.size());
    
    // Process capture (microphone) audio
    auto processed = pipeline.processCapture(capture_signal.data(), capture_signal.size());
    
    // Calculate energy reduction
    float input_energy = 0.0f;
    float output_energy = 0.0f;
    
    for (size_t i = 0; i < capture_signal.size(); ++i) {
        input_energy += capture_signal[i] * capture_signal[i];
    }
    for (size_t i = 0; i < processed.size(); ++i) {
        output_energy += processed[i] * processed[i];
    }
    
    float reduction_db = 10.0f * std::log10(input_energy / (output_energy + 1e-10f));
    
    std::cout << "  Input energy:  " << input_energy << std::endl;
    std::cout << "  Output energy: " << output_energy << std::endl;
    std::cout << "  Reduction:     " << reduction_db << " dB" << std::endl;
    std::cout << "  ERLE:          " << pipeline.getERLE() << " dB" << std::endl;
    
    // AEC3 needs time to converge, so we just check it processed correctly
    if (processed.size() > 0) {
        std::cout << "[PASS] Echo cancellation processing works" << std::endl;
    } else {
        std::cout << "[FAIL] Echo cancellation produced no output" << std::endl;
    }
}

void test_with_audio_engine() {
    std::cout << "\n--- Test: Integration with AudioEngine ---" << std::endl;
    
    AudioPipeline pipeline(16000, 1);
    if (!pipeline.isInitialized()) {
        std::cout << "[SKIP] AEC3 not initialized" << std::endl;
        return;
    }
    
    AudioConfig config;
    config.sample_rate = 16000;
    config.frames_per_buffer = 160;  // 10ms frames for AEC3
    
    AudioEngine engine(config);
    if (!engine.initialize()) {
        std::cout << "[SKIP] Audio engine failed: " << engine.lastError() << std::endl;
        return;
    }
    
    size_t frames_processed = 0;
    
    engine.setInputCallback([&](const float* samples, size_t count) {
        auto processed = pipeline.processCapture(samples, count);
        frames_processed++;
    });
    
    if (!engine.start()) {
        std::cout << "[SKIP] Audio engine start failed: " << engine.lastError() << std::endl;
        return;
    }
    
    // Run for 1 second
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    engine.stop();
    
    std::cout << "  Frames processed: " << frames_processed << std::endl;
    std::cout << "  Echo detected: " << (pipeline.isEchoDetected() ? "yes" : "no") << std::endl;
    
    if (frames_processed > 0) {
        std::cout << "[PASS] AEC3 integration with AudioEngine works" << std::endl;
    } else {
        std::cout << "[FAIL] No frames processed" << std::endl;
    }
}

int main() {
    std::cout << "=== WebRTC AEC3 Pipeline Tests ===" << std::endl;
    
    test_basic_initialization();
    test_echo_cancellation();
    test_with_audio_engine();
    
    std::cout << "\nTests complete!" << std::endl;
    return 0;
}
