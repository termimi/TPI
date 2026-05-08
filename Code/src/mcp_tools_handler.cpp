/*
    Contains the tools logic of the mcp server (creation, listing, and calling)
*/
#include "mcp_tools_handler.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include "mcp_tools.h"

McpToolHandler::McpToolHandler(int reg_size) : _reg_size(reg_size){
    _tools_registry.reserve(_reg_size);
}

void McpToolHandler::add_tool(McpTool *tool){
    try
    {
        Serial.println("Adding tool");
        _tools_registry.push_back(tool);
        return;
    }
    catch(const std::exception& e)
    {
        throw std::runtime_error("An error occurred while adding tool to the registry, Error : " + std::string(e.what()));
    }
}

void McpToolHandler::retrieve_tools_list_json(JsonDocument &awnser){
    // Adding tools to the array
    JsonArray tools_array = awnser["result"]["tools"].to<JsonArray>();
    for(int i = 0; i < _tools_registry.size(); i++){

        JsonObject tool_schema = tools_array.add<JsonObject>();
        _tools_registry[i]->build_schema(tool_schema);
    }
}

bool McpToolHandler::call_tool(JsonObject &mcp_params, JsonArray &result){
    String tools_name = mcp_params["name"].as<String>();

    for(int i = 0; i < _tools_registry.size(); i++){
        
        if(tools_name == String(_tools_registry[i]->get_name())){
            Serial.println("calling tool : " + String(tools_name));
            String error;
            return _tools_registry[i]->execute(mcp_params["arguments"],result,error);
        }
    }
    return false;
}

