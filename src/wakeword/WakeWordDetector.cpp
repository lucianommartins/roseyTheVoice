/**
 * WakeWordDetector.cpp - Porcupine wake word detection
 */

#include "rtv/wakeword/WakeWordDetector.hpp"

#include <iostream>
#include <cstring>
#include <vector>

// Porcupine C API
extern "C" {
#include "pv_porcupine.h"
}

namespace rtv::wakeword {

struct WakeWordDetector::Impl {
    pv_porcupine_t* porcupine = nullptr;
    WakeWordCallback callback;
    int frame_length = 512;
    bool ready = false;
    std::vector<int16_t> int16_buffer;
    
    Impl(const std::string& access_key,
         const std::string& model_path,
         const std::vector<std::string>& keyword_paths,
         const std::vector<float>& sensitivities) {
        
        if (keyword_paths.empty()) {
            std::cerr << "[WakeWord] No keyword paths provided" << std::endl;
            return;
        }
        
        // Prepare C-style arrays
        std::vector<const char*> kw_paths;
        for (const auto& p : keyword_paths) {
            kw_paths.push_back(p.c_str());
        }
        
        // Default sensitivity to 0.5 if not provided
        std::vector<float> sens = sensitivities;
        if (sens.empty()) {
            sens.resize(keyword_paths.size(), 0.5f);
        }
        
        pv_status_t status = pv_porcupine_init(
            access_key.c_str(),
            model_path.c_str(),
            "cpu",  // device
            static_cast<int32_t>(keyword_paths.size()),
            kw_paths.data(),
            sens.data(),
            &porcupine
        );
        
        if (status != PV_STATUS_SUCCESS) {
            std::cerr << "[WakeWord] Failed to initialize Porcupine: " 
                      << pv_status_to_string(status) << std::endl;
            porcupine = nullptr;
            return;
        }
        
        frame_length = pv_porcupine_frame_length();
        int16_buffer.resize(frame_length);
        ready = true;
        
        std::cout << "[WakeWord] Porcupine initialized (version: " 
                  << pv_porcupine_version() 
                  << ", frame_length: " << frame_length << ")" << std::endl;
    }
    
    ~Impl() {
        if (porcupine) {
            pv_porcupine_delete(porcupine);
        }
    }
    
    int process(const int16_t* samples) {
        if (!ready || !porcupine) return -1;
        
        int32_t keyword_index = -1;
        pv_status_t status = pv_porcupine_process(porcupine, samples, &keyword_index);
        
        if (status != PV_STATUS_SUCCESS) {
            std::cerr << "[WakeWord] Process error: " << pv_status_to_string(status) << std::endl;
            return -1;
        }
        
        if (keyword_index >= 0 && callback) {
            callback(keyword_index);
        }
        
        return keyword_index;
    }
    
    int processFloat(const float* samples, size_t count) {
        if (!ready || !porcupine) return -1;
        
        // Accumulate samples until we have a full frame
        static std::vector<int16_t> accumulator;
        
        // Convert float to int16
        for (size_t i = 0; i < count; i++) {
            float sample = samples[i];
            if (sample > 1.0f) sample = 1.0f;
            if (sample < -1.0f) sample = -1.0f;
            accumulator.push_back(static_cast<int16_t>(sample * 32767.0f));
        }
        
        int result = -1;
        
        // Process full frames
        while (accumulator.size() >= static_cast<size_t>(frame_length)) {
            int idx = process(accumulator.data());
            if (idx >= 0) {
                result = idx;  // Return last detection
            }
            accumulator.erase(accumulator.begin(), accumulator.begin() + frame_length);
        }
        
        return result;
    }
};

WakeWordDetector::WakeWordDetector(
    const std::string& access_key,
    const std::string& model_path,
    const std::vector<std::string>& keyword_paths,
    const std::vector<float>& sensitivities
) : impl_(std::make_unique<Impl>(access_key, model_path, keyword_paths, sensitivities)) {
}

WakeWordDetector::~WakeWordDetector() = default;

bool WakeWordDetector::isReady() const {
    return impl_->ready;
}

int WakeWordDetector::getFrameLength() const {
    return impl_->frame_length;
}

int WakeWordDetector::getSampleRate() const {
    return 16000;  // Porcupine always uses 16kHz
}

int WakeWordDetector::process(const int16_t* samples) {
    return impl_->process(samples);
}

int WakeWordDetector::processFloat(const float* samples, size_t count) {
    return impl_->processFloat(samples, count);
}

void WakeWordDetector::setCallback(WakeWordCallback callback) {
    impl_->callback = std::move(callback);
}

std::string WakeWordDetector::getVersion() {
    return pv_porcupine_version();
}

} // namespace rtv::wakeword
