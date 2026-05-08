#include "mcp_tools.h"
#include <ArduinoJson.h>
#include <Arduino.h>

McpTool::McpTool(const char* name, const char* title, const char* description):
    _name(name), _title(title), _description(description){}

void McpTool::build_schema(JsonObject &tool) const{
    tool["name"] = _name;
    tool["title"] = _title;
    tool["description"] = _description;
    JsonObject schema = tool["inputSchema"].to<JsonObject>();

    schema["type"] = "object";
    JsonObject props = schema["properties"].to<JsonObject>();
    add_schema_prop(schema);
}

void McpTool::add_parameter(JsonObject &schema, const char* param_name, const char* param_type,const char* description,bool required) const{
    JsonObject props =  schema["properties"];

    JsonObject param = props[param_name].to<JsonObject>();
    param["type"] = _get_param_type(param_type);
    param["description"] = description;

    if(required){
        JsonArray required_params = schema["required"].isNull() ?
                                    schema["required"].to<JsonArray>() :
                                    schema["required"].as<JsonArray>();

        required_params.add(param_name);
    }
}
//////////// RETRIEVAL FUNCS /////////////////

const char* McpTool::_get_param_type(const char* str_param_type) const{
    if(str_param_type == "bool"){
        return "boolean";
    }
    else if (str_param_type == "str"){
        return "string";
    }
    else if (str_param_type == "int")
    {
        return "integer";
    }
    else{
        return "";
    }
}

const char* McpTool::get_name(){
    return _name;
}

const char* McpTool::get_title(){
    return _title;
}

const char* McpTool::get_description(){
    return _description;
}




