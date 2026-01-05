/**
 * Orchestrator.cpp - Main state machine
 */

#include <iostream>
#include <atomic>

namespace rtv {

enum class State {
    IDLE,
    LISTENING,
    THINKING,
    ACTING,
    SPEAKING,
    ERROR
};

class Orchestrator {
public:
    Orchestrator() {
        std::cout << "[Orchestrator] Initialized" << std::endl;
    }
    
    void run() {
        while (running_) {
            switch (state_) {
                case State::IDLE:
                    // Wait for wake word
                    break;
                case State::LISTENING:
                    // Record audio until VAD timeout
                    break;
                case State::THINKING:
                    // STT -> ActionDetector -> ConversationEngine
                    break;
                case State::ACTING:
                    // Execute tool
                    break;
                case State::SPEAKING:
                    // TTS output
                    break;
                case State::ERROR:
                    // Handle error, return to IDLE
                    state_ = State::IDLE;
                    break;
            }
        }
    }
    
    void stop() { running_ = false; }
    State state() const { return state_; }

private:
    std::atomic<bool> running_{true};
    State state_ = State::IDLE;
};

} // namespace rtv
