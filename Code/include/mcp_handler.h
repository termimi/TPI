#ifndef MCP_HANDLER_H
#define MCP_HANDLER_H

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "mcp_tools_handler.h"
#include <Arduino.h>


/// @brief Handler that coordinates all the MCP logic (Type of request, tools to use etc...)
class McpHandler{
    public:
        McpHandler(JsonDocument &json_request, String *awnser);
        const char* get_session_id() const;

        /// @brief Tells the tool handler to execute one of his tools
        void execute_tools();
        
        /// @brief Handle the MCP response logic once the cli send something
        void handle();

        /// @brief sets the good header for the type of response that the servers has to send
        /// @param _mcp_response the http response to modify
        void create_mcp_response_headrer(AsyncWebServerResponse *_mcp_response);

        /// @brief Create the mcp tool handler
        /// @param registry_size size of the tool registry
        /// @return the mcp tool handler
        McpToolHandler* create_tool_handler(uint16_t registry_size);

        /// @brief True or False whether the last received request is a tool call or not
        /// @return 
        bool get_is_request_call();

    private:
        JsonDocument    &_json_request;                 // Received json body
        String          *_awnser;
        int             _mcp_recieved_code;             // type of request the cli has send to the server (0 = initialize, 1 = request, 2 = method, 3 = response, -1 = None)
        bool            _is_request_call_tool = false;  // is the last recieved request a tool call
        McpToolHandler  *_mcp_tools_handler;
        const char      *_session_id;                   // current session ID

        /// @brief returns the request type as a number (0 : init, 1 : request, 2 : notification, 3 : response)
        int _get_mcp_request_type();

        /// @brief respond to the initialize message
        void _init_respond();

        /// @brief Decide what functions to trigger when a request is recieved
        void _mcp_handle_recieved_request();
};
#endif