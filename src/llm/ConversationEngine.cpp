/**
 * ConversationEngine.cpp - Gemma 3 12B conversation implementation
 */

#include "rtv/llm/ConversationEngine.hpp"
#include "rtv/llm/LLMClient.hpp"

#include <iostream>
#include <sstream>

namespace rtv::llm {

// Default system prompt for Rosey persona
static const char* DEFAULT_SYSTEM_PROMPT = R"(Voce e Rosey, uma assistente virtual amigavel e prestativa.
Voce responde em portugues brasileiro de forma natural e concisa.
Voce e eficiente e vai direto ao ponto, sem rodeios.
Quando nao souber algo, admita honestamente.
IMPORTANTE: Nunca use emojis nas suas respostas, pois elas serao lidas em voz alta.)";

struct ConversationEngine::Impl {
    LLMClient client;
    std::string system_prompt;
    std::vector<Message> history;
    
    Impl(const std::string& server_url)
        : client(server_url, 60000)
        , system_prompt(DEFAULT_SYSTEM_PROMPT) {
    }
    
    std::string buildPrompt(const std::string& user_message) {
        std::stringstream prompt;
        
        // Gemma 3 instruction format
        prompt << "<start_of_turn>user\n";
        prompt << system_prompt << "\n\n";
        
        // Add conversation history
        for (const auto& msg : history) {
            if (msg.role == Message::Role::User) {
                prompt << "Usuario: " << msg.content << "\n";
            } else if (msg.role == Message::Role::Assistant) {
                prompt << "Rosey: " << msg.content << "\n";
            }
        }
        
        // Add current message
        prompt << "Usuario: " << user_message << "\n";
        prompt << "<end_of_turn>\n";
        prompt << "<start_of_turn>model\n";
        prompt << "Rosey: ";
        
        return prompt.str();
    }
};

ConversationEngine::ConversationEngine(const std::string& server_url)
    : impl_(std::make_unique<Impl>(server_url)) {
    std::cout << "[ConversationEngine] Connecting to " << server_url << std::endl;
}

ConversationEngine::~ConversationEngine() = default;

bool ConversationEngine::isReady() {
    return impl_->client.isHealthy();
}

void ConversationEngine::setSystemPrompt(const std::string& prompt) {
    impl_->system_prompt = prompt;
}

std::string ConversationEngine::chat(const std::string& user_message) {
    std::string prompt = impl_->buildPrompt(user_message);
    
    CompletionRequest request;
    request.prompt = prompt;
    request.max_tokens = 512;
    request.temperature = 0.7f;
    request.stop = {"<end_of_turn>", "Usuario:", "\n\n"};
    request.stream = false;
    
    auto response = impl_->client.complete(request);
    
    if (!response.content.empty()) {
        // Add to history
        impl_->history.push_back({Message::Role::User, user_message});
        impl_->history.push_back({Message::Role::Assistant, response.content});
        
        // Keep history manageable (last 10 turns)
        while (impl_->history.size() > 20) {
            impl_->history.erase(impl_->history.begin());
        }
    }
    
    return response.content;
}

std::string ConversationEngine::chatStreaming(
    const std::string& user_message,
    ResponseCallback callback
) {
    std::string prompt = impl_->buildPrompt(user_message);
    
    CompletionRequest request;
    request.prompt = prompt;
    request.max_tokens = 512;
    request.temperature = 0.7f;
    request.stop = {"<end_of_turn>", "Usuario:", "\n\n"};
    request.stream = true;
    
    auto response = impl_->client.completeStreaming(request, [&callback](const std::string& token) {
        if (callback) {
            callback(token);
        }
        return true;  // Continue streaming
    });
    
    if (!response.content.empty()) {
        impl_->history.push_back({Message::Role::User, user_message});
        impl_->history.push_back({Message::Role::Assistant, response.content});
        
        while (impl_->history.size() > 20) {
            impl_->history.erase(impl_->history.begin());
        }
    }
    
    return response.content;
}

std::string ConversationEngine::chatWithToolResult(
    const std::string& original_query,
    const std::string& tool_name,
    const std::string& tool_result
) {
    std::stringstream augmented_prompt;
    augmented_prompt << original_query << "\n\n";
    augmented_prompt << "[Resultado da acao '" << tool_name << "']\n";
    augmented_prompt << tool_result << "\n\n";
    augmented_prompt << "Por favor, responda ao usuario com base nesse resultado.";
    
    return chat(augmented_prompt.str());
}

void ConversationEngine::clearHistory() {
    impl_->history.clear();
}

const std::vector<Message>& ConversationEngine::history() const {
    return impl_->history;
}

} // namespace rtv::llm
