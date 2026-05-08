#ifndef MCP_TOOLS_H
#define MCP_TOOLS_H
#include <vector>
#include <ArduinoJson.h>
#include <Arduino.h>

class McpTool
{
    private:
        const char* _name;
        const char* _title;
        const char* _description;

        /// @brief returns the mcp param type given a c++ type in a string
        /// @param str_param_type the type of the param in string (ex : bool)
        /// @return the mcp param type (ex : boolean)
        const char* _get_param_type(const char* str_param_type) const;

    protected:
        virtual void add_schema_prop(JsonObject &schema) const = 0;
        void add_parameter(JsonObject &schema, const char* param_name, const char* param_type, const char* description, bool required = true) const;

        /// @brief Convert the recieved parameter by the MCP cli to a c++ type
        /// @tparam T Type to convert
        /// @param args Json args ton convert
        /// @param name Name of the param that need to be converted
        /// @param value Reference to the value that will be used after conversion
        /// @param error String containg error if something went wrong
        /// @return true = Scucess; Flase = Error
        template <typename T>
        bool get_mcp_param(const JsonVariantConst args, const char* name, T &value, String& error) const;

    public:
        virtual ~McpTool() {}  // Virtual destructor for proper cleanup
        McpTool(const char* name, const char* title, const char* description);

        const char* get_name();
        const char* get_title();
        const char* get_description();

        virtual void build_schema(JsonObject &tool) const;

        virtual bool execute(const JsonVariantConst recieved_args, JsonArray& result, String& error) const = 0;
};

/////// Template code for the linker ///////

template <typename T>
bool McpTool::get_mcp_param(const JsonVariantConst args,const char* name, T &value, String& error) const {
    if(!args.is<JsonObjectConst>()){
        error = "arguments_must_be_object";
        return false;
    }

    JsonObjectConst recieved_obj = args.as<JsonObjectConst>();
    if(recieved_obj[name].isNull()){
        error = String("missing_argument_") + name;
        return false;
    }

    value = recieved_obj[name].as<T>();
    return true;
}
#endif