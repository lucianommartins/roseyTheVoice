/**
 * LLMClient.cpp - HTTP client for llama.cpp server
 * 
 * Uses cpp-httplib with Request.content_receiver for true streaming responses.
 */

#include "rtv/llm/LLMClient.hpp"

#include <iostream>
#include <sstream>

#include <httplib.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace rtv::llm {

struct LLMClient::Impl {
    std::unique_ptr<httplib::Client> client;
    std::string base_url;
    
    Impl(const std::string& url, int timeout_ms) : base_url(url) {
        client = std::make_unique<httplib::Client>(url);
        client->set_connection_timeout(timeout_ms / 1000, (timeout_ms % 1000) * 1000);
        client->set_read_timeout(timeout_ms / 1000, (timeout_ms % 1000) * 1000);
        client->set_write_timeout(timeout_ms / 1000, (timeout_ms % 1000) * 1000);
    }
};

LLMClient::LLMClient(const std::string& base_url, int timeout_ms)
    : impl_(std::make_unique<Impl>(base_url, timeout_ms))
    , base_url_(base_url)
    , timeout_ms_(timeout_ms) {
}

LLMClient::~LLMClient() = default;

bool LLMClient::isHealthy() {
    auto res = impl_->client->Get("/health");
    return res && res->status == 200;
}

CompletionResponse LLMClient::complete(const CompletionRequest& request) {
    CompletionResponse response;
    
    // Build request JSON
    json req_json = {
        {"prompt", request.prompt},
        {"n_predict", request.max_tokens},
        {"temperature", request.temperature},
        {"top_p", request.top_p},
        {"stream", false}
    };
    
    if (!request.stop.empty()) {
        req_json["stop"] = request.stop;
    }
    
    auto res = impl_->client->Post(
        "/completion",
        req_json.dump(),
        "application/json"
    );
    
    if (!res || res->status != 200) {
        std::cerr << "[LLMClient] Request failed: " 
                  << (res ? std::to_string(res->status) : "no response") 
                  << std::endl;
        return response;
    }
    
    try {
        json res_json = json::parse(res->body);
        response.content = res_json.value("content", "");
        response.tokens_generated = res_json.value("tokens_predicted", 0);
        response.tokens_prompt = res_json.value("tokens_evaluated", 0);
        response.stopped = res_json.value("stopped_eos", false) || 
                          res_json.value("stopped_word", false);
        response.stop_reason = res_json.value("stopping_word", "");
    } catch (const std::exception& e) {
        std::cerr << "[LLMClient] JSON parse error: " << e.what() << std::endl;
    }
    
    return response;
}

CompletionResponse LLMClient::completeStreaming(
    const CompletionRequest& request,
    StreamCallback callback
) {
    CompletionResponse response;
    std::stringstream full_content;
    
    // Build request JSON with streaming enabled
    json req_json = {
        {"prompt", request.prompt},
        {"n_predict", request.max_tokens},
        {"temperature", request.temperature},
        {"top_p", request.top_p},
        {"stream", true}
    };
    
    if (!request.stop.empty()) {
        req_json["stop"] = request.stop;
    }
    
    std::string body = req_json.dump();
    bool should_stop = false;
    std::string buffer;
    
    // Create request with content receiver for streaming
    httplib::Request req;
    req.method = "POST";
    req.path = "/completion";
    req.set_header("Content-Type", "application/json");
    req.body = body;
    
    // Set streaming content receiver - called as data arrives
    req.content_receiver = [&](const char* data, size_t data_length,
                               uint64_t /*offset*/, uint64_t /*total_length*/) -> bool {
        if (should_stop) return false;
        
        buffer.append(data, data_length);
        
        // Process complete lines from buffer
        size_t pos;
        while ((pos = buffer.find('\n')) != std::string::npos) {
            std::string line = buffer.substr(0, pos);
            buffer.erase(0, pos + 1);
            
            // Remove trailing \r if present
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            
            // Skip empty lines
            if (line.empty()) continue;
            
            std::string json_str = line;
            
            // Handle SSE format if present (data: prefix)
            if (line.rfind("data: ", 0) == 0) {
                json_str = line.substr(6);
            }
            
            if (json_str == "[DONE]") {
                response.stopped = true;
                continue;
            }
            
            try {
                json data_json = json::parse(json_str);
                std::string token = data_json.value("content", "");
                
                if (!token.empty()) {
                    full_content << token;
                    response.tokens_generated++;
                    
                    // Call user callback with each token
                    if (callback && !callback(token)) {
                        should_stop = true;
                        return false;
                    }
                }
                
                if (data_json.value("stop", false)) {
                    response.stopped = true;
                    response.stop_reason = data_json.value("stopping_word", "");
                    std::cerr << "[LLM] Stopped with reason: " << response.stop_reason << std::endl;
                }
            } catch (const std::exception& e) {
                std::cerr << "[LLM] Parse error: " << e.what() << " - data: " << json_str.substr(0, 100) << std::endl;
            }
        }
        
        return true;
    };
    
    // Send request - content_receiver will be called as data arrives
    auto result = impl_->client->send(req);
    
    response.content = full_content.str();
    
    if (!result) {
        std::cerr << "[LLMClient] Streaming request failed: " 
                  << httplib::to_string(result.error()) << std::endl;
    }
    
    return response;
}

std::vector<float> LLMClient::embed(const std::string& text) {
    json req_json = {
        {"content", text}
    };
    
    auto res = impl_->client->Post(
        "/embedding",
        req_json.dump(),
        "application/json"
    );
    
    if (!res || res->status != 200) {
        std::cerr << "[LLMClient] Embedding request failed" << std::endl;
        return {};
    }
    
    try {
        json res_json = json::parse(res->body);
        return res_json.value("embedding", std::vector<float>{});
    } catch (const std::exception& e) {
        std::cerr << "[LLMClient] JSON parse error: " << e.what() << std::endl;
        return {};
    }
}

} // namespace rtv::llm
