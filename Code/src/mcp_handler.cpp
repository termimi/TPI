/*
    Contains the logic of the responses that the mcp server has to return, it does not contains any tool or anything else
*/

#include <ArduinoJson.h>
#include <Arduino.h>
#include "mcp_handler.h"
#include "mcp_tools_handler.h"

/////////////////////////// MAIN CODE /////////////////////////////////
McpHandler::McpHandler(JsonDocument &json_request, String *awnser) : 
    _json_request(json_request), _awnser(awnser) {}

void McpHandler::handle(){
    // Initialize response
    _mcp_recieved_code = McpHandler::_get_mcp_request_type(); 

    Serial.printf("recieved code %d", _mcp_recieved_code);
    switch (_mcp_recieved_code)
    {
    case 0:
        _init_respond();
        break;
    case 1:
        _mcp_handle_recieved_request();
        break;
    case 2:
        // MCP requires to send 202 with no body, so we can skip this part
        break;
    case 3:
        // _response_respond();
        break;
    default:
        // _error_respond();
        break;
    }
}

////////// MCP RESPONSE /////////

void McpHandler::_init_respond(){
    // get id of conv
    int cli_id = _json_request["id"];
    const char* protocol_version = _json_request["params"]["protocolVersion"];

    // create response
    JsonDocument doc;
    doc["jsonrpc"] = "2.0";
    doc["id"] = cli_id;

    // Principal nested objects
    JsonObject json_result = doc["result"].to<JsonObject>();

    // nested json objects of result object
    JsonObject json_capabilities = json_result["capabilities"].to<JsonObject>();
    JsonObject json_resources = json_result["resources"].to<JsonObject>();
    JsonObject json_tools = json_capabilities["tools"].to<JsonObject>();
    JsonObject json_server_info = json_result["serverInfo"].to<JsonObject>();

    json_result["protocolVersion"] = protocol_version;
    json_tools["listChanged"] = false;
    
    //nested json objects of serverInfo object
    json_server_info["name"] = "MCP_ESP";
    json_server_info["version"] = "1.0.0";
    serializeJson(doc,*_awnser);
}

int McpHandler::_get_mcp_request_type(){
    // the initialize message has been recieved
    if(_json_request["method"] == "initialize"){return 0;}

    // a request has been recieved
    else if(
        !_json_request["id"].isNull() && 
        _json_request["method"].as<String>()
    ){return 1;}

    // a notification has been recieved
    else if(
        _json_request["method"].as<String>().startsWith("notifications/")
    ){return 2;}

    // a response has been recieved
    else if(
        _json_request["id"].as<String>() && 
        !_json_request["method"].as<String>() &&
        _json_request["result"].as<String>()
    ){return 3;}

    // no primitive has been recieved
    return -1;
}

void McpHandler::create_mcp_response_headrer(AsyncWebServerResponse *mcp_response){
    switch (_mcp_recieved_code){
        case 0: {
            // Initialize header
            _session_id = String(millis()).c_str();
            mcp_response->addHeader("Mcp-Session-Id",_session_id);
            mcp_response->setCode(200);
            mcp_response->setContentType("application/json");
            mcp_response->addHeader("connection", "keep-alive");
            break;
        }
        case 1:
            if(_is_request_call_tool){
                mcp_response->setContentType("text/event-stream");
            }
            break;
        case 2:
            mcp_response->setCode(202);
            break;
        default:
            // _error_respond();
            break;
    }
}

McpToolHandler* McpHandler::create_tool_handler(uint16_t registry_size){
    _mcp_tools_handler = new McpToolHandler(registry_size);
    return _mcp_tools_handler;
}

void McpHandler::_mcp_handle_recieved_request(){
    if(_json_request["method"].isNull()) return;

    if(_json_request["method"] == "tools/list"){
        Serial.println("Tools list request recieved");
        JsonDocument doc;
        doc["jsonrpc"] = "2.0";
        doc["id"] = _json_request["id"].as<uint8_t>();

        _mcp_tools_handler->retrieve_tools_list_json(doc);

        serializeJson(doc,*_awnser);

        Serial.println("Tool list awnser : ");
        Serial.println(*_awnser);
    }
    else if(_json_request["method"] == "tools/call"){
        _is_request_call_tool = true;
        Serial.println("Tools call request recieved");
        
        return;
    }
}

bool McpHandler::get_is_request_call(){
    return _is_request_call_tool;
}

const char* McpHandler::get_session_id() const{
    return _session_id;
}

void McpHandler::execute_tool(){
    if(_is_request_call_tool){
        JsonDocument doc;
        doc["jsonrpc"] = "2.0";
        doc["id"] = _json_request["id"].as<uint8_t>();

        JsonObject result = doc["result"].to<JsonObject>();
        JsonArray content = doc["result"]["content"].to<JsonArray>();
        JsonObject tool = _json_request["params"];

        Serial.println();
        bool call_result = _mcp_tools_handler->call_tool(tool,content);

        if(call_result){
            result["isError"] = false;
        }
        else{
            result["isError"] = true;
        }
        serializeJson(doc,*_awnser);
        _is_request_call_tool = false;
    }
    return;
}