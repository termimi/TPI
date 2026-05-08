#include <Arduino.h>
#include <M5Unified.h>
#include <AsyncTCP.h>
#include <WiFi.h>
#include <ESPAsyncWiFiManager.h>
#include <SD.h>
#include <SPI.h>

#include "mcp_web_server.h"
#include "mcp_tools_handler.h"
#include "mcp_handler.h"
#include "mcp_tools.h"

#define SD_SPI_CS_PIN    4
#define SD_SPI_SCK_PIN  36
#define SD_SPI_MISO_PIN 35
#define SD_SPI_MOSI_PIN 37

///////////// Vars /////////////
McpWebServer mcp_web_server(80);
uint16_t number_of_tools = 2;
unsigned long lastDiagnosticTime = 0;
DNSServer dns;

void printMemoryInfo();
bool save_json_info__to_sd(const char* path, JsonDocument& doc);

///////////// tools /////////////
class SerialTool : public McpTool{
  protected:
    void add_schema_prop(JsonObject &schema) const override{
      char desc[80];
      snprintf(desc, sizeof(desc), "Word or sentence to print in the serial monitor");
      Serial.println("Description :");
      Serial.println(String(desc));
      add_parameter(schema,"Text","str",desc);
    }
  public:
    SerialTool() : McpTool("Serial", "Serial Interface Tool","A tool to interact with the serial monitor of the ESP32"){}

    bool execute(const JsonVariantConst recieved_args, JsonArray& result, String& error) const override{
      String value;
      get_mcp_param<String>(recieved_args,"Text",value,error);

      Serial.println("Outil Serial : " + value);
      JsonObject result_value = result.add<JsonObject>(); 
  
      result_value["type"] = "text";
      result_value["text"] = "OK : " + value;

      Serial.println("Tool Execution result array : ");
      serializeJson(result, Serial); // Envoie directement le JSON sur le port série
      Serial.println();

      return true;
    }
};


class SystemInfoTool : public McpTool {
  private:
    void add_schema_prop(JsonObject &schema) const override{

    }
  public:
    SystemInfoTool() : McpTool("GetSystemInfo", "Diagnostic system", "Retrieves the status of RAM and Flash") {}

    bool execute(const JsonVariantConst recieved_args, JsonArray& result, String& error) const override {
      JsonObject res = result.add<JsonObject>();
      res["type"] = "text";
      
      char buffer[150];
      snprintf(buffer, sizeof(buffer), 
               "RAM Libre: %u bytes, Min Libre: %u bytes, Flash utilisée: %u bytes", 
               ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getSketchSize());
      
      res["text"] = String(buffer);
      return true;
    }
};


void setup() {
  ///////// Config M5 /////////
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Power.Axp2101.begin();
  M5.Power.setUsbOutput(true);
  
  ///////// Serial Config /////////
  Serial.begin(115200);
  delay(10000);

  ///////// Wifi config /////////
  Serial.println("--- WiFi Config ---");
  AsyncWebServer temp_server(80);
  AsyncWiFiManager wifi_manager(&temp_server,&dns);

  Serial.println("--- WiFi Manager Launching ---");
  // removes the logs so the AP does not crash
  wifi_manager.setDebugOutput(false);

  if (!wifi_manager.autoConnect("mcp_esp", "rootrootroot")) {
    Serial.println("Connexion failed, restarting ESP");
    delay(3000);
    ESP.restart();
  }

  ///////// SD Card config /////////
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    Serial.println("Erreur : Carte SD non détectée !");
  } else {
    Serial.println("Carte SD initialisée avec succès.");
  }

  ///////// MCP Config /////////
  // Get the mcp_handler
  McpHandler *mcp_handler = mcp_web_server.get_mcp_handler();

  // tools declaration
  McpToolHandler* mcp_tool_handler = mcp_handler->create_tool_handler(number_of_tools);
  SerialTool *serial_tool = new SerialTool();
  SystemInfoTool *system_tool = new SystemInfoTool();
  mcp_tool_handler->add_tool(serial_tool);
  mcp_tool_handler->add_tool(system_tool);

  // start asyncWebServer
  mcp_web_server.begin();

  Serial.println("Server ready !");

  Serial.println("Test Writing in SD card");
  JsonDocument doc;
  doc["device"] = "M5Stack Core S3 SE";
  doc["uptime"] = millis();
  doc["free_heap"] = ESP.getFreeHeap();
  if (save_json_info__to_sd("/log.json", doc)) {
    Serial.println("SD card writed");
  } else {
    Serial.println("Writing in SD card failed");
  }
}

void loop() {
  //M5.update() handle buttons & alim
  M5.update(); 

  if (millis() - lastDiagnosticTime > 10000) {
    printMemoryInfo();
    lastDiagnosticTime = millis();
    Serial.println(WiFi.localIP());
  }
}


void printMemoryInfo() {
  Serial.println("\n--- DIAGNOSTIC MÉMOIRE ---");
  
  // RAM (Heap)
  uint32_t totalHeap = ESP.getHeapSize();
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t minFreeHeap = ESP.getMinFreeHeap();
  
  Serial.printf("RAM Totale : %u octets\n", totalHeap);
  Serial.printf("RAM Libre  : %u octets (%.2f%% libre)\n", freeHeap, (float)freeHeap / totalHeap * 100);
  Serial.printf("Point critique (Min Free) : %u octets\n", minFreeHeap);

  // FLASH
  uint32_t flashSize = ESP.getFlashChipSize();
  uint32_t sketchSize = ESP.getSketchSize();
  
  Serial.printf("Flash Totale : %u octets\n", flashSize);
  Serial.printf("Programme    : %u octets\n", sketchSize);
  Serial.printf("Espace libre : %u octets\n", ESP.getFreeSketchSpace());
  Serial.println("--------------------------\n");
}

bool save_json_info__to_sd(const char* path, JsonDocument& doc){
  File file = SD.open(path, FILE_WRITE);
  
  if (!file) {
    Serial.println("Cannot open the file to write in it");
    return false;
  }

  if (serializeJson(doc, file) == 0) {
    Serial.println("Échec de l'écriture du JSON");
    file.close();
    return false;
  }

  file.close();
  Serial.printf("Fichier %s enregistré !\n", path);
  return true;
}