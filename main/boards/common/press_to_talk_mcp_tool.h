#ifndef PRESS_TO_TALK_MCP_TOOL_H
#define PRESS_TO_TALK_MCP_TOOL_H

#include "mcp_server.h"
#include "settings.h"

// Reusable press-to-talk MCP tool
class PressToTalkMcpTool {
private:
    bool press_to_talk_enabled_;

public:
    PressToTalkMcpTool();
    
    // Initialize tool and register with MCP server
    void Initialize();
    
    // Get current press-to-talk mode
    bool IsPressToTalkEnabled() const;

private:
    // MCP tool callback
    ReturnValue HandleSetPressToTalk(const PropertyList& properties);
    
    // Set press-to-talk state and persist
    void SetPressToTalkEnabled(bool enabled);
};

#endif // PRESS_TO_TALK_MCP_TOOL_H 