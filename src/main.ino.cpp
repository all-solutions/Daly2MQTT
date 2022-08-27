# 1 "C:\\Users\\PCHILF~1\\AppData\\Local\\Temp\\tmpjik6tqk4"
#include <Arduino.h>
# 1 "C:/Users/PC Hilfe Weimar/Documents/Git repos/DALY-BMS-to-MQTT/DALY-BMS-to-MQTT/src/main.ino"
#include <Arduino.h>

#include <daly-bms-uart.h>
#define BMS_SERIAL Serial
#define DALY_BMS_DEBUG Serial1

#include <EEPROM.h>
#include <PubSubClient.h>

#include <ArduinoJson.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncWiFiManager.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "Settings.h"

#include "webpages/htmlCase.h"
#include "webpages/main.h"
#include "webpages/settings.h"
#include "webpages/settingsedit.h"

WiFiClient client;
Settings _settings;
PubSubClient mqttclient(client);
int jsonBufferSize = 1024;
char jsonBuffer[1024];
DynamicJsonDocument bmsJson(jsonBufferSize);
JsonObject packJson = bmsJson.createNestedObject("Pack");
JsonObject cellVJson = bmsJson.createNestedObject("CellV");
JsonObject cellTempJson = bmsJson.createNestedObject("CellTemp");

String topic = "/";

unsigned long mqtttimer = 0;
unsigned long bmstimer = 0;
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
AsyncWebSocketClient *wsClient;
DNSServer dns;
Daly_BMS_UART bms(BMS_SERIAL);


bool shouldSaveConfig = false;
char mqtt_server[40];
bool restartNow = false;
bool updateProgress = false;
bool dataCollect = false;
void saveConfigCallback();
static void handle_update_progress_cb(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final);
void notifyClients();
void handleWebSocketMessage(void *arg, uint8_t *data, size_t len);
void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len);
void setup();
void loop();
void getJsonData();
void clearJsonData();
bool sendtoMQTT();
void callback(char *top, byte *payload, unsigned int length);
#line 51 "C:/Users/PC Hilfe Weimar/Documents/Git repos/DALY-BMS-to-MQTT/DALY-BMS-to-MQTT/src/main.ino"
void saveConfigCallback()
{
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

static void handle_update_progress_cb(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  uint32_t free_space = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
  if (!index)
  {
#ifdef DALY_BMS_DEBUG
    DALY_BMS_DEBUG.println("Update");
#endif
    Update.runAsync(true);
    if (!Update.begin(free_space))
    {
#ifdef DALY_BMS_DEBUG
      Update.printError(DALY_BMS_DEBUG);
#endif
    }
  }

  if (Update.write(data, len) != len)
  {
#ifdef DALY_BMS_DEBUG
    Update.printError(DALY_BMS_DEBUG);
#endif
  }

  if (final)
  {
    if (!Update.end(true))
    {
#ifdef DALY_BMS_DEBUG
      Update.printError(DALY_BMS_DEBUG);
#endif
    }
    else
    {

      AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Please wait while the device is booting new Firmware");
      response->addHeader("Refresh", "10; url=/");
      response->addHeader("Connection", "close");
      request->send(response);

      restartNow = true;
#ifdef DALY_BMS_DEBUG
      DALY_BMS_DEBUG.println("Update complete");
#endif
    }
  }
}

void notifyClients()
{
  if (wsClient != nullptr && wsClient->canSend())
  {
    serializeJson(bmsJson, jsonBuffer);
    wsClient->text(jsonBuffer);

  }
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    data[len] = 0;
    updateProgress = true;
    if (strcmp((char *)data, "dischargeFetSwitch_on") == 0)
    {
      bms.setDischargeMOS(true);
    }
    if (strcmp((char *)data, "dischargeFetSwitch_off") == 0)
    {
      bms.setDischargeMOS(false);
    }
    if (strcmp((char *)data, "chargeFetSwitch_on") == 0)
    {
      bms.setChargeMOS(true);
    }
    if (strcmp((char *)data, "chargeFetSwitch_off") == 0)
    {
      bms.setChargeMOS(false);
    }
    delay(200);
    updateProgress = false;
    if (strcmp((char *)data, "dataRequired") == 0)
    {
      bmstimer = 0;

    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    wsClient = client;

    break;
  case WS_EVT_DISCONNECT:
    wsClient = nullptr;

    break;
  case WS_EVT_DATA:
    handleWebSocketMessage(arg, data, len);
    break;
  case WS_EVT_PONG:
  case WS_EVT_ERROR:
    break;
  }
}

void setup()
{
#ifdef DALY_BMS_DEBUG

  DALY_BMS_DEBUG.begin(9600);
#endif

  _settings.load();
  delay(500);
  bms.Init();
  WiFi.persistent(true);
  packJson["Device_Name"] = _settings._deviceName;
  AsyncWiFiManager wm(&server, &dns);
  bmstimer = millis();
  mqtttimer = millis();

#ifdef DALY_BMS_DEBUG
  wm.setDebugOutput(false);
#endif
  wm.setSaveConfigCallback(saveConfigCallback);

#ifdef DALY_BMS_DEBUG
  DALY_BMS_DEBUG.begin(9600);
#endif

#ifdef DALY_BMS_DEBUG
  DALY_BMS_DEBUG.println();
  DALY_BMS_DEBUG.printf("Device Name:\t");
  DALY_BMS_DEBUG.println(_settings._deviceName);
  DALY_BMS_DEBUG.printf("Mqtt Server:\t");
  DALY_BMS_DEBUG.println(_settings._mqttServer);
  DALY_BMS_DEBUG.printf("Mqtt Port:\t");
  DALY_BMS_DEBUG.println(_settings._mqttPort);
  DALY_BMS_DEBUG.printf("Mqtt User:\t");
  DALY_BMS_DEBUG.println(_settings._mqttUser);
  DALY_BMS_DEBUG.printf("Mqtt Passwort:\t");
  DALY_BMS_DEBUG.println(_settings._mqttPassword);
  DALY_BMS_DEBUG.printf("Mqtt Interval:\t");
  DALY_BMS_DEBUG.println(_settings._mqttRefresh);
  DALY_BMS_DEBUG.printf("Mqtt Topic:\t");
  DALY_BMS_DEBUG.println(_settings._mqttTopic);
#endif
  AsyncWiFiManagerParameter custom_mqtt_server("mqtt_server", "MQTT server", NULL, 40);
  AsyncWiFiManagerParameter custom_mqtt_user("mqtt_user", "MQTT User", NULL, 40);
  AsyncWiFiManagerParameter custom_mqtt_pass("mqtt_pass", "MQTT Password", NULL, 100);
  AsyncWiFiManagerParameter custom_mqtt_topic("mqtt_topic", "MQTT Topic", NULL, 30);
  AsyncWiFiManagerParameter custom_mqtt_port("mqtt_port", "MQTT Port", NULL, 6);
  AsyncWiFiManagerParameter custom_mqtt_refresh("mqtt_refresh", "MQTT Send Interval", NULL, 4);
  AsyncWiFiManagerParameter custom_device_name("device_name", "Device Name", NULL, 40);

  wm.addParameter(&custom_mqtt_server);
  wm.addParameter(&custom_mqtt_user);
  wm.addParameter(&custom_mqtt_pass);
  wm.addParameter(&custom_mqtt_topic);
  wm.addParameter(&custom_mqtt_port);
  wm.addParameter(&custom_mqtt_refresh);
  wm.addParameter(&custom_device_name);

  bool res = wm.autoConnect("DALY-BMS-AP");

  wm.setConnectTimeout(30);
  wm.setConfigPortalTimeout(120);


  if (shouldSaveConfig)
  {
    _settings._mqttServer = custom_mqtt_server.getValue();
    _settings._mqttUser = custom_mqtt_user.getValue();
    _settings._mqttPassword = custom_mqtt_pass.getValue();
    _settings._mqttPort = atoi(custom_mqtt_port.getValue());
    _settings._deviceName = custom_device_name.getValue();
    _settings._mqttTopic = custom_mqtt_topic.getValue();
    _settings._mqttRefresh = atoi(custom_mqtt_refresh.getValue());

    _settings.save();
    delay(500);
    _settings.load();
    ESP.restart();
  }

  topic = _settings._mqttTopic;
  mqttclient.setServer(_settings._mqttServer.c_str(), _settings._mqttPort);
  mqttclient.setCallback(callback);
  mqttclient.setBufferSize(jsonBufferSize);

  if (!res)
  {
#ifdef DALY_BMS_DEBUG
    DALY_BMS_DEBUG.println("Failed to connect or hit timeout");
#endif
  }
  else
  {

    MDNS.begin(_settings._deviceName);
    WiFi.hostname(_settings._deviceName);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                AsyncResponseStream *response = request->beginResponseStream("text/html");
                response->printf_P(HTML_HEAD);
                response->printf_P(HTML_MAIN);
                response->printf_P(HTML_FOOT);
                request->send(response); });

    server.on("/livejson", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                AsyncResponseStream *response = request->beginResponseStream("application/json");
                serializeJson(bmsJson, *response);
                request->send(response); });

    server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Please wait while the device reboots...");
                response->addHeader("Refresh", "5; url=/");
                response->addHeader("Connection", "close");
                request->send(response);
                restartNow = true; });

    server.on("/confirmreset", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                AsyncResponseStream *response = request->beginResponseStream("text/html");
                response->printf_P(HTML_HEAD);
                response->printf_P(HTML_CONFIRM_RESET);
                response->printf_P(HTML_FOOT);
                request->send(response); });

    server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "Device is Erasing...");
                response->addHeader("Refresh", "15; url=/");
                response->addHeader("Connection", "close");
                request->send(response);
                delay(1000);
                _settings.reset();
                ESP.eraseConfig();
                ESP.restart(); });

    server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                AsyncResponseStream *response = request->beginResponseStream("text/html");
                response->printf_P(HTML_HEAD);
                response->printf_P(HTML_SETTINGS);
                response->printf_P(HTML_FOOT);
                request->send(response); });

    server.on("/settingsedit", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                AsyncResponseStream *response = request->beginResponseStream("text/html");
                response->printf_P(HTML_HEAD);
                response->printf_P(HTML_SETTINGS_EDIT);
                response->printf_P(HTML_FOOT);
                request->send(response); });

    server.on("/settingsjson", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                AsyncResponseStream *response = request->beginResponseStream("application/json");
                DynamicJsonDocument SettingsJson(256);
                SettingsJson["device_name"] = _settings._deviceName;
                SettingsJson["mqtt_server"] = _settings._mqttServer;
                SettingsJson["mqtt_port"] = _settings._mqttPort;
                SettingsJson["mqtt_topic"] = _settings._mqttTopic;
                SettingsJson["mqtt_user"] = _settings._mqttUser;
                SettingsJson["mqtt_password"] = _settings._mqttPassword;
                SettingsJson["mqtt_refresh"] = _settings._mqttRefresh;
                SettingsJson["mqtt_json"] = _settings._mqttJson?true:false;
                serializeJson(SettingsJson, *response);
                request->send(response); });

    server.on("/settingssave", HTTP_POST, [](AsyncWebServerRequest *request)
              {
                request->redirect("/settings");
                _settings._mqttServer = request->arg("post_mqttServer");
                _settings._mqttPort = request->arg("post_mqttPort").toInt();
                _settings._mqttUser = request->arg("post_mqttUser");
                _settings._mqttPassword = request->arg("post_mqttPassword");
                _settings._mqttTopic = request->arg("post_mqttTopic");
                _settings._mqttRefresh = request->arg("post_mqttRefresh").toInt() < 1 ? 1 : request->arg("post_mqttRefresh").toInt();
                _settings._deviceName = request->arg("post_deviceName");
                if(request->arg("post_mqttjson") == "true") _settings._mqttJson = true;
                if(request->arg("post_mqttjson") != "true") _settings._mqttJson = false;
                Serial.print(_settings._mqttServer);
                _settings.save();
                delay(500);
                _settings.load(); });

    server.on("/set", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                AsyncWebParameter *p = request->getParam(0);
                if (p->name() == "chargefet")
                {
#ifdef DALY_BMS_DEBUG
                    DALY_BMS_DEBUG.println("charge fet webswitch to: "+(String)p->value());
#endif
                    if(p->value().toInt() == 1){
                      bms.setChargeMOS(true);
                      bms.get.chargeFetState = true;
                    }
                    if(p->value().toInt() == 0){
                      bms.setChargeMOS(false);
                      bms.get.chargeFetState = false;
                    }
                }
                if (p->name() == "dischargefet")
                {
#ifdef DALY_BMS_DEBUG
                    DALY_BMS_DEBUG.println("discharge fet webswitch to: "+(String)p->value());
#endif
                    if(p->value().toInt() == 1){
                      bms.setDischargeMOS(true);
                      bms.get.disChargeFetState = true;
                    }
                    if(p->value().toInt() == 0){
                      bms.setDischargeMOS(false);
                      bms.get.disChargeFetState = false;
                    }
                }
                request->send(200, "text/plain", "message received"); });

    server.on(
        "/update", HTTP_POST, [](AsyncWebServerRequest *request)
        {
          updateProgress = true;

          request->send(200);
          request->redirect("/"); },
        handle_update_progress_cb);

    ws.onEvent(onEvent);
    server.addHandler(&ws);
    server.begin();
    MDNS.addService("http", "tcp", 80);
#ifdef DALY_BMS_DEBUG
    DALY_BMS_DEBUG.println("Webserver Running...");
#endif
  }

  if (!mqttclient.connected())
    mqttclient.connect((String(_settings._deviceName)).c_str(), _settings._mqttUser.c_str(), _settings._mqttPassword.c_str());
  if (mqttclient.connect(_settings._deviceName.c_str()))
  {
    if (!_settings._mqttJson)
    {
      mqttclient.subscribe((String(topic + "/" + _settings._deviceName) + String("/Pack DischargeFET")).c_str());
      mqttclient.subscribe((String(topic + "/" + _settings._deviceName) + String("/Pack ChargeFET")).c_str());
    }
    else
    {
      mqttclient.subscribe((String(topic + "/" + _settings._deviceName)).c_str());
    }
  }
}





void loop()
{
  if (restartNow)
  {
    Serial.println("Restart");
    ESP.restart();
  }


  if (WiFi.status() == WL_CONNECTED)
  {
    ws.cleanupClients();
    MDNS.update();
    mqttclient.loop();

    if (!updateProgress)
    {
      if (millis() > (bmstimer + (3 * 1000)) && wsClient != nullptr && wsClient->canSend())
      {
        bmstimer = millis();
        if (bms.update())
        {
          getJsonData();
        }
        else
        {
          clearJsonData();
        }
        notifyClients();
      }
      else if (millis() > (mqtttimer + (_settings._mqttRefresh * 1000)))
      {
        mqtttimer = millis();
        if (millis() < (bmstimer + (3 * 1000)))
        {
          sendtoMQTT();
        }
        else
        {
          if (bms.update())
          {
            sendtoMQTT();
          }
          else
          {
            clearJsonData();
          }
        }
      }
    }
  }

  yield();
}

void getJsonData()
{
  packJson["Device_IP"] = WiFi.localIP().toString();
  packJson["Voltage"] = bms.get.packVoltage;
  packJson["Current"] = bms.get.packCurrent;
  packJson["SOC"] = bms.get.packSOC;
  packJson["Remaining_mAh"] = bms.get.resCapacitymAh;
  packJson["Cycles"] = bms.get.bmsCycles;
  packJson["MinTemp"] = bms.get.tempMin;
  packJson["MaxTemp"] = bms.get.tempMax;
  packJson["Temp"] = bms.get.cellTemperature[0];
  packJson["High_CellNr"] = bms.get.maxCellVNum;
  packJson["High_CellV"] = bms.get.maxCellmV / 1000;
  packJson["Low_CellNr"] = bms.get.minCellVNum;
  packJson["Low_CellV"] = bms.get.minCellmV / 1000;
  packJson["Cell_Diff"] = bms.get.cellDiff;
  packJson["DischargeFET"] = bms.get.disChargeFetState ? true : false;
  packJson["ChargeFET"] = bms.get.chargeFetState ? true : false;
  packJson["Status"] = bms.get.chargeDischargeStatus;
  packJson["Cells"] = bms.get.numberOfCells;
  packJson["Heartbeat"] = bms.get.bmsHeartBeat;
  packJson["Balance_Active"] = bms.get.cellBalanceActive ? true : false;

  for (size_t i = 0; i < size_t(bms.get.numberOfCells); i++)
  {
    cellVJson["CellV " + String(i + 1)] = bms.get.cellVmV[i] / 1000;
    cellVJson["Balance " + String(i + 1)] = bms.get.cellBalanceState[i];
  }

  for (size_t i = 0; i < size_t(bms.get.numOfTempSensors); i++)
  {
    cellTempJson["Temp" + String(i + 1)] = bms.get.cellTemperature[i];
  }
}

void clearJsonData()
{
  packJson["Voltage"] = nullptr;
  packJson["Current"] = nullptr;
  packJson["SOC"] = nullptr;
  packJson["Remaining_mAh"] = nullptr;
  packJson["Cycles"] = nullptr;
  packJson["MinTemp"] = nullptr;
  packJson["MaxTemp"] = nullptr;
  packJson["Temp"] = nullptr;
  packJson["High_CellNr"] = nullptr;
  packJson["High_CellV"] = nullptr;
  packJson["Low_CellNr"] = nullptr;
  packJson["Low_CellV"] = nullptr;
  packJson["Cell_Diff"] = nullptr;
  packJson["DischargeFET"] = nullptr;
  packJson["ChargeFET"] = nullptr;
  packJson["Status"] = nullptr;
  packJson["Cells"] = nullptr;
  packJson["Heartbeat"] = nullptr;
  packJson["Balance_Active"] = nullptr;
  cellVJson.clear();
  cellTempJson.clear();
}

bool sendtoMQTT()
{







  if (!mqttclient.connected())
  {
    if (mqttclient.connect((String(_settings._deviceName)).c_str(), _settings._mqttUser.c_str(), _settings._mqttPassword.c_str()))
    {
#ifdef DALY_BMS_DEBUG
      DALY_BMS_DEBUG.println(F("Reconnected to MQTT SERVER"));
#endif
      if (!_settings._mqttJson)
      {
        mqttclient.publish((topic + "/" + _settings._deviceName + String("/Device_IP")).c_str(), String(WiFi.localIP().toString()).c_str());
      }
    }
    else
    {
#ifdef DALY_BMS_DEBUG
      DALY_BMS_DEBUG.println(F("CANT CONNECT TO MQTT"));
#endif
      return false;
    }
  }
#ifdef DALY_BMS_DEBUG
  DALY_BMS_DEBUG.println(F("Data sent to MQTT Server"));
#endif





  if (!_settings._mqttJson)
  {
    char msgBuffer[20];
    mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack Voltage")).c_str(), dtostrf(bms.get.packVoltage, 4, 1, msgBuffer));
    mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack Current")).c_str(), dtostrf(bms.get.packCurrent, 4, 1, msgBuffer));
    mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack SOC")).c_str(), dtostrf(bms.get.packSOC, 6, 2, msgBuffer));
    mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack Remaining mAh")).c_str(), String(bms.get.resCapacitymAh).c_str());
    mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack Cycles")).c_str(), String(bms.get.bmsCycles).c_str());
    mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack Min Temperature")).c_str(), String(bms.get.tempMin).c_str());
    mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack Max Temperature")).c_str(), String(bms.get.tempMax).c_str());
    mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack High Cell")).c_str(), (dtostrf(bms.get.maxCellVNum, 1, 0, msgBuffer) + String(".- ") + dtostrf(bms.get.maxCellmV / 1000, 5, 3, msgBuffer)).c_str());
    mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack Low Cell")).c_str(), (dtostrf(bms.get.minCellVNum, 1, 0, msgBuffer) + String(".- ") + dtostrf(bms.get.minCellmV / 1000, 5, 3, msgBuffer)).c_str());
    mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack Cell Difference")).c_str(), String(bms.get.cellDiff).c_str());
    mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack ChargeFET")).c_str(), bms.get.chargeFetState ? "true" : "false");
    mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack DischargeFET")).c_str(), bms.get.disChargeFetState ? "true" : "false");
    mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack Status")).c_str(), String(bms.get.chargeDischargeStatus).c_str());
    mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack Cells")).c_str(), String(bms.get.numberOfCells).c_str());
    mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack Heartbeat")).c_str(), String(bms.get.bmsHeartBeat).c_str());
    mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack Balance Active")).c_str(), String(bms.get.cellBalanceActive ? "true" : "false").c_str());

    for (size_t i = 0; i < size_t(bms.get.numberOfCells); i++)
    {
      mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack Cells Voltage/Cell ") + (String)(i + 1)).c_str(), dtostrf(bms.get.cellVmV[i] / 1000, 5, 3, msgBuffer));
      mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack Cells Balance/Cell ") + (String)(i + 1)).c_str(), String(bms.get.cellBalanceState[i] ? "true" : "false").c_str());
    }
    for (size_t i = 0; i < size_t(bms.get.numOfTempSensors); i++)
    {
      mqttclient.publish((String(topic + "/" + _settings._deviceName) + String("/Pack Temperature Sensor No ") + (String)(i + 1)).c_str(), String(bms.get.cellTemperature[i]).c_str());
    }
  }
  else
  {
    size_t n = serializeJson(bmsJson, jsonBuffer);
    mqttclient.publish((String(topic + "/" + _settings._deviceName)).c_str(), jsonBuffer, n);
  }
  return true;
}

void callback(char *top, byte *payload, unsigned int length)
{
  updateProgress = true;
  if (!_settings._mqttJson)
  {
    String messageTemp;
    for (unsigned int i = 0; i < length; i++)
    {
      messageTemp += (char)payload[i];
    }
#ifdef DALY_BMS_DEBUG
    DALY_BMS_DEBUG.println("message recived: " + messageTemp);
#endif

    if (strcmp(top, (topic + "/Pack DischargeFET").c_str()) == 0)
    {
#ifdef DALY_BMS_DEBUG
      DALY_BMS_DEBUG.println("message recived: " + messageTemp);
#endif

      if (messageTemp == "true")
      {
#ifdef DALY_BMS_DEBUG
        DALY_BMS_DEBUG.println("switching Discharging mos on");
#endif
        bms.setDischargeMOS(true);
      }
      if (messageTemp == "false")
      {
#ifdef DALY_BMS_DEBUG
        DALY_BMS_DEBUG.println("switching Discharging mos off");
#endif
        bms.setDischargeMOS(false);
      }
    }


    if (strcmp(top, (topic + "/Pack ChargeFET").c_str()) == 0)
    {
#ifdef DALY_BMS_DEBUG
      DALY_BMS_DEBUG.println("message recived: " + messageTemp);
#endif

      if (messageTemp == "true")
      {
#ifdef DALY_BMS_DEBUG
        DALY_BMS_DEBUG.println("switching Charging mos on");
#endif
        bms.setChargeMOS(true);
      }
      if (messageTemp == "false")
      {
#ifdef DALY_BMS_DEBUG
        DALY_BMS_DEBUG.println("switching Charging mos off");
#endif
        bms.setChargeMOS(false);
      }
    }
  }
  else
  {
    StaticJsonDocument<1024> mqttJsonAnswer;
    deserializeJson(mqttJsonAnswer, (const byte *)payload, length);

    if (mqttJsonAnswer["Pack"]["ChargeFET"] == true)
    {
      bms.setChargeMOS(true);
    }
    else if (mqttJsonAnswer["Pack"]["ChargeFET"] == false)
    {
      bms.setChargeMOS(false);
    }
    else
    {
#ifdef DALY_BMS_DEBUG
      DALY_BMS_DEBUG.println("No Valid Command from JSON for setChargeMOS");
#endif
    }
    if (mqttJsonAnswer["Pack"]["DischargeFET"] == true)
    {
      bms.setDischargeMOS(true);
    }
    else if (mqttJsonAnswer["Pack"]["DischargeFET"] == false)
    {
      bms.setDischargeMOS(false);
    }
    else
    {
#ifdef DALY_BMS_DEBUG
      DALY_BMS_DEBUG.println("No Valid Command from JSON for setDischargeMOS");
#endif
    }
  }
  updateProgress = false;
}