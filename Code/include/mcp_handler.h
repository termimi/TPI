#ifndef MCP_HANDLER_H
#define MCP_HANDLER_H

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include "mcp_tools_handler.h"
#include <Arduino.h>


// func declaration
class McpHandler{
    public:
        McpHandler(JsonDocument &json_request, String *awnser);
        
        /// @brief Handle the MCP response logic once the cli send something
        void handle();

        /// @brief sets the good header for the type of response that the servers has to send
        /// @param _mcp_response the http response to modify
        void create_mcp_response_headrer(AsyncWebServerResponse *_mcp_response);
        McpToolHandler* create_tool_handler(uint16_t registry_size);
        void execute_tool();
        bool get_is_request_call();
        const char* get_session_id() const;

    private:
        JsonDocument &_json_request;
        String *_awnser;
        // type of request the cli has send to the server (0 = initialize, 1 = request, 2 = method, 3 = response, -1 = None)
        int _mcp_recieved_code;
        bool _is_request_call_tool = false;
        McpToolHandler* _mcp_tools_handler;
        const char* _session_id;

        /// @brief returns the request type as a number (0 : init, 1 : request, 2 : notification, 3 : response)
        int _get_mcp_request_type();
        /// @brief respond to the initialize message
        void _init_respond();

        /// @brief Decide what functions to trigger when a request is recieved
        void _mcp_handle_recieved_request();
};
#endif