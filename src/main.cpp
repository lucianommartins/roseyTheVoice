/**
 * Rosey The Voice (RTV) - Main Entry Point
 * 
 * Local-first voice assistant powered by Google Gemma models.
 * Inspired by Rosey the Robot from The Jetsons.
 */

#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

// #include "rtv/orchestrator/Orchestrator.hpp"
// #include "rtv/audio/AudioEngine.hpp"

std::atomic<bool> g_running{true};

void signalHandler(int signal) {
    std::cout << "\n[RTV] Shutting down..." << std::endl;
    g_running = false;
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    std::cout << R"(
    ╔═══════════════════════════════════════════════╗
    ║        ROSEY THE VOICE (RTV) v0.1.0           ║
    ║   Local-first voice assistant with Gemma 3    ║
    ╚═══════════════════════════════════════════════╝
    )" << std::endl;
    
    // TODO: Parse command line arguments
    // TODO: Initialize AudioEngine
    // TODO: Initialize STTEngine (preload whisper-medium)
    // TODO: Initialize LLM clients (shared memory IPC)
    // TODO: Initialize Orchestrator
    // TODO: Start main loop
    
    std::cout << "[RTV] Initializing..." << std::endl;
    
    // Placeholder main loop
    while (g_running) {
        // TODO: Process audio, detect wake word, STT, LLM, TTS
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "[RTV] Goodbye!" << std::endl;
    return 0;
}
