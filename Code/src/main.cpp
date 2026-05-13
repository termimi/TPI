#include <Arduino.h>
#include <M5Unified.h>
#include <AsyncTCP.h>
#include <WiFi.h>
#include <ESPAsyncWiFiManager.h>
#include <SD.h>
#include <SPI.h>
#include <Unit_Sonic.h>
#include <esp_sntp.h>

#include "mcp_web_server.h"
#include "mcp_tools_handler.h"
#include "mcp_handler.h"
#include "mcp_tools.h"
#include "M5_STHS34PF80.h"

#define SD_SPI_CS_PIN    4
#define SD_SPI_SCK_PIN  36
#define SD_SPI_MISO_PIN 35
#define SD_SPI_MOSI_PIN 37

#define SWISS_TZ "CET-1CEST,M3.5.0,M10.5.0/3"

///////////// Const /////////////
SONIC_I2C ULTRASONIC_SENSOR;
M5_STHS34PF80 TMOS;

///////////// Vars /////////////
//MCP
McpWebServer mcp_web_server(80);
uint16_t number_of_tools = 2;
DNSServer dns;
// PIR TMOS
int16_t motionVal = 0, presenceVal = 0;
//Other
unsigned long lastDiagnosticTime = 0;

///////////// Funcs /////////////
void printMemoryInfo();
bool save_json_info__to_sd(const char* path, JsonDocument& doc);
void display_animation_function(LovyanGFX* gfx);
void emit_sound(m5::Speaker_Class* speaker);

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

  // Find the sda pin for the current M5 board
  int8_t sda = M5.getPin(m5::pin_name_t::port_a_sda);
  // Find the scl pin for the current M5 board
  int8_t scl = M5.getPin(m5::pin_name_t::port_a_scl);
  
  ///////// Serial Config /////////
  Serial.begin(115200);
  delay(10000);

  ///////// Screen Config /////////
  int textsize = M5.Display.height() / 130;
  if (textsize == 0) {
    textsize = 1;
  }
  M5.Display.setTextSize(textsize);

  ///////// Wifi config /////////
  Serial.println("--- WiFi Config ---");
  M5.Display.println("--- WiFi Config ---");
  AsyncWebServer temp_server(80);
  AsyncWiFiManager wifi_manager(&temp_server,&dns);

  Serial.println("--- WiFi Manager Launching ---");
  M5.Display.println("--- WiFi Manager Launching ---");
  // removes the logs so the AP does not crash
  wifi_manager.setDebugOutput(false);
  wifi_manager.setConfigPortalTimeout(20);
  wifi_manager.autoConnect("mcp_esp", "rootrootroot");

  ///////// SD Card config /////////
  Serial.println("Init SD card");
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000)) {
    Serial.println("Error: SD card not detected");
    M5.Display.println("Error: SD card not detected");
  } 
  else {
    Serial.println("SD card initialized successfully.");
    M5.Display.println("SD card initialized successfully.");
  }

  ///////// Ultrasonic sensor config /////////
  Serial.println("Init ultra sonic sensor");
  M5.Display.println("Init ultra sonic sensor");
  Wire.begin(sda,scl);
  ULTRASONIC_SENSOR.begin();

  ///////// PIR sensor config /////////
  Serial.println("Init PIR TEMOS sensor");
  M5.Display.println("Init PIR TEMOS sensor");
  if (TMOS.begin(&Wire, STHS34PF80_I2C_ADDRESS, sda, scl) == false) {
    Serial.println("Error : TMOS sensor not found on the port A");
    M5.Display.println("Error : TMOS sensor not found on the port A");
  } else {
    // Set Mode to wide to avoid saturation
    TMOS.setGainMode(STHS34PF80_GAIN_DEFAULT_MODE);
    TMOS.setTmosSensitivity(0xff);

    // Set TMOS frequency
    TMOS.setTmosODR(STHS34PF80_TMOS_ODR_OFF);
    TMOS.setTmosODR(STHS34PF80_TMOS_ODR_AT_15Hz);

    // Set TMOS Thresholds and Hystersis to avoid false positive
    TMOS.setMotionThreshold(0xFF);
    TMOS.setPresenceThreshold(0x258);
    TMOS.setPresenceHysteresis(0x32);
    TMOS.setMotionHysteresis(0x0);

    // Reset algo to applay changes (asked in doc)
    TMOS.resetAlgo();
    Serial.println("TMOS Sensor is ready");
    M5.Display.println("TMOS Sensor is ready");
  }
  ///////// RTC config /////////
  Serial.print("RTC Config");
  M5.Display.println("RTC Config");

  // Get date with SNTP if WiFi is connected
  configTzTime(SWISS_TZ, "ch.pool.ntp.org");
  if(WiFi.status() == WL_CONNECTED){
    int retry = 0;
    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && retry < 20) {
      Serial.print('.');
      delay(500);
      retry++;
    }

    Serial.println();
    struct tm time_info;

    if (getLocalTime(&time_info)) {
      // Convert local time to timestamp
      time_t time = mktime(&time_info);
      // Convert timestamp to greenwich mean time
      M5.Rtc.setDateTime(gmtime(&time));
      Serial.println("RTC updated via NTP");
      M5.Display.println("RTC updated via NTP");
    }
    else{
      Serial.println("Could not get local time, RTC Update failed");
      M5.Display.println("Could not get local time, RTC Update failed");
    }
  }
  else{
    Serial.println("WARNING : WiFi not connected, NTP configuration cannot be done");
    M5.Display.println("WARNING : WiFi not connected, NTP configuration cannot be done");
    auto dt = M5.Rtc.getDateTime();
      
    // Convert RTC info to internal clock
    struct tm rtc_time_info = {0};
    rtc_time_info.tm_year = dt.date.year - 1900;
    rtc_time_info.tm_mon  = dt.date.month - 1;
    rtc_time_info.tm_mday = dt.date.date;
    rtc_time_info.tm_hour = dt.time.hours;
    rtc_time_info.tm_min  = dt.time.minutes;
    rtc_time_info.tm_sec  = dt.time.seconds;

    // set correct time into the internal clock
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t timestamp = mktime(&rtc_time_info);
    setenv("TZ", SWISS_TZ, 1);
    tzset();
    struct timeval now = { .tv_sec = timestamp };
    settimeofday(&now, NULL); 
    
    Serial.println("Time configured with the RTC module");
    M5.Display.println("Time configured with the RTC module");
  }
  static constexpr const char* const wd[7] = {"Sun", "Mon", "Tue", "Wed",
                                                "Thr", "Fri", "Sat"};

  time_t now = time(nullptr);
  struct tm* local_tm = localtime(&now);
  Serial.printf("LOCAL TIME  :%04d/%02d/%02d (%s)  %02d:%02d:%02d\r\n",
                local_tm->tm_year + 1900, local_tm->tm_mon + 1, local_tm->tm_mday,
                wd[local_tm->tm_wday], local_tm->tm_hour, local_tm->tm_min, local_tm->tm_sec
  );

  Serial.println("");

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
  M5.Display.println("Server ready !");

  Serial.println("Test Writing in SD card");
  M5.Display.println("Test Writing in SD card");

  JsonDocument doc;
  doc["device"] = "M5Stack Core S3 SE";
  doc["uptime"] = millis();
  doc["free_heap"] = ESP.getFreeHeap();
  if (save_json_info__to_sd("/log.json", doc)) {
    Serial.println("SD card writed");
    M5.Display.println("SD card writed");
  } else {
    Serial.println("Writing in SD card failed");
    M5.Display.println("Writing in SD card failed");
  }
}

void loop() {
  //M5.update() handle buttons & alim
  M5.update();
  M5.Display.clear();

  // Prepare TMOS data
  sths34pf80_tmos_drdy_status_t dataReady;
  TMOS.getDataReady(&dataReady);

  if (dataReady.drdy == 1) {
    sths34pf80_tmos_func_status_t status;
    TMOS.getPresenceValue(&presenceVal);
    TMOS.getMotionValue(&motionVal);
    TMOS.getStatus(&status);

    if(status.mot_flag == 1 && status.pres_flag == 1 && motionVal > 300){
      Serial.print("Organic presence and motion detected");
      Serial.printf("PrescenceValue:%d\n", presenceVal);
      Serial.printf("MotionValue:%d\n", motionVal); 
    }
  }

  static unsigned long last_distance_check = 0;
  if (millis() - last_distance_check > 2000) {
    last_distance_check = millis();
    Serial.printf("%.2fmm", ULTRASONIC_SENSOR.getDistance());
    Serial.println("");
  }

  static unsigned long last_sound = 0;
  if (millis() - last_sound > 3000) {
    emit_sound(&M5.Speaker);
    last_sound = millis();
  }

  static unsigned long last_diagnostic = 0;
  if (millis() - last_diagnostic > 10000) {
    printMemoryInfo();
    last_diagnostic = millis();
    Serial.println(WiFi.localIP());
  }

  int x      = rand() % M5.Display.width();
  int y      = rand() % M5.Display.height();
  int r      = (M5.Display.width() >> 4) + 2;
  uint16_t c = rand();
  M5.Display.fillCircle(x, y, r, c);
  display_animation_function(&M5.Display);
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

void display_animation_function(LovyanGFX* gfx) {
  int textsize = M5.Display.height() / 60;
  if (textsize == 0) {
    textsize = 1;
  }
  M5.Display.setTextSize(textsize);
  int x      = rand() % gfx->width();
  int y      = rand() % gfx->height();
  int r      = (gfx->width() >> 4) + 2;
  uint16_t c = rand();
  gfx->fillRect(x - r, y - r, r * 2, r * 2, c);
}
void emit_sound(m5::Speaker_Class* speaker){
    speaker->tone(4000, 100);
}