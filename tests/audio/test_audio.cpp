/**
 * test_audio.cpp - Audio system test
 * Tests AudioEngine initialization and device listing
 */

#include "rtv/audio/AudioEngine.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace rtv::audio;

int main() {
    std::cout << "=== RTV Audio System Test ===" << std::endl;
    
    // List available devices
    std::cout << "\n--- Input Devices ---" << std::endl;
    auto inputs = AudioEngine::listInputDevices();
    for (size_t i = 0; i < inputs.size(); ++i) {
        std::cout << "  [" << i << "] " << inputs[i] << std::endl;
    }
    
    std::cout << "\n--- Output Devices ---" << std::endl;
    auto outputs = AudioEngine::listOutputDevices();
    for (size_t i = 0; i < outputs.size(); ++i) {
        std::cout << "  [" << i << "] " << outputs[i] << std::endl;
    }
    
    // Initialize audio engine
    std::cout << "\n--- Initializing AudioEngine ---" << std::endl;
    AudioConfig config;
    config.sample_rate = 16000;
    config.frames_per_buffer = 512;
    
    AudioEngine engine(config);
    
    if (!engine.initialize()) {
        std::cerr << "Failed to initialize: " << engine.lastError() << std::endl;
        return 1;
    }
    
    // Set callback to count samples
    size_t totalSamples = 0;
    engine.setInputCallback([&totalSamples](const float* samples, size_t count) {
        totalSamples += count;
    });
    
    // Start audio
    std::cout << "\n--- Starting audio capture (3 seconds) ---" << std::endl;
    if (!engine.start()) {
        std::cerr << "Failed to start: " << engine.lastError() << std::endl;
        return 1;
    }
    
    // Run for 3 seconds
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    // Stop
    engine.stop();
    
    // Report
    float durationSec = static_cast<float>(totalSamples) / config.sample_rate;
    std::cout << "\n--- Results ---" << std::endl;
    std::cout << "  Samples captured: " << totalSamples << std::endl;
    std::cout << "  Duration: " << durationSec << " seconds" << std::endl;
    std::cout << "  Expected: ~3.0 seconds" << std::endl;
    
    bool success = (durationSec >= 2.5f && durationSec <= 3.5f);
    std::cout << "\n" << (success ? "[PASS]" : "[FAIL]") << " Audio capture test" << std::endl;
    
    return success ? 0 : 1;
}
