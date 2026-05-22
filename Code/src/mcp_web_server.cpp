#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include "mcp_web_server.h"
#include "mcp_handler.h"
#include <Arduino.h>

JsonDocument json_body;
String awnser = "";

McpWebServer::McpWebServer(uint16_t port) : AsyncWebServer(port){
    //create the mcp_handler
    _mcp_handler = new McpHandler(json_body,&awnser);
    
    //create mcp_web_handlers
    _mcp_post_web_handler = new McpWebHandler(
        [this](AsyncWebServerRequest* req) { return _post_handler_matcher(req); },
        [this](AsyncWebServerRequest* req) { _on_post_mcp_request_callback(req); },
        NULL,
        [this](AsyncWebServerRequest* req, uint8_t *data, size_t len, size_t index, size_t total) { 
            _on_post_mcp_request_body_callback(req, data, len, index, total); 
        }
    );
    _mcp_sse_handler = new AsyncEventSource("/mcp");

    _mcp_sse_handler->onConnect([this](AsyncEventSourceClient *client){
        _send_sse_flux(client);
    });

    // server routes
    this->addHandler(_mcp_post_web_handler);
    this->addHandler(_mcp_sse_handler);

    this->onNotFound([](AsyncWebServerRequest *request){
        // display the request
        Serial.println("ONF : Request recieved");
        Serial.println("-------------REQUEST INFO------------");

        Serial.println("HEADERS");
        for(int x = 0; x < request->headers(); x++){
            Serial.println("header : " + request->header(x));
        }

        Serial.println(String("Method : ") + request->methodToString());
        Serial.println(String("URL : ") + request->url().c_str());

        // response to not timeout the cli
        request->send(200, "application/json", "{\"debug\":\"ok\"}");
    }); 
}

McpHandler* McpWebServer::get_mcp_handler(){
    return _mcp_handler;
}
//////////////// WebServer matchers ////////////////

bool McpWebServer::_post_handler_matcher(AsyncWebServerRequest* request){
    if(request->url() == "/mcp" && request->method() == HTTP_POST){
        return true;
    }
    return false;
}

bool McpWebServer::_get_handler_matcher(AsyncWebServerRequest* request){
    if(request->url() == "/mcp" && request->method() == HTTP_GET){
        return true;
    }
    return false;
}

//////////////// POST requests callbacks ////////////////

void McpWebServer::_on_post_mcp_request_callback(AsyncWebServerRequest *request)
{
    Serial.println("POST : Request recieved");
    Serial.println("--- REQUEST INFO ---");

    for(int x = 0; x < request->headers(); x++){
        Serial.println("header : " + request->header(x));
    }

    if (request->contentLength() > 1024 * 1024)
    {
        Serial.println("POST : to big request");
        request->send(401, "text/plain", "Query To Big");
        request->_tempObject = (void *)1;
        return;
    }
}
void McpWebServer::_on_post_mcp_upload_callback(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    // Ce code s'exécute si le serveur croit recevoir un fichier
    // On peut juste logger pour voir si le CLI Gemini tombe ici par erreur
    if (index == 0) {
        Serial.println("Upload : Début de réception flux...");
    }
}
void McpWebServer::_on_post_mcp_request_body_callback(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total)
{
    if (request->_tempObject == (void *)1)
    {
        // Ignore the request body if _tempObject == 1
        return;
    }

    // Handle body
    static String accumulatedBody = "";
    for (size_t i = 0; i < len; i++)
    {
        accumulatedBody += (char)data[i];
    }

    if (index + len == total)
    {
        // displays in serial
        this->_display_body(&accumulatedBody, len, total);

        // json deserialize
        deserializeJson(json_body,accumulatedBody);
        accumulatedBody = "";

        // handle response with the request
        _mcp_handler->handle();

        // Checks if the last recieved request is a tool call
        if(_mcp_handler->get_is_request_call()){
            // if so calls the tool and send the response via SSE
            _mcp_handler->execute_tools();
            _mcp_sse_handler->send(awnser.c_str(), "message");
        }

        AsyncWebServerResponse *mcp_response = request->beginResponse(200, "application/json",awnser);
        _mcp_handler->create_mcp_response_headrer(mcp_response);

        request->send(mcp_response);


        Serial.println("--- REQUEST RESPONSE ---");
        Serial.println(awnser);

        // Clears the vars to gain space
        awnser = "";
        json_body.clear();
    }
}

//////////////// SSE on connect callback ////////////////

void McpWebServer::_send_sse_flux(AsyncEventSourceClient *client)
{
    Serial.println("--- SSE : Nouveau client connecté ---");
        
    // On récupère l'IP pour le log
    Serial.printf("IP: %s\n", client->client()->remoteIP().toString().c_str());

    // --- ENVOI DU MESSAGE D'AMORÇAGE (PRIMING) ---
    // Syntaxe : client->send(message, event, id, reconnect)
    // - message : "" (data vide)
    // - event   : NULL (ou "message" par défaut)
    // - id      : 1 (un ID de départ pour initialiser le Last-Event-ID côté client)
    String endpointUrl = "/mcp?sessionId=" + String(_mcp_handler->get_session_id());
    client->send(endpointUrl.c_str(), "endpoint");

    Serial.println("Flux amorcé : ID 1 envoyé avec data vide.");
}

//////////////// Other funcs ////////////////

void McpWebServer::_display_body(String * body, size_t len, size_t total){
    Serial.println("--- New body recieved ---");
    Serial.print("content : ");
    Serial.println(*body);
    Serial.printf("Length expected : %u bytes, Total got : %u\n", len, total);
}

