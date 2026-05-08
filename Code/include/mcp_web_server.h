#ifndef MCP_WEB_SERVER_H
#define MCP_WEB_SERVER_H

#include <ESPAsyncWebServer.h>
#include "mcp_handler.h"
#include <Arduino.h>

class McpWebHandler : public AsyncWebHandler {
    private:
        std::function<bool(AsyncWebServerRequest*)> _matcher;
        ArRequestHandlerFunction _onRequest;
        ArUploadHandlerFunction _onUpload;
        ArBodyHandlerFunction _onBody;
    public:
        McpWebHandler(
            std::function<bool(AsyncWebServerRequest*)> matcher, 
            ArRequestHandlerFunction onRequest, 
            ArUploadHandlerFunction onUpload = NULL, 
            ArBodyHandlerFunction onBody = NULL
        ): _matcher(matcher), _onRequest(onRequest), _onUpload(onUpload), _onBody(onBody) {}

        bool canHandle(AsyncWebServerRequest *request) const override {
            return _matcher(request);
        }

        void handleRequest(AsyncWebServerRequest *request) override { if(_onRequest) _onRequest(request); }
        void handleUpload(AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) override { if(_onUpload) _onUpload(request, filename, index, data, len, final); }
        void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override { if(_onBody) _onBody(request, data, len, index, total); }
};

class McpWebServer : public AsyncWebServer{
    private:
        McpHandler *_mcp_handler;
        McpWebHandler *_mcp_post_web_handler;
        McpWebHandler *_mcp_get_web_handler;
        AsyncEventSource *_mcp_sse_handler;

        void _on_post_mcp_request_callback(AsyncWebServerRequest *request);

        /// @brief Callback whene a request with a body is recieved
        /// @param request 
        /// @param filename 
        /// @param index 
        /// @param data 
        /// @param len 
        /// @param final 
        void _on_post_mcp_upload_callback(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);

        /// @brief Callback whene a request with a body is recieved
        /// @param request The request recieved
        /// @param data Request data
        /// @param len request expected lenght
        /// @param index index of the body chunk
        /// @param total Reuqest total length
        void _on_post_mcp_request_body_callback(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total);

        /// @brief Dispay the body of the recieved request
        /// @param body The request body
        /// @param len The request expected length
        /// @param total The request total legth
        void _display_body(String * body, size_t len, size_t total);

        /// @brief Handler to handle the Get on the /mcp request of the CLI
        /// @param request The request recieved
        /// @return Is the request match
        bool _post_handler_matcher(AsyncWebServerRequest* request);

        /// @brief respond to a GET recieved in the MCP endpoint to create a SSE connexion
        /// @param request request recieved
        void _send_sse_flux(AsyncEventSourceClient *client);

        /// @brief Handler to handle the Get on the /mcp request of the CLI
        /// @param request The request recieved
        /// @return Is the request match 
        bool _get_handler_matcher(AsyncWebServerRequest* request);


    public:
        McpWebServer(uint16_t port);

        /// @brief Get the mcp_handler linked to the server
        /// @return A pointer to the mcp_handler
        McpHandler* get_mcp_handler();
};

#endif