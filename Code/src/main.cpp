#include <Arduino.h>
#include <AsyncTCP.h>
#include <WiFi.h>
#include <ESPAsyncWiFiManager.h>
#include <SD.h>
#include <M5Unified.h>
#include <SPI.h>
#include <Unit_Sonic.h>
#include <esp_sntp.h>
#include <atomic>
#include <ESPmDNS.h>

#include "mcp_web_server.h"
#include "mcp_tools_handler.h"
#include "mcp_handler.h"
#include "mcp_tools.h"
#include "M5_STHS34PF80.h"

#define SD_SPI_CS_PIN    4
#define SD_SPI_SCK_PIN  36
#define SD_SPI_MISO_PIN 35
#define SD_SPI_MOSI_PIN 37

#define SWISS_TZ     "CET-1CEST,M3.5.0,M10.5.0/3"
#define WAV_FILENAME "/miaou.wav"
#define JSON_FILE    "/data.json"

///////////// Const /////////////
SONIC_I2C ULTRASONIC_SENSOR;
M5_STHS34PF80 TMOS;

///////////// Vars /////////////
// MCP
McpWebServer mcp_web_server(80);
uint16_t number_of_tools = 5;
DNSServer dns;

// MCP Tools
std::atomic<bool> trigger_play_sound{false};
std::atomic<bool> trigger_animation {false};

// PIR TMOS
int16_t motionVal   = 0;
int16_t presenceVal = 0;

// Cat Crossings
float empty_ultrasonic_distance = 0; // distance when the sensor is in the rest position
float post_ultrasonic_distance  = 0; // distance after the passage
uint8_t distance_threshold      = 10;
bool has_passed                 = false; // Has something passed the TMOS sensor ?
// Other
unsigned long lastDiagnosticTime = 0;

///////////// Funcs /////////////
void printMemoryInfo();
bool save_json_info__to_sd(const char *path, JsonDocument &doc);
JsonDocument load_json_from_sd(const char *path);
void display_animation_function(LovyanGFX *gfx);
bool play_wav_from_sd(uint32_t repeat = 1, int channel = -1, bool stop_current = true);
bool play_wav_memory(File& wavFile, size_t fileSize, uint32_t repeat, int channel, bool stop_current);
bool play_wav_segmented(const char *filename, uint32_t repeat, int channel, bool stop_current);

///////////// tools /////////////

/// @brief MCP Tool to write something in the serial monitor
class SerialTool : public McpTool
{
protected:
  void add_schema_prop(JsonObject &schema) const override
  {
    char desc[80];
    snprintf(desc, sizeof(desc), "Word or sentence to print in the serial monitor");
    Serial.println("Description :");
    Serial.println(String(desc));
    add_parameter(schema, "Text", "str", desc);
  }

public:
  SerialTool() : McpTool("Serial", "Serial Interface Tool", "A tool to interact with the serial monitor of the ESP32") {}

  bool execute(const JsonVariantConst recieved_args, JsonArray &result, String &error) const override
  {
    String value;
    get_mcp_param<String>(recieved_args, "Text", value, error);

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

/// @brief MCP Tool to retrieve a hardware diagnostic
class SystemInfoTool : public McpTool
{
private:
  void add_schema_prop(JsonObject &schema) const override
  {
  }

public:
  SystemInfoTool() : McpTool("GetSystemInfo", "Diagnostic system", "Retrieves the status of RAM and Flash") {}

  bool execute(const JsonVariantConst recieved_args, JsonArray &result, String &error) const override
  {
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

/// @brief MCP Tool to retrieve all the cats crossings 
class CatPassagesToolInfo : public McpTool
{
private:
  void add_schema_prop(JsonObject &schema) const override
  {
  }

public:
  CatPassagesToolInfo() : McpTool("GetCatCrossings", "Get cat crossings", "retrieves all instances of the cat crossings in front of the device in json formats, date formats : d.m.Y:H:i") {}

  bool execute(const JsonVariantConst recieved_args, JsonArray &result, String &error) const override
  {
    JsonObject res = result.add<JsonObject>();
    res["type"] = "text";
    JsonDocument doc = load_json_from_sd("/data.json");
    String jsonString;
    serializeJson(doc, jsonString);
    res["text"] = jsonString;
    return true;
  }
};

/// @brief MCP Tool that launches the miaou.wav file that is in the SD card
class SpeakerTool : public McpTool
{
private:
  void add_schema_prop(JsonObject &schema) const override
  {
  }

public:
  SpeakerTool() : McpTool("PlaySound", "Play a sound", "Play a cat meow sound from the ESP32") {}

  bool execute(const JsonVariantConst recieved_args, JsonArray &result, String &error) const override
  {
    JsonObject res = result.add<JsonObject>();
    res["type"] = "text";
    trigger_play_sound = true;
    res["text"] = "OK";
    return true;
  }
};

/// @brief MCP Tool that launches an animation on the screen
class ScreenTool : public McpTool
{
private:
  void add_schema_prop(JsonObject &schema) const override
  {
  }

public:
  ScreenTool() : McpTool("DisplayAnimation", "Display animation", "Displays a preconfigured animation on the ESP32 screen") {}

  bool execute(const JsonVariantConst recieved_args, JsonArray &result, String &error) const override
  {
    JsonObject res = result.add<JsonObject>();
    res["type"] = "text";

    trigger_animation = true;

    res["text"] = "OK";
    return true;
  }
};

void setup()
{
  ///////// Config M5 /////////
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Power.setUsbOutput(true);

  int8_t sda = M5.getPin(m5::pin_name_t::port_a_sda); // Find the sda pin for the current M5 board
  int8_t scl = M5.getPin(m5::pin_name_t::port_a_scl); // Find the scl pin for the current M5 board

  ///////// Serial Config /////////
  Serial.begin(115200);
  delay(10000);

  ///////// Screen Config /////////
  int textsize = M5.Display.height() / 130;
  if (textsize == 0)
  {
    textsize = 1;
  }
  M5.Display.setTextSize(textsize);
  
  ///////// Speaker Config /////////
  M5.Speaker.setVolume(128);

  ///////// Wifi config /////////
  Serial.println("--- WiFi Config ---");
  M5.Display.println("--- WiFi Config ---");

  AsyncWebServer    temp_server(80);
  AsyncWiFiManager  wifi_manager(&temp_server, &dns);

  Serial.println("--- WiFi Manager Launching ---");
  M5.Display.println("--- WiFi Manager Launching ---");

  // removes the logs so the AP does not crash
  wifi_manager.setDebugOutput(false);
  wifi_manager.autoConnect("mcp_esp", "rootrootroot");

  ///////// SD Card config /////////
  Serial.println("Init SD card");
  SPI.begin(SD_SPI_SCK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
  if (!SD.begin(SD_SPI_CS_PIN, SPI, 25000000))
  {
    Serial.println("Error: SD card not detected");
    M5.Display.println("Error: SD card not detected");
  }
  else
  {
    Serial.println("SD card initialized successfully.");
    M5.Display.println("SD card initialized successfully.");
  }

  ///////// Ultrasonic sensor config /////////
  Serial.println("Init ultra sonic sensor");
  M5.Display.println("Init ultra sonic sensor");
  Wire.begin(sda, scl);
  ULTRASONIC_SENSOR.begin();

  ///////// PIR sensor config /////////
  Serial.println("Init PIR TEMOS sensor");
  M5.Display.println("Init PIR TEMOS sensor");
  if (TMOS.begin(&Wire, STHS34PF80_I2C_ADDRESS, sda, scl) == false)
  {
    Serial.println("Error : TMOS sensor not found on the port A");
    M5.Display.println("Error : TMOS sensor not found on the port A");
  }
  else
  {
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
  if (WiFi.status() == WL_CONNECTED)
  {
    int retry = 0;
    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && retry < 20)
    {
      Serial.print('.');
      delay(500);
      retry++;
    }

    Serial.println();
    struct tm time_info;

    if (getLocalTime(&time_info))
    {
      // Convert local time to timestamp
      time_t time = mktime(&time_info);
      // Convert timestamp to greenwich mean time
      M5.Rtc.setDateTime(gmtime(&time));
      Serial.println("RTC updated via NTP");
      M5.Display.println("RTC updated via NTP");
    }
    else
    {
      Serial.println("Could not get local time, RTC Update failed");
      M5.Display.println("Could not get local time, RTC Update failed");
    }
  }
  else
  {
    Serial.println("WARNING : WiFi not connected, NTP configuration cannot be done");
    M5.Display.println("WARNING : WiFi not connected, NTP configuration cannot be done");
    auto dt = M5.Rtc.getDateTime();

    // Convert RTC info to internal clock
    struct tm rtc_time_info   = {0};
    rtc_time_info.tm_year     = dt.date.year - 1900;
    rtc_time_info.tm_mon      = dt.date.month - 1;
    rtc_time_info.tm_mday     = dt.date.date;
    rtc_time_info.tm_hour     = dt.time.hours;
    rtc_time_info.tm_min      = dt.time.minutes;
    rtc_time_info.tm_sec      = dt.time.seconds;

    // set correct time into the internal clock
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t timestamp    = mktime(&rtc_time_info);

    setenv("TZ", SWISS_TZ, 1);
    tzset();
    struct timeval now  = {.tv_sec = timestamp};
    settimeofday(&now, NULL);

    Serial.println("Time configured with the RTC module");
    M5.Display.println("Time configured with the RTC module");
  }
  static constexpr const char *const wd[7] = {"Sun", "Mon", "Tue", "Wed",
                                              "Thr", "Fri", "Sat"};

  time_t now = time(nullptr);
  struct tm *local_tm = localtime(&now);
  Serial.printf("LOCAL TIME  :%04d/%02d/%02d (%s)  %02d:%02d:%02d\r\n",
                local_tm->tm_year + 1900, local_tm->tm_mon + 1, local_tm->tm_mday,
                wd[local_tm->tm_wday], local_tm->tm_hour, local_tm->tm_min, local_tm->tm_sec);

  Serial.println("");

  ///////// MCP Config /////////

  McpHandler *mcp_handler                 = mcp_web_server.get_mcp_handler();

  McpToolHandler *mcp_tool_handler        = mcp_handler->create_tool_handler(number_of_tools);
  SerialTool *serial_tool                 = new SerialTool();
  SystemInfoTool *system_tool             = new SystemInfoTool();
  ScreenTool *screen_tool                 = new ScreenTool();
  SpeakerTool *speaker_tool               = new SpeakerTool();
  CatPassagesToolInfo *cat_passages_tool  = new CatPassagesToolInfo();

  mcp_tool_handler->add_tool(serial_tool);
  mcp_tool_handler->add_tool(system_tool);
  mcp_tool_handler->add_tool(screen_tool);
  mcp_tool_handler->add_tool(speaker_tool);
  mcp_tool_handler->add_tool(cat_passages_tool);

  // start asyncWebServer
  mcp_web_server.begin();

  Serial.println("Server ready !");
  M5.Display.println("Server ready !");
}

void loop()
{
  // M5.update() handle buttons & alim
  M5.update();
  M5.Display.clear();
  M5.Display.setCursor(0, 0);
  M5.Display.println(WiFi.localIP());
  // Prepare TMOS data
  sths34pf80_tmos_drdy_status_t dataReady;
  TMOS.getDataReady(&dataReady);

  empty_ultrasonic_distance = ULTRASONIC_SENSOR.getDistance() / 10; // mm -> cm

  if (dataReady.drdy == 1)
  {
    sths34pf80_tmos_func_status_t status;
    TMOS.getPresenceValue(&presenceVal);
    TMOS.getMotionValue(&motionVal);
    TMOS.getStatus(&status);

    if (status.mot_flag == 1 && status.pres_flag == 1 && motionVal > 300)
    {
      if(!has_passed){
        Serial.print("Organic presence and motion detected");
        Serial.printf("PrescenceValue:%d\n", presenceVal);
        Serial.printf("MotionValue:%d\n", motionVal);
        int ultrasonic_distance_threshold = empty_ultrasonic_distance - (empty_ultrasonic_distance * distance_threshold / 100);

        // Infinite loop possible if the ultrasonic sensor does not get a sufficient distance
        while(ULTRASONIC_SENSOR.getDistance() / 10 >= ultrasonic_distance_threshold){}
        post_ultrasonic_distance = ULTRASONIC_SENSOR.getDistance() / 10;
        Serial.printf("%.2fcm \n", post_ultrasonic_distance);
        has_passed = true;
      }
    }
    else{has_passed = false;}
  }

  // Save crossing in the SD card
  if(post_ultrasonic_distance != 0){
    time_t now          = time(nullptr);
    struct tm *local_tm = localtime(&now);

    char date_buffer[20];
    strftime(date_buffer, sizeof(date_buffer),"%d.%m.%Y:%H:%M", local_tm);
    
    JsonDocument new_entry;
    JsonDocument doc          = load_json_from_sd(JSON_FILE);
    new_entry["date"]         = String(date_buffer);
    new_entry["distance"]     = post_ultrasonic_distance;
    post_ultrasonic_distance  = 0;

    if(!doc.is<JsonArray>()){
      doc.to<JsonArray>();
    }
    doc.add(new_entry);

    if (save_json_info__to_sd(JSON_FILE, doc))
    {
      Serial.println("SD card writed");
      M5.Display.println("SD card writed");
    }
    else
    {
      Serial.println("Writing in SD card failed");
      M5.Display.println("Writing in SD card failed");
    }
  }

  static unsigned long last_diagnostic = 0;
  if (millis() - last_diagnostic > 10000)
  {
    printMemoryInfo();
    last_diagnostic = millis();
    Serial.println(WiFi.localIP());
  }

  if(trigger_animation){
    M5.Display.clear();
    M5.Display.setCursor(0, 0);
    unsigned long start_time = millis();
    while(millis() - start_time < 10000){
      int x       = rand() % M5.Display.width();
      int y       = rand() % M5.Display.height();
      int r       = (M5.Display.width() >> 4) + 2;
      uint16_t c  = rand();

      M5.Display.fillCircle(x, y, r, c);
      display_animation_function(&M5.Display);
    }
    trigger_animation = false;
  }

  if(trigger_play_sound){
    play_wav_from_sd();
    trigger_play_sound = false;
  }
}

/// @brief Print a diagnostic result in the serial monitor
void printMemoryInfo()
{
  Serial.println("\n--- DIAGNOSTIC MÉMOIRE ---");

  // RAM (Heap)
  uint32_t totalHeap    = ESP.getHeapSize();
  uint32_t freeHeap     = ESP.getFreeHeap();
  uint32_t minFreeHeap  = ESP.getMinFreeHeap();

  Serial.printf("RAM Totale : %u octets\n", totalHeap);
  Serial.printf("RAM Libre  : %u octets (%.2f%% libre)\n", freeHeap, (float)freeHeap / totalHeap * 100);
  Serial.printf("Point critique (Min Free) : %u octets\n", minFreeHeap);

  // FLASH
  uint32_t flashSize  = ESP.getFlashChipSize();
  uint32_t sketchSize = ESP.getSketchSize();

  Serial.printf("Flash Totale : %u octets\n", flashSize);
  Serial.printf("Programme    : %u octets\n", sketchSize);
  Serial.printf("Espace libre : %u octets\n", ESP.getFreeSketchSpace());
  Serial.println("--------------------------\n");
}

/// @brief Save a json document in the SD card
/// @param path File path
/// @param doc Json document
/// @return True -> OK False -> KO
bool save_json_info__to_sd(const char *path, JsonDocument &doc)
{
  File file = SD.open(path, FILE_WRITE);

  if (!file)
  {
    Serial.println("Cannot open the file to write in it");
    return false;
  }

  if (serializeJson(doc, file) == 0)
  {
    Serial.println("Failed to write JSON");
    file.close();
    return false;
  }

  file.close();
  Serial.printf("File %s saved !\n", path);
  return true;
}

/// @brief Get the json infos from a Json file
/// @param path File path
/// @return Json document
JsonDocument load_json_from_sd(const char *path)
{
  JsonDocument doc; 

  File file = SD.open(path, FILE_READ);
  if (!file)
  {
    Serial.printf("The file %s does not yet exist (or cannot be read).\n", path);
    return doc; 
  }

  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error)
  {
    Serial.printf("JSON parsing error on %s: %s \n", path, error.c_str());
    doc.clear();
  }

  return doc;
}

/// @brief Display an animation in the screen (source : https://docs.m5stack.com/en/arduino/m5cores3/display)
/// @param gfx Display pointer
void display_animation_function(LovyanGFX *gfx)
{
  int textsize = M5.Display.height() / 60;
  if (textsize == 0)
  {
    textsize = 1;
  }
  M5.Display.setTextSize(textsize);
  int x = rand() % gfx->width();
  int y = rand() % gfx->height();
  int r = (gfx->width() >> 4) + 2;
  uint16_t c = rand();
  gfx->fillRect(x - r, y - r, r * 2, r * 2, c);
}

/// @brief Play the .wav file from the SD card
/// @param repeat number of repetitions
/// @param channel number of channels
/// @param stop_current does it have to stop the current player 
/// @return True -> OK False -> KO
bool play_wav_from_sd(uint32_t repeat, int channel, bool stop_current)
{
  File wav_file = SD.open(WAV_FILENAME);
  if (!wav_file)
  {
    Serial.print("Failed to open .wav file");
    return false;
  }

  size_t file_size = wav_file.size();
  size_t free_heap = ESP.getFreeHeap();

  Serial.printf("File size : %d \n", file_size);
  Serial.printf("Free heap : %d \n", free_heap);

  if (file_size < free_heap / 2)
  {
    return play_wav_memory(wav_file, file_size,repeat, channel, stop_current);
  }
  wav_file.close();
  return play_wav_segmented(WAV_FILENAME,repeat, channel, stop_current);
}

/// @brief Plays the .wav file entirely from the heap memory (do not use it if the file is to big)
/// @param wavFile 
/// @param fileSize 
/// @param repeat 
/// @param channel 
/// @param stop_current 
/// @return True -> OK False -> KO
bool play_wav_memory(File &wavFile, size_t fileSize, uint32_t repeat, int channel, bool stop_current)
{
  uint8_t *wavData = (uint8_t *)malloc(fileSize);
  if (!wavData)
  {
    Serial.println("Memory allocation failed!");
    wavFile.close();
    return false;
  }

  Serial.println("Loading file to memory...");
  size_t bytesRead = wavFile.read(wavData, fileSize);
  wavFile.close();

  if (bytesRead != fileSize)
  {
    Serial.printf("Read error: %d/%d bytes\n", bytesRead, fileSize);
    free(wavData);
    return false;
  }

  Serial.println("Starting playback...");
  bool result = M5.Speaker.playWav(wavData, fileSize, repeat, channel, stop_current);

  if (result)
  {
    while (M5.Speaker.isPlaying())
    {
      delay(100);
    }
    Serial.println("Playback completed!");
  }

  free(wavData);
  return result;
}

/// @brief Plays the .wav in a segmented way (to use if the file > heap memory)
/// @param filename 
/// @param repeat 
/// @param channel 
/// @param stop_current 
/// @return True -> OK False -> KO
bool play_wav_segmented(const char *filename, uint32_t repeat, int channel, bool stop_current)
{
  File wavFile = SD.open(filename, FILE_READ);
  if (!wavFile) return false;

  uint32_t sampleRate = 0, audio_start_byte = 0, dataSize = 0;
  uint16_t channels   = 0, bitsPerSample    = 0;

  // Check the 12 first bytes to see if the file is a WAV
  uint8_t riff[12];
  if (wavFile.read(riff, 12) != 12 || strncmp((char *)riff, "RIFF", 4) != 0 || strncmp((char *)riff + 8, "WAVE", 4) != 0) {
    Serial.println("Invalid WAV format");
    wavFile.close();
    return false;
  }

  // Finds the audio data in the file
  while (wavFile.available()) {
    char chunkId[4];
    uint32_t chunkSize;
    if (wavFile.read((uint8_t*)chunkId, 4)    != 4) break;
    if (wavFile.read((uint8_t*)&chunkSize, 4) != 4) break;

    if (strncmp(chunkId, "fmt ", 4) == 0) {
      uint8_t fmtData[16];
      wavFile.read(fmtData, min((uint32_t)16, chunkSize));
      channels      = *(uint16_t *)(fmtData + 2);
      sampleRate    = *(uint32_t *)(fmtData + 4);
      bitsPerSample = *(uint16_t *)(fmtData + 14);
      if (chunkSize > 16) wavFile.seek(wavFile.position() + (chunkSize - 16)); // skips every thing else
    }
    else if (strncmp(chunkId, "data", 4) == 0) {
      // Audio has been found
      audio_start_byte  = wavFile.position();
      dataSize          = chunkSize;
      break; 
    }
    else {
      wavFile.seek(wavFile.position() + chunkSize); // Ignores unknown chunks
    }
  }

  if (audio_start_byte == 0 || channels == 0) {
    Serial.println("Error : fmt or data chunk not found");
    wavFile.close();
    return false;
  }

  Serial.printf("WAV: %dHz, %dch, %dbit, Data offset: %d\n", sampleRate, channels, bitsPerSample, audio_start_byte);

  // Make sure the sampples are the right size
  size_t bytesPerSample     = (bitsPerSample / 8) * channels;
  size_t chunks_in_samples  = 16384 / bytesPerSample; // 16384 = 16ko wich is good for the Core S3 SE
  size_t chunkSize          = chunks_in_samples * bytesPerSample; // total size of a complete chunk

  uint8_t *chunkBuffer    = nullptr;
  size_t actualChunkSize  = chunkSize;

  // Tries to allocate the the complete chunksize + 44 bytes (wav header)
  while (actualChunkSize >= 4096 && !chunkBuffer) {
    chunkBuffer = (uint8_t *)malloc(actualChunkSize + 44);
    if (!chunkBuffer) {
      actualChunkSize   /= 2;
      chunks_in_samples = actualChunkSize / bytesPerSample;
      actualChunkSize   = chunks_in_samples * bytesPerSample;
    }
  }

  if (!chunkBuffer) {
    Serial.println("Buffer allocation failed!");
    wavFile.close();
    return false;
  }

  // Builds a wav header
  uint8_t pristineHeader[44] = {
    'R', 'I', 'F', 'F',
    0, 0, 0, 0, // ChunkSize (à remplir plus bas)
    'W', 'A', 'V', 'E',
    'f', 'm', 't', ' ',
    16, 0, 0, 0, // Subchunk1Size
    1, 0,        // AudioFormat (PCM)
    (uint8_t)channels, (uint8_t)(channels >> 8),
    (uint8_t)sampleRate, (uint8_t)(sampleRate >> 8), (uint8_t)(sampleRate >> 16), (uint8_t)(sampleRate >> 24),
    0, 0, 0, 0,  // ByteRate
    0, 0,        // BlockAlign
    (uint8_t)bitsPerSample, (uint8_t)(bitsPerSample >> 8),
    'd', 'a', 't', 'a',
    0, 0, 0, 0   // to complete in the code under
  };

  uint32_t byteRate   = sampleRate * channels * (bitsPerSample / 8);
  uint16_t blockAlign = channels * (bitsPerSample / 8);
  memcpy(pristineHeader + 28, &byteRate, 4);
  memcpy(pristineHeader + 32, &blockAlign, 2);

  // Reads the audio
  for (uint32_t rep = 0; rep < repeat; rep++) {
    size_t totalRead  = 0;
    int segmentNum    = 0;
    
    wavFile.seek(audio_start_byte); 

    while (totalRead < dataSize) {
      size_t bytesToRead = min(actualChunkSize, dataSize - totalRead);

      memcpy(chunkBuffer, pristineHeader, 44);
      uint32_t chunkFileSize = bytesToRead + 36;
      memcpy(chunkBuffer + 4, &chunkFileSize, 4);
      memcpy(chunkBuffer + 40, &bytesToRead, 4);

      size_t bytesRead = wavFile.read(chunkBuffer + 44, bytesToRead);
      if (bytesRead == 0) break;

      totalRead += bytesRead;
      segmentNum++;

      if (segmentNum % 5 == 1) {
        Serial.printf("Segment %d (%.1f%%)\n", segmentNum, (float)totalRead / dataSize * 100.0);
      }

      bool playResult = M5.Speaker.playWav(chunkBuffer, bytesRead + 44, 1, channel, stop_current);

      if (!playResult) {
        Serial.printf("Segment %d failed\n", segmentNum);
        break;
      }

      while (M5.Speaker.isPlaying()) {
        delay(5);
      }
      delay(10);
    }

    if (rep < repeat - 1) delay(1000);
  }

  free(chunkBuffer);
  wavFile.close();
  Serial.println("Segmented playback completed!");
  return true;
}