/**
 * Orchestrator.cpp - Main conversation loop controller
 * 
 * Connects: AudioEngine → VAD → STT → LLM → TTS → AudioEngine
 */

#include "rtv/Orchestrator.hpp"
#include "rtv/audio/AudioEngine.hpp"
#include "rtv/audio/VADProcessor.hpp"
#include "rtv/stt/STTEngine.hpp"
#include "rtv/llm/ConversationEngine.hpp"
#include "rtv/tts/TTSEngine.hpp"
#include "rtv/tts/TTSStreamer.hpp"

#ifdef RTV_HAS_PORCUPINE
#include "rtv/wakeword/WakeWordDetector.hpp"
#endif

#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <queue>
#include <random>
#include <thread>

namespace rtv {

struct Orchestrator::Impl {
    // Components
    std::unique_ptr<audio::AudioEngine> audio;
    std::unique_ptr<audio::VADProcessor> vad;
    std::unique_ptr<stt::STTEngine> stt;
    std::unique_ptr<llm::ConversationEngine> llm;
    std::unique_ptr<tts::TTSEngine> tts;
    std::unique_ptr<tts::TTSStreamer> tts_streamer;
    
#ifdef RTV_HAS_PORCUPINE
    std::unique_ptr<wakeword::WakeWordDetector> wakeword;
#endif
    
    // State
    std::atomic<bool> running{false};
    std::atomic<bool> interrupted{false};
    std::atomic<OrchestratorState> state{OrchestratorState::SLEEPING};
    std::thread worker_thread;
    
    // Audio buffer for recording
    std::vector<float> audio_buffer;
    std::mutex buffer_mutex;
    bool speech_active = false;
    
    // Wake word state
    bool awaiting_command = false;  // True after wake word, waiting for user command
    std::chrono::steady_clock::time_point idle_start_time;
    std::chrono::steady_clock::time_point speaking_start_time;
    
    // Callbacks
    OrchestratorCallbacks callbacks;
    
    // Config
    std::string whisper_model = "models/whisper/ggml-small-q5_1.bin";
    std::string tts_ref_voice = "models/tts/reference_voice.wav";
    std::string llm_url = "http://localhost:8080";
    std::string porcupine_key_file = ".porcupine_key";
    std::string porcupine_model = "external/porcupine/lib/common/porcupine_params.pv";
    std::vector<std::string> wakeword_models = {"models/wakeword/hi_gemma.ppn"};
    
    // Pre-recorded greeting WAV for instant response
    std::string greeting_wav = "models/greetings/greeting_1.wav";
    std::vector<float> cached_greeting;  // Loaded on init
    
    // Notification sounds (optional)
    std::string wake_sound_wav = "models/sounds/wake.wav";
    std::string sleep_sound_wav = "models/sounds/sleep.wav";
    std::vector<float> cached_wake_sound;
    std::vector<float> cached_sleep_sound;
    
    void setState(OrchestratorState new_state) {
        state = new_state;
        if (callbacks.onStateChange) {
            callbacks.onStateChange(new_state);
        }
    }
    
    // Helper to load WAV file into float vector (resampled to 24000Hz)
    std::vector<float> loadWavFile(const std::string& path) {
        std::vector<float> samples;
        std::ifstream wav_file(path, std::ios::binary);
        if (!wav_file.good()) return samples;
        
        wav_file.seekg(0, std::ios::end);
        size_t file_size = wav_file.tellg();
        wav_file.seekg(0, std::ios::beg);
        
        if (file_size <= 44) return samples;
        
        std::vector<uint8_t> wav_data(file_size);
        wav_file.read(reinterpret_cast<char*>(wav_data.data()), file_size);
        
        // Read WAV header
        uint32_t sample_rate = *reinterpret_cast<uint32_t*>(&wav_data[24]);
        uint16_t bits = *reinterpret_cast<uint16_t*>(&wav_data[34]);
        size_t data_offset = 44;
        
        // Find data chunk
        for (size_t i = 12; i < file_size - 8; i++) {
            if (wav_data[i] == 'd' && wav_data[i+1] == 'a' && 
                wav_data[i+2] == 't' && wav_data[i+3] == 'a') {
                data_offset = i + 8;
                break;
            }
        }
        
        if (bits == 32) {
            size_t num_samples = (file_size - data_offset) / 4;
            samples.resize(num_samples);
            float* src = reinterpret_cast<float*>(&wav_data[data_offset]);
            std::copy(src, src + num_samples, samples.begin());
        } else if (bits == 24) {
            // 24-bit audio: 3 bytes per sample
            size_t num_samples = (file_size - data_offset) / 3;
            samples.resize(num_samples);
            const float gain = 4.0f;  // Boost volume for notification sounds
            for (size_t i = 0; i < num_samples; i++) {
                // Read 3 bytes, sign-extend to 32-bit
                uint8_t* ptr = &wav_data[data_offset + i * 3];
                int32_t val = (ptr[0] << 8) | (ptr[1] << 16) | (ptr[2] << 24);
                val >>= 8;  // Sign-extend
                float sample = (static_cast<float>(val) / 8388608.0f) * gain;
                // Clamp to avoid clipping
                samples[i] = std::max(-1.0f, std::min(1.0f, sample));
            }
        } else if (bits == 16) {
            size_t num_samples = (file_size - data_offset) / 2;
            samples.resize(num_samples);
            int16_t* src = reinterpret_cast<int16_t*>(&wav_data[data_offset]);
            for (size_t i = 0; i < num_samples; i++) {
                samples[i] = static_cast<float>(src[i]) / 32768.0f;
            }
        } else {
            std::cerr << "[WAV] Unsupported bits per sample: " << bits << " in " << path << std::endl;
        }
        
        // Resample to 24000Hz if needed
        const uint32_t target_rate = 24000;
        if (!samples.empty() && sample_rate != target_rate) {
            std::cout << "[WAV] Resampling from " << sample_rate << "Hz to " << target_rate << "Hz" << std::endl;
            
            double ratio = static_cast<double>(target_rate) / sample_rate;
            size_t new_size = static_cast<size_t>(samples.size() * ratio);
            std::vector<float> resampled(new_size);
            
            for (size_t i = 0; i < new_size; i++) {
                double src_pos = i / ratio;
                size_t idx = static_cast<size_t>(src_pos);
                double frac = src_pos - idx;
                
                if (idx + 1 < samples.size()) {
                    // Linear interpolation
                    resampled[i] = samples[idx] * (1.0f - frac) + samples[idx + 1] * frac;
                } else if (idx < samples.size()) {
                    resampled[i] = samples[idx];
                }
            }
            
            samples = std::move(resampled);
        }
        
        return samples;
    }
    
    bool initialize() {
        std::cout << "[Orchestrator] Initializing components..." << std::endl;
        
        // Audio Engine
        audio = std::make_unique<audio::AudioEngine>();
        if (!audio->initialize()) {
            std::cerr << "[Orchestrator] AudioEngine init failed" << std::endl;
            return false;
        }
        std::cout << "[Orchestrator] AudioEngine OK" << std::endl;
        
        // VAD
        vad = std::make_unique<audio::VADProcessor>();
        std::cout << "[Orchestrator] VADProcessor OK" << std::endl;
        
        // STT
        stt = std::make_unique<stt::STTEngine>(whisper_model);
        if (!stt->isReady()) {
            std::cerr << "[Orchestrator] STTEngine init failed" << std::endl;
            return false;
        }
        std::cout << "[Orchestrator] STTEngine OK" << std::endl;
        
        // LLM
        llm = std::make_unique<llm::ConversationEngine>(llm_url);
        std::cout << "[Orchestrator] ConversationEngine OK" << std::endl;
        
        // TTS
        tts = std::make_unique<tts::TTSEngine>("", tts_ref_voice);
        if (!tts->isReady()) {
            std::cerr << "[Orchestrator] TTSEngine init failed (missing reference voice?)" << std::endl;
            return false;
        }
        tts_streamer = std::make_unique<tts::TTSStreamer>(*tts);
        std::cout << "[Orchestrator] TTSEngine OK" << std::endl;
        
#ifdef RTV_HAS_PORCUPINE
        // Wake Word Detector
        std::string access_key;
        std::ifstream key_file(porcupine_key_file);
        if (key_file.good()) {
            std::getline(key_file, access_key);
            // Trim whitespace
            while (!access_key.empty() && (access_key.back() == '\n' || access_key.back() == '\r' || access_key.back() == ' ')) {
                access_key.pop_back();
            }
        }
        
        if (!access_key.empty()) {
            wakeword = std::make_unique<wakeword::WakeWordDetector>(
                access_key,
                porcupine_model,
                wakeword_models
            );
            
            if (wakeword->isReady()) {
                std::cout << "[Orchestrator] WakeWordDetector OK (say 'Hi Gemma')" << std::endl;
                
                // Load pre-recorded greeting WAV
                cached_greeting = loadWavFile(greeting_wav);
                if (!cached_greeting.empty()) {
                    std::cout << "[Orchestrator] Loaded greeting WAV (" << cached_greeting.size() << " samples)" << std::endl;
                } else {
                    std::cerr << "[Orchestrator] Warning: Greeting WAV not found: " << greeting_wav << std::endl;
                }
                
                // Load notification sounds (optional)
                cached_wake_sound = loadWavFile(wake_sound_wav);
                if (!cached_wake_sound.empty()) {
                    std::cout << "[Orchestrator] Loaded wake sound (" << cached_wake_sound.size() << " samples)" << std::endl;
                } else {
                    std::cerr << "[Orchestrator] Warning: Wake sound not found or invalid: " << wake_sound_wav << std::endl;
                }
                
                cached_sleep_sound = loadWavFile(sleep_sound_wav);
                if (!cached_sleep_sound.empty()) {
                    std::cout << "[Orchestrator] Loaded sleep sound (" << cached_sleep_sound.size() << " samples)" << std::endl;
                } else {
                    std::cerr << "[Orchestrator] Warning: Sleep sound not found or invalid: " << sleep_sound_wav << std::endl;
                }
            } else {
                std::cerr << "[Orchestrator] WakeWordDetector failed to initialize" << std::endl;
                wakeword.reset();
            }
        } else {
            std::cerr << "[Orchestrator] No Porcupine access key found in " << porcupine_key_file << std::endl;
        }
#endif
        
        std::cout << "[Orchestrator] All components initialized!" << std::endl;
        return true;
    }
    
    void run() {
        running = true;
        
#ifdef RTV_HAS_PORCUPINE
        if (wakeword && wakeword->isReady()) {
            setState(OrchestratorState::SLEEPING);
            std::cout << "[Orchestrator] Running... (say 'Hi Gemma' to wake)" << std::endl;
        } else {
            setState(OrchestratorState::IDLE);
            std::cout << "[Orchestrator] Running... (say something)" << std::endl;
        }
#else
        setState(OrchestratorState::IDLE);
        std::cout << "[Orchestrator] Running... (say something)" << std::endl;
#endif
        
        // Setup audio callback
        audio->setInputCallback([this](const float* samples, size_t count) {
            handleAudioInput(samples, count);
        });
        
        // Setup TTS audio callback (output stream runs at 24kHz)
        tts_streamer->setAudioCallback([this](const std::vector<float>& samples, int sr) {
            if (!interrupted) {
                audio->queuePlayback(samples.data(), samples.size());
            }
        });
        
        audio->start();
        
        while (running) {
            switch (state.load()) {
                case OrchestratorState::SLEEPING:
                    // Wake word is processed in handleAudioInput
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    break;
                    
                case OrchestratorState::IDLE:
                    // Check if we have speech buffered
                    if (hasSpeechReady()) {
                        awaiting_command = false;  // Got command, stop timeout
                        setState(OrchestratorState::PROCESSING);
                    }
#ifdef RTV_HAS_PORCUPINE
                    // Timeout after 8 seconds if waiting for command post-wake-word
                    // But only if TTS is done and no audio is playing
                    else if (awaiting_command && wakeword && wakeword->isReady()) {
                        if (!tts_streamer->isSpeaking() && !audio->isPlaying()) {
                            auto elapsed = std::chrono::steady_clock::now() - idle_start_time;
                            if (elapsed > std::chrono::seconds(8)) {
                                std::cout << "[Orchestrator] Timeout - going back to sleep" << std::endl;
                                awaiting_command = false;
                                
                                // Play sleep notification sound (if available)
                                if (!cached_sleep_sound.empty()) {
                                    audio->queuePlayback(cached_sleep_sound.data(), cached_sleep_sound.size());
                                }
                                
                                setState(OrchestratorState::SLEEPING);
                            }
                        } else {
                            // Reset timeout while audio is playing
                            idle_start_time = std::chrono::steady_clock::now();
                        }
                    }
#endif
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    break;
                    
                case OrchestratorState::LISTENING:
                    // Check if speech ended and we have audio to process
                    if (hasSpeechReady()) {
                        awaiting_command = false;  // Got command
                        setState(OrchestratorState::PROCESSING);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    break;
                    
                case OrchestratorState::PROCESSING:
                    processSTT();
                    break;
                    
                case OrchestratorState::THINKING:
                    processLLM();
                    break;
                    
                case OrchestratorState::SPEAKING:
                    // Wait for TTS to finish (with minimum time to let audio queue fill)
                    {
                        auto speaking_elapsed = std::chrono::steady_clock::now() - speaking_start_time;
                        bool min_time_passed = speaking_elapsed > std::chrono::milliseconds(500);
                        
                        if (min_time_passed && !tts_streamer->isSpeaking() && !audio->isPlaying()) {
#ifdef RTV_HAS_PORCUPINE
                            if (wakeword && wakeword->isReady()) {
                                // After any response (greeting or normal), go to IDLE and start timeout
                                // This allows user to continue conversation without saying wake word again
                                idle_start_time = std::chrono::steady_clock::now();
                                awaiting_command = true;  // Enable timeout to go to sleep
                                setState(OrchestratorState::IDLE);
                            } else {
                                setState(OrchestratorState::IDLE);
                            }
#else
                            setState(OrchestratorState::IDLE);
#endif
                        }
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    break;
                    
                case OrchestratorState::ERROR:
                    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                    setState(OrchestratorState::SLEEPING);
                    break;
            }
            
            // Check for interrupt
            if (interrupted) {
                tts_streamer->stop();
                audio->clearPlayback();
                interrupted = false;
#ifdef RTV_HAS_PORCUPINE
                if (wakeword && wakeword->isReady()) {
                    setState(OrchestratorState::SLEEPING);
                } else {
                    setState(OrchestratorState::IDLE);
                }
#else
                setState(OrchestratorState::IDLE);
#endif
            }
        }
        
        audio->stop();
    }
    
    void handleAudioInput(const float* samples, size_t count) {
#ifdef RTV_HAS_PORCUPINE
        // Process wake word when sleeping
        if (state == OrchestratorState::SLEEPING && wakeword && wakeword->isReady()) {
            int keyword_idx = wakeword->processFloat(samples, count);
            if (keyword_idx >= 0) {
                std::cout << "[WakeWord] Detected! Playing greeting..." << std::endl;
                
                // Clear any accumulated audio buffer
                {
                    std::lock_guard<std::mutex> lock(buffer_mutex);
                    audio_buffer.clear();
                }
                speech_active = false;
                silence_frames = 0;
                
                setState(OrchestratorState::SPEAKING);
                speaking_start_time = std::chrono::steady_clock::now();
                
                // Play wake notification sound first (if available)
                if (!cached_wake_sound.empty()) {
                    audio->queuePlayback(cached_wake_sound.data(), cached_wake_sound.size());
                }
                
                // Then play pre-recorded greeting (instant!)
                if (!cached_greeting.empty()) {
                    audio->queuePlayback(cached_greeting.data(), cached_greeting.size());
                    std::cout << "[Orchestrator] Queued " << cached_greeting.size() << " greeting samples" << std::endl;
                } else {
                    std::cerr << "[Orchestrator] Warning: No greeting audio cached!" << std::endl;
                }
                
                // Mark that we're waiting for a command after greeting
                awaiting_command = true;
                return;
            }
        }
#endif
        
        // Ignore audio input while speaking (prevents feedback)
        if (state == OrchestratorState::SPEAKING) {
            return;
        }
        
        // Process through VAD
        vad->process(samples, count);
        bool is_speech = vad->isSpeaking();
        
        if (is_speech) {
            if (state == OrchestratorState::IDLE) {
                setState(OrchestratorState::LISTENING);
            }
            speech_active = true;
            silence_frames = 0;
            
            // Buffer the audio
            std::lock_guard<std::mutex> lock(buffer_mutex);
            audio_buffer.insert(audio_buffer.end(), samples, samples + count);
        } else {
            // Silence
            if (speech_active) {
                // Still buffer a bit of silence for natural ending
                std::lock_guard<std::mutex> lock(buffer_mutex);
                audio_buffer.insert(audio_buffer.end(), samples, samples + count);
                silence_frames++;
                
                // End speech after ~500ms of silence (16000 Hz * 0.5s / 512 frames = ~15 frames)
                if (silence_frames > 15) {
                    speech_active = false;
                }
            }
        }
    }
    
    bool hasSpeechReady() {
        std::lock_guard<std::mutex> lock(buffer_mutex);
        // Require at least 0.5 seconds of audio and speech ended
        bool ready = audio_buffer.size() > 8000 && !speech_active && state == OrchestratorState::LISTENING;
        return ready;
    }
    
    int silence_frames = 0;
    
    void processSTT() {
        std::vector<float> audio_copy;
        {
            std::lock_guard<std::mutex> lock(buffer_mutex);
            audio_copy = std::move(audio_buffer);
            audio_buffer.clear();
        }
        
        if (audio_copy.empty()) {
            setState(OrchestratorState::IDLE);
            return;
        }
        
        std::cout << "[Orchestrator] Transcribing..." << std::endl;
        std::string transcript = stt->transcribe(audio_copy);
        
        if (transcript.empty()) {
            setState(OrchestratorState::IDLE);
            return;
        }
        
        std::cout << "[Orchestrator] User: " << transcript << std::endl;
        
        if (callbacks.onUserUtterance) {
            callbacks.onUserUtterance(transcript);
        }
        
        current_transcript = transcript;
        setState(OrchestratorState::THINKING);
    }
    
    void processLLM() {
        std::cout << "[Orchestrator] Thinking..." << std::endl;
        
        full_response.clear();  // Reset for new response
        tts_streamer->stop();   // Clear any leftover audio from previous response
        
        setState(OrchestratorState::SPEAKING);
        
        std::cout << "[LLM] Sending: " << current_transcript << std::endl;
        
        // Stream LLM response to TTS
        bool got_tokens = false;
        llm->chatStreaming(current_transcript, [this, &got_tokens](const std::string& token) {
            if (interrupted) return;
            
            got_tokens = true;
            tts_streamer->feedToken(token);
            full_response += token;
            std::cout << token << std::flush;  // Real-time output
        });
        
        std::cout << std::endl;
        
        if (!got_tokens) {
            std::cout << "[LLM] Warning: No tokens received from LLM!" << std::endl;
        }
        
        tts_streamer->flush();
        
        // Wait for audio to actually finish playing (with debouncing to avoid race between chunks)
        // TTSStreamer::flush() returns after synthesis but audio may still be playing
        int silence_count = 0;
        while (!interrupted && silence_count < 5) {  // Require 5 consecutive "not playing" checks
            if (!audio->isPlaying() && !tts_streamer->isSpeaking()) {
                silence_count++;
            } else {
                silence_count = 0;  // Reset if still active
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "[Orchestrator] Rosey: " << full_response << std::endl;
        
        if (callbacks.onAssistantResponse) {
            callbacks.onAssistantResponse(full_response);
        }
        
        full_response.clear();
    }
    
    std::string processText(const std::string& text) {
        if (!llm) {
            llm = std::make_unique<llm::ConversationEngine>(llm_url);
        }
        std::string response = llm->chat(text);
        return response;
    }
    
    std::string current_transcript;
    std::string full_response;
};

Orchestrator::Orchestrator() : impl_(std::make_unique<Impl>()) {}
Orchestrator::~Orchestrator() { stop(); }

bool Orchestrator::initialize() { return impl_->initialize(); }

void Orchestrator::run() { impl_->run(); }

void Orchestrator::start() {
    impl_->worker_thread = std::thread([this]() { run(); });
}

void Orchestrator::stop() {
    impl_->running = false;
    if (impl_->worker_thread.joinable()) {
        impl_->worker_thread.join();
    }
}

void Orchestrator::interrupt() { impl_->interrupted = true; }

bool Orchestrator::isRunning() const { return impl_->running; }

OrchestratorState Orchestrator::state() const { return impl_->state; }

void Orchestrator::setCallbacks(OrchestratorCallbacks callbacks) {
    impl_->callbacks = std::move(callbacks);
}

std::string Orchestrator::processText(const std::string& text) {
    return impl_->processText(text);
}

} // namespace rtv
