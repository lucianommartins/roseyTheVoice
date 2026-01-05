/**
 * AudioEngine.cpp - PortAudio wrapper implementation
 * 
 * Provides low-latency audio capture and playback for the RTV voice assistant.
 * Uses PortAudio for cross-platform audio I/O.
 */

#include "rtv/audio/AudioEngine.hpp"
#include "rtv/audio/RingBuffer.hpp"

#include <portaudio.h>
#include <iostream>
#include <mutex>
#include <cstring>

namespace rtv::audio {

// Playback buffer size (samples)
constexpr size_t PLAYBACK_BUFFER_SIZE = 16000 * 10;  // 10 seconds at 16kHz

// Forward declare Impl for callbacks
struct AudioEngineImpl {
    PaStream* inputStream = nullptr;
    PaStream* outputStream = nullptr;
    
    AudioCallback userCallback;
    RingBuffer<float> playbackBuffer{PLAYBACK_BUFFER_SIZE};
    
    std::atomic<bool> running{false};
    std::atomic<bool> initialized{false};
    
    std::mutex callbackMutex;
    std::string lastError;
    
    AudioConfig config;
};

/**
 * PortAudio callback for input stream
 */
static int inputCallback(
    const void* input,
    void* output,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData
);

/**
 * PortAudio callback for output stream
 */
static int outputCallback(
    const void* input,
    void* output,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData
);

struct AudioEngine::Impl : public AudioEngineImpl {};

AudioEngine::AudioEngine(const AudioConfig& config)
    : pImpl_(std::make_unique<Impl>())
    , config_(config)
{
    pImpl_->config = config;
}

AudioEngine::~AudioEngine() {
    stop();
    
    if (pImpl_->initialized) {
        Pa_Terminate();
    }
}

bool AudioEngine::initialize() {
    if (pImpl_->initialized) {
        return true;
    }
    
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        pImpl_->lastError = std::string("Pa_Initialize failed: ") + Pa_GetErrorText(err);
        std::cerr << "[AudioEngine] " << pImpl_->lastError << std::endl;
        return false;
    }
    
    pImpl_->initialized = true;
    
    // Log available devices
    int numDevices = Pa_GetDeviceCount();
    std::cout << "[AudioEngine] Found " << numDevices << " audio devices" << std::endl;
    
    int defaultInput = Pa_GetDefaultInputDevice();
    int defaultOutput = Pa_GetDefaultOutputDevice();
    
    if (defaultInput >= 0) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(defaultInput);
        std::cout << "[AudioEngine] Default input: " << info->name << std::endl;
    }
    
    if (defaultOutput >= 0) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(defaultOutput);
        std::cout << "[AudioEngine] Default output: " << info->name << std::endl;
    }
    
    return true;
}

bool AudioEngine::start() {
    if (pImpl_->running) {
        return true;
    }
    
    if (!pImpl_->initialized && !initialize()) {
        return false;
    }
    
    PaError err;
    
    // Configure input stream
    PaStreamParameters inputParams;
    inputParams.device = (config_.input_device >= 0) 
        ? config_.input_device 
        : Pa_GetDefaultInputDevice();
    
    if (inputParams.device == paNoDevice) {
        pImpl_->lastError = "No input device available";
        std::cerr << "[AudioEngine] " << pImpl_->lastError << std::endl;
        return false;
    }
    
    inputParams.channelCount = config_.channels;
    inputParams.sampleFormat = paFloat32;
    inputParams.suggestedLatency = Pa_GetDeviceInfo(inputParams.device)->defaultLowInputLatency;
    inputParams.hostApiSpecificStreamInfo = nullptr;
    
    // Open input stream
    err = Pa_OpenStream(
        &pImpl_->inputStream,
        &inputParams,
        nullptr,  // No output for this stream
        config_.sample_rate,
        config_.frames_per_buffer,
        paClipOff,
        inputCallback,
        pImpl_.get()
    );
    
    if (err != paNoError) {
        pImpl_->lastError = std::string("Pa_OpenStream (input) failed: ") + Pa_GetErrorText(err);
        std::cerr << "[AudioEngine] " << pImpl_->lastError << std::endl;
        return false;
    }
    
    // Configure output stream
    PaStreamParameters outputParams;
    outputParams.device = (config_.output_device >= 0)
        ? config_.output_device
        : Pa_GetDefaultOutputDevice();
    
    if (outputParams.device == paNoDevice) {
        pImpl_->lastError = "No output device available";
        std::cerr << "[AudioEngine] " << pImpl_->lastError << std::endl;
        Pa_CloseStream(pImpl_->inputStream);
        pImpl_->inputStream = nullptr;
        return false;
    }
    
    outputParams.channelCount = config_.channels;
    outputParams.sampleFormat = paFloat32;
    outputParams.suggestedLatency = Pa_GetDeviceInfo(outputParams.device)->defaultLowOutputLatency;
    outputParams.hostApiSpecificStreamInfo = nullptr;
    
    // Open output stream
    err = Pa_OpenStream(
        &pImpl_->outputStream,
        nullptr,  // No input for this stream
        &outputParams,
        config_.sample_rate,
        config_.frames_per_buffer,
        paClipOff,
        outputCallback,
        pImpl_.get()
    );
    
    if (err != paNoError) {
        pImpl_->lastError = std::string("Pa_OpenStream (output) failed: ") + Pa_GetErrorText(err);
        std::cerr << "[AudioEngine] " << pImpl_->lastError << std::endl;
        Pa_CloseStream(pImpl_->inputStream);
        pImpl_->inputStream = nullptr;
        return false;
    }
    
    // Start streams
    err = Pa_StartStream(pImpl_->inputStream);
    if (err != paNoError) {
        pImpl_->lastError = std::string("Pa_StartStream (input) failed: ") + Pa_GetErrorText(err);
        std::cerr << "[AudioEngine] " << pImpl_->lastError << std::endl;
        Pa_CloseStream(pImpl_->inputStream);
        Pa_CloseStream(pImpl_->outputStream);
        pImpl_->inputStream = nullptr;
        pImpl_->outputStream = nullptr;
        return false;
    }
    
    err = Pa_StartStream(pImpl_->outputStream);
    if (err != paNoError) {
        pImpl_->lastError = std::string("Pa_StartStream (output) failed: ") + Pa_GetErrorText(err);
        std::cerr << "[AudioEngine] " << pImpl_->lastError << std::endl;
        Pa_StopStream(pImpl_->inputStream);
        Pa_CloseStream(pImpl_->inputStream);
        Pa_CloseStream(pImpl_->outputStream);
        pImpl_->inputStream = nullptr;
        pImpl_->outputStream = nullptr;
        return false;
    }
    
    pImpl_->running = true;
    std::cout << "[AudioEngine] Started (sample_rate=" << config_.sample_rate 
              << "Hz, buffer=" << config_.frames_per_buffer << " frames)" << std::endl;
    
    return true;
}

void AudioEngine::stop() {
    if (!pImpl_->running) {
        return;
    }
    
    pImpl_->running = false;
    
    if (pImpl_->inputStream) {
        Pa_StopStream(pImpl_->inputStream);
        Pa_CloseStream(pImpl_->inputStream);
        pImpl_->inputStream = nullptr;
    }
    
    if (pImpl_->outputStream) {
        Pa_StopStream(pImpl_->outputStream);
        Pa_CloseStream(pImpl_->outputStream);
        pImpl_->outputStream = nullptr;
    }
    
    std::cout << "[AudioEngine] Stopped" << std::endl;
}

bool AudioEngine::isRunning() const {
    return pImpl_->running;
}

void AudioEngine::setInputCallback(AudioCallback callback) {
    std::lock_guard<std::mutex> lock(pImpl_->callbackMutex);
    pImpl_->userCallback = std::move(callback);
}

void AudioEngine::queuePlayback(const float* samples, size_t count) {
    pImpl_->playbackBuffer.push(samples, count);
}

void AudioEngine::clearPlayback() {
    pImpl_->playbackBuffer.clear();
}

bool AudioEngine::isPlaying() const {
    return pImpl_->playbackBuffer.available() > 0;
}

std::vector<std::string> AudioEngine::listInputDevices() {
    std::vector<std::string> devices;
    
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        return devices;
    }
    
    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxInputChannels > 0) {
            devices.push_back(info->name);
        }
    }
    
    Pa_Terminate();
    return devices;
}

std::vector<std::string> AudioEngine::listOutputDevices() {
    std::vector<std::string> devices;
    
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        return devices;
    }
    
    int numDevices = Pa_GetDeviceCount();
    for (int i = 0; i < numDevices; ++i) {
        const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
        if (info && info->maxOutputChannels > 0) {
            devices.push_back(info->name);
        }
    }
    
    Pa_Terminate();
    return devices;
}

std::string AudioEngine::lastError() const {
    return pImpl_->lastError;
}

// ============================================================================
// PortAudio Callbacks
// ============================================================================

static int inputCallback(
    const void* input,
    void* output,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData
) {
    auto* impl = static_cast<AudioEngineImpl*>(userData);
    const float* samples = static_cast<const float*>(input);
    
    // Call user callback if set
    std::lock_guard<std::mutex> lock(impl->callbackMutex);
    if (impl->userCallback && samples) {
        impl->userCallback(samples, frameCount);
    }
    
    return paContinue;
}

static int outputCallback(
    const void* input,
    void* output,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void* userData
) {
    auto* impl = static_cast<AudioEngineImpl*>(userData);
    float* out = static_cast<float*>(output);
    
    // Read from playback buffer
    size_t read = impl->playbackBuffer.pop(out, frameCount);
    
    // Zero-fill if not enough data
    if (read < frameCount) {
        std::memset(out + read, 0, (frameCount - read) * sizeof(float));
    }
    
    return paContinue;
}

} // namespace rtv::audio
