#include <cJSON.h>
#include <esp_log.h>

#include <cstring>

#include "application.h"
#include "board.h"
#include "config.h"
#include "mcp_server.h"
#include "sdkconfig.h"
#include "settings.h"
#include "display.h"

#define TAG "MCPController"

class MCPController {
public:
    MCPController() {
        RegisterMcpTools();
        ESP_LOGI(TAG, "Registering MCP tools");
    }

	void RegisterMcpTools() {
        auto& mcp_server = McpServer::GetInstance();
		ESP_LOGI(TAG, "Starting MCP tool registration...");

	mcp_server.AddTool(
        "self.AEC.set_mode", 
        "Set AEC barge-in mode. Use when the user wants to switch barge-in mode, finds AI conversations too easy to interrupt, or cannot barge in.\n"
        "Parameters:\n"
        "   `mode`: Barge-in mode; only `kAecOff` (off) or `kAecOnDeviceSide` (on)\n"
        "Return value:\n"
        "   Status message; no confirmation needed; speak the result immediately\n",
        PropertyList({
            Property("mode", kPropertyTypeString)
        }), 
        [](const PropertyList& properties) -> ReturnValue {
            auto mode = properties["mode"].value<std::string>();
            auto& app = Application::GetInstance();
            vTaskDelay(pdMS_TO_TICKS(2000));
            if (mode == "kAecOff") {
                app.SetAecMode(kAecOff);
                return "{\"success\": true, \"message\": \"AEC barge-in mode disabled\"}";
            }else {
                auto& board = Board::GetInstance();
                app.SetAecMode(kAecOnDeviceSide);
                
                return "{\"success\": true, \"message\": \"AEC barge-in mode enabled\"}";
            }
        }
    );

    mcp_server.AddTool(
        "self.AEC.get_mode",
        "Get AEC barge-in mode status. Use when the user asks for the current barge-in mode.\n"
        "Return value:\n"
        "   Status message; no confirmation needed; speak the result immediately\n",
        PropertyList(),  
        [](const PropertyList&) -> ReturnValue {
            auto& app = Application::GetInstance();
            const bool is_currently_off = (app.GetAecMode() == kAecOff);
           if (is_currently_off) {
                return "{\"success\": true, \"message\": \"AEC barge-in mode is off\"}";
            }else {
                return "{\"success\": true, \"message\": \"AEC barge-in mode is on\"}";
            }
        }
    );
	
    mcp_server.AddTool(
        "self.res.esp_restart",
        "Reboot the device. Use when the user asks to restart.\n",
        PropertyList(),  
        [](const PropertyList&) -> ReturnValue {
            vTaskDelay(pdMS_TO_TICKS(1000));
            // Reboot the device
            esp_restart();
            return true;
        }
    );

        ESP_LOGI(TAG, "MCP tool registration complete");
    }

};

static MCPController* g_mcp_controller = nullptr;

void InitializeMCPController() {
    if (g_mcp_controller == nullptr) {
        g_mcp_controller = new MCPController();
        ESP_LOGI(TAG, "Registering MCP tools");
    }
}