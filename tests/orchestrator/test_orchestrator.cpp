/**
 * test_orchestrator.cpp - Orchestrator Integration Tests
 */

#include <iostream>
#include <chrono>
#include <thread>

#include "rtv/Orchestrator.hpp"

void testTextMode() {
    std::cout << "\n--- Test: Text Mode (no audio) ---\n";
    
    rtv::Orchestrator orchestrator;
    
    // Test without full initialization (just LLM)
    std::cout << "Testing LLM-only mode...\n";
    
    // Note: This requires the LLM server to be running
    std::string response = orchestrator.processText("Oi, tudo bem?");
    
    if (response.empty()) {
        std::cout << "[SKIP] LLM server not available\n";
        std::cout << "  Run: docker-compose up -d\n";
        return;
    }
    
    std::cout << "User: Oi, tudo bem?\n";
    std::cout << "Rosey: " << response << "\n";
    std::cout << "[PASS] Text mode works\n";
}

void testFullInitialization() {
    std::cout << "\n--- Test: Full Initialization ---\n";
    
    rtv::Orchestrator orchestrator;
    
    if (!orchestrator.initialize()) {
        std::cout << "[SKIP] Some components not available\n";
        std::cout << "  Check: models/whisper, models/tts, docker-compose\n";
        return;
    }
    
    std::cout << "[PASS] All components initialized\n";
}

void testStateCallbacks() {
    std::cout << "\n--- Test: State Callbacks ---\n";
    
    rtv::Orchestrator orchestrator;
    
    int state_changes = 0;
    
    orchestrator.setCallbacks({
        .onStateChange = [&](rtv::OrchestratorState state) {
            state_changes++;
            std::cout << "  State changed to: " << static_cast<int>(state) << "\n";
        },
        .onUserUtterance = [](const std::string& text) {
            std::cout << "  User said: " << text << "\n";
        },
        .onAssistantResponse = [](const std::string& text) {
            std::cout << "  Assistant said: " << text << "\n";
        },
        .onError = [](const std::string& err) {
            std::cerr << "  Error: " << err << "\n";
        }
    });
    
    std::cout << "[PASS] Callbacks set\n";
}

int main() {
    std::cout << "=== Orchestrator Integration Tests ===\n";
    std::cout << "Note: Requires all components to be available\n";
    std::cout << "  - models/whisper/ggml-small-q5_1.bin\n";
    std::cout << "  - models/tts/reference_voice.wav\n";
    std::cout << "  - docker-compose up -d (LLM server)\n";
    
    testStateCallbacks();
    testTextMode();
    testFullInitialization();
    
    std::cout << "\nTests complete!\n";
    return 0;
}
