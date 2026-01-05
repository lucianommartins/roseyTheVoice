/**
 * ActionDetector.cpp - FunctionGemma action detection implementation
 */

#include "rtv/llm/ActionDetector.hpp"
#include "rtv/llm/LLMClient.hpp"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace rtv::llm {

// Tool catalog for FunctionGemma
static const char* TOOL_CATALOG = R"(
Voce e um assistente que detecta acoes do usuario.
Analise a mensagem e retorne um JSON com a acao detectada.

Acoes disponiveis:
- play_music: tocar musica (params: query, artist, genre)
- check_calendar: ver agenda (params: date, days_ahead)
- add_calendar_event: adicionar evento (params: title, date, time, duration)
- send_email: enviar email (params: to, subject, body)
- check_email: ver emails (params: folder, count)
- search_web: buscar na internet (params: query)
- get_weather: previsao do tempo (params: location, days)
- control_media: controlar midia (params: action: play/pause/next/prev/volume_up/volume_down)
- none: nenhuma acao detectada, apenas conversa

Responda APENAS com JSON no formato:
{"action": "nome_acao", "params": {...}, "confidence": 0.0-1.0}
)";

// Which actions require online connectivity
static const std::map<std::string, bool> ACTION_REQUIRES_ONLINE = {
    {"play_music", true},
    {"check_calendar", false},  // Uses cache
    {"add_calendar_event", true},
    {"send_email", true},
    {"check_email", false},  // Uses cache
    {"search_web", true},
    {"get_weather", true},
    {"control_media", false},
    {"none", false}
};

struct ActionDetector::Impl {
    LLMClient client;
    
    Impl(const std::string& server_url)
        : client(server_url, 30000) {  // 30s timeout for detection
    }
    
    std::string buildPrompt(const std::string& query) {
        std::stringstream prompt;
        prompt << TOOL_CATALOG << "\n\n";
        prompt << "Mensagem do usuario: " << query << "\n\n";
        prompt << "JSON:";
        return prompt.str();
    }
    
    std::optional<DetectedAction> parseResponse(const std::string& response) {
        // Find JSON in response
        size_t start = response.find('{');
        size_t end = response.rfind('}');
        
        if (start == std::string::npos || end == std::string::npos || end < start) {
            return std::nullopt;
        }
        
        std::string json_str = response.substr(start, end - start + 1);
        
        try {
            json j = json::parse(json_str);
            
            DetectedAction action;
            action.name = j.value("action", "none");
            action.confidence = j.value("confidence", 0.0f);
            
            if (j.contains("params") && j["params"].is_object()) {
                for (auto& [key, value] : j["params"].items()) {
                    if (value.is_string()) {
                        action.parameters[key] = value.get<std::string>();
                    } else {
                        action.parameters[key] = value.dump();
                    }
                }
            }
            
            auto it = ACTION_REQUIRES_ONLINE.find(action.name);
            action.requires_online = (it != ACTION_REQUIRES_ONLINE.end()) ? it->second : true;
            
            if (action.name == "none" || action.confidence < 0.3f) {
                return std::nullopt;
            }
            
            return action;
            
        } catch (const std::exception& e) {
            std::cerr << "[ActionDetector] JSON parse error: " << e.what() << std::endl;
            return std::nullopt;
        }
    }
};

ActionDetector::ActionDetector(const std::string& server_url)
    : impl_(std::make_unique<Impl>(server_url)) {
    std::cout << "[ActionDetector] Connecting to " << server_url << std::endl;
}

ActionDetector::~ActionDetector() = default;

bool ActionDetector::isReady() {
    return impl_->client.isHealthy();
}

std::optional<DetectedAction> ActionDetector::detect(const std::string& query) {
    std::string prompt = impl_->buildPrompt(query);
    
    CompletionRequest request;
    request.prompt = prompt;
    request.max_tokens = 256;
    request.temperature = 0.1f;  // Low temperature for structured output
    request.stop = {"\n\n", "Mensagem"};
    request.stream = false;
    
    auto response = impl_->client.complete(request);
    
    if (response.content.empty()) {
        std::cerr << "[ActionDetector] Empty response from server" << std::endl;
        return std::nullopt;
    }
    
    return impl_->parseResponse(response.content);
}

std::vector<std::string> ActionDetector::supportedActions() const {
    return {
        "play_music",
        "check_calendar",
        "add_calendar_event",
        "send_email",
        "check_email",
        "search_web",
        "get_weather",
        "control_media"
    };
}

} // namespace rtv::llm
