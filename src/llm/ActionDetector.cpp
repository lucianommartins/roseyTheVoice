/**
 * ActionDetector.cpp - FunctionGemma for tool/action identification
 */

#include <iostream>
#include <string>
#include <optional>
#include <vector>

namespace rtv::llm {

struct ToolCall {
    std::string name;
    std::string params_json;
    float confidence;
};

class ActionDetector {
public:
    ActionDetector() {
        std::cout << "[ActionDetector] FunctionGemma ready (via shared memory IPC)" << std::endl;
    }
    
    /**
     * Detect if user input maps to a tool call
     * @param text User's transcribed speech
     * @return ToolCall if detected, nullopt for general conversation
     */
    std::optional<ToolCall> detect(const std::string& text) {
        // TODO: Send to FunctionGemma via shared memory IPC
        // Parse JSON response
        
        // Placeholder: simple keyword matching
        if (text.find("hora") != std::string::npos) {
            return ToolCall{"get_time", "{}", 0.95f};
        }
        if (text.find("calendário") != std::string::npos || 
            text.find("compromisso") != std::string::npos) {
            return ToolCall{"check_calendar", R"({"date": "today"})", 0.90f};
        }
        if (text.find("email") != std::string::npos) {
            return ToolCall{"check_email", R"({"count": 5})", 0.88f};
        }
        if (text.find("clima") != std::string::npos || 
            text.find("tempo") != std::string::npos) {
            return ToolCall{"get_weather", "{}", 0.85f};
        }
        
        return std::nullopt; // General conversation
    }
    
    /**
     * Get list of available tools
     */
    static std::vector<std::string> availableTools() {
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
};

} // namespace rtv::llm
