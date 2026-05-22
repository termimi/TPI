#ifndef MCP_TOOLS_HANDLER_H
#define MCP_TOOLS_HANDLER_H
#include <vector>
#include <ArduinoJson.h>
#include "mcp_tools.h"
#include <Arduino.h>

/// @brief Handles the global tool logic (Storage, list, calls)
class McpToolHandler
{
    private:
        // Vector containing the list of the server tools
        std::vector<McpTool*> _tools_registry;
        /// @brief Size of the registry tool
        int _reg_size;
    public:
        /// @brief Create an McpToolHandler instance
        /// @param reg_size size of the tool registry (maximum number of tools that the server will provide)
        McpToolHandler(int reg_size);

        /// @brief Add a tool to the tools registry
        /// @param tool Pointer to a tool structure
        void add_tool(McpTool *tool);

        /// @brief returns a json object that contains all the tools of the server (Is triggerd when a tool list call is recieved)
        /// @param awnser Json object that contains the all the schemas of all the tools
        void retrieve_tools_list_json(JsonDocument &awnser);

        /// @brief call the tool sent by the client
        /// @param mcp_params JsonObject that represents the "params" object sent by the client
        bool call_tool(JsonObject &mcp_params,JsonArray &result);
    };
#endif