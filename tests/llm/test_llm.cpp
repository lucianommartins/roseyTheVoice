/**
 * test_llm.cpp - LLM integration tests
 */

#include <iostream>
#include <thread>
#include <chrono>

#include "rtv/llm/LLMClient.hpp"
#include "rtv/llm/ConversationEngine.hpp"
#include "rtv/llm/ActionDetector.hpp"

void test_llm_client_health() {
    std::cout << "\n--- Test: LLMClient Health Check ---" << std::endl;
    
    rtv::llm::LLMClient client("http://localhost:8080");
    
    if (client.isHealthy()) {
        std::cout << "[PASS] Gemma 3 12B server is healthy" << std::endl;
    } else {
        std::cout << "[SKIP] Server not available at http://localhost:8080" << std::endl;
        std::cout << "  Run: docker-compose up -d" << std::endl;
    }
}

void test_conversation_simple() {
    std::cout << "\n--- Test: Simple Conversation ---" << std::endl;
    
    rtv::llm::ConversationEngine engine("http://localhost:8080");
    
    if (!engine.isReady()) {
        std::cout << "[SKIP] Server not available" << std::endl;
        return;
    }
    
    std::string response = engine.chat("Oi, tudo bem?");
    
    std::cout << "  User: Oi, tudo bem?" << std::endl;
    std::cout << "  Rosey: " << response << std::endl;
    
    if (!response.empty()) {
        std::cout << "[PASS] Got response from Gemma 3" << std::endl;
    } else {
        std::cout << "[FAIL] Empty response" << std::endl;
    }
}

void test_conversation_streaming() {
    std::cout << "\n--- Test: Streaming Conversation ---" << std::endl;
    
    rtv::llm::ConversationEngine engine("http://localhost:8080");
    
    if (!engine.isReady()) {
        std::cout << "[SKIP] Server not available" << std::endl;
        return;
    }
    
    std::cout << "  User: O que e inteligencia artificial?" << std::endl;
    std::cout << "  Rosey: ";
    
    int token_count = 0;
    std::string response = engine.chatStreaming(
        "O que e inteligencia artificial? Responda em uma frase curta.",
        [&token_count](const std::string& token) {
            std::cout << token << std::flush;
            token_count++;
        }
    );
    
    std::cout << std::endl;
    std::cout << "  Tokens: " << token_count << std::endl;
    
    if (token_count > 0) {
        std::cout << "[PASS] Streaming works" << std::endl;
    } else {
        std::cout << "[FAIL] No tokens received" << std::endl;
    }
}

void test_action_detection() {
    std::cout << "\n--- Test: Action Detection ---" << std::endl;
    
    rtv::llm::ActionDetector detector("http://localhost:8081");
    
    if (!detector.isReady()) {
        std::cout << "[SKIP] FunctionGemma server not available at port 8081" << std::endl;
        return;
    }
    
    // Test cases
    std::vector<std::pair<std::string, std::string>> test_cases = {
        {"Toca uma musica do Beatles", "play_music"},
        {"Como esta o tempo hoje?", "get_weather"},
        {"Quais sao meus compromissos de amanha?", "check_calendar"},
        {"Oi, tudo bem?", "none"},  // Should not detect action
    };
    
    int passed = 0;
    for (const auto& [query, expected] : test_cases) {
        auto action = detector.detect(query);
        
        std::cout << "  Query: \"" << query << "\"" << std::endl;
        
        if (action) {
            std::cout << "  Detected: " << action->name 
                      << " (confidence: " << action->confidence << ")" << std::endl;
            
            if (action->name == expected || (expected == "none" && action->confidence < 0.3f)) {
                passed++;
            }
        } else if (expected == "none") {
            std::cout << "  Detected: none" << std::endl;
            passed++;
        } else {
            std::cout << "  Detected: none (expected " << expected << ")" << std::endl;
        }
    }
    
    std::cout << "  Results: " << passed << "/" << test_cases.size() << " passed" << std::endl;
    
    if (passed == static_cast<int>(test_cases.size())) {
        std::cout << "[PASS] Action detection works" << std::endl;
    } else {
        std::cout << "[PARTIAL] Some test cases failed" << std::endl;
    }
}

int main() {
    std::cout << "=== LLM Integration Tests ===" << std::endl;
    std::cout << "Note: These tests require docker-compose services running" << std::endl;
    std::cout << "Run: docker-compose up -d" << std::endl;
    
    test_llm_client_health();
    test_conversation_simple();
    test_conversation_streaming();
    test_action_detection();
    
    std::cout << "\nTests complete!" << std::endl;
    return 0;
}
