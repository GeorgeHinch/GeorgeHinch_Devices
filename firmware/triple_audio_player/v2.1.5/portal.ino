#include <WebServer.h>
#include <DNSServer.h>
#include "triple_audio_player_portal.h"

WebServer configServer(80);
DNSServer configDns;
bool configApActive = false;
bool configServerStarted = false;
char configApName[33] = "";

bool isConfigAccessPointActive() {
  return configApActive;
}

void updateConfigAccessPointName() {
  snprintf(configApName, sizeof(configApName), "%s", deviceId);
}

int boundedArgument(const char* name, int current, int minimum, int maximum) {
  if (!configServer.hasArg(name)) return current;
  int value = configServer.arg(name).toInt();
  return constrain(value, minimum, maximum);
}

void copyArgument(const char* name, char* destination, size_t length) {
  if (!configServer.hasArg(name)) return;
  copySetting(destination, length, configServer.arg(name).c_str());
}

void sendPortalPage() {
  configServer.sendHeader("Content-Encoding", "gzip");
  configServer.send_P(200, "text/html; charset=utf-8",
                      reinterpret_cast<PGM_P>(TRIPLE_AUDIO_PLAYER_PORTAL_HTML),
                      TRIPLE_AUDIO_PLAYER_PORTAL_HTML_LENGTH);
}

void sendConfigJson() {
  JsonDocument doc;
  doc["deviceId"] = deviceId;
  doc["firmware"] = FIRMWARE_VERSION;
  updateConfigAccessPointName();
  doc["configSsid"] = configApName;
  doc["connected"] = WiFi.status() == WL_CONNECTED;
  doc["ip"] = WiFi.status() == WL_CONNECTED
                ? WiFi.localIP().toString()
                : WiFi.softAPIP().toString();
  doc["globalBegEnabled"] = settings.globalBegEnabled;
  doc["begAction"] = settings.begButtonAction;

  JsonArray playerArray = doc["players"].to<JsonArray>();
  for (int i = 0; i < 3; i++) {
    JsonObject player = playerArray.add<JsonObject>();
    player["present"] = dfpPresent[i];
    player["mode"] = modeToString(players[i].mode);
    player["volume"] = players[i].volume;
    player["track"] = players[i].track;
    player["folder"] = players[i].folder;
    player["muted"] = players[i].muted;
  }

  doc["wifiSsid"] = settings.wifiSsid;
  doc["mqttEnabled"] = settings.mqttEnabled;
  doc["mqttBroker"] = settings.mqttBroker;
  doc["mqttPort"] = settings.mqttPort;
  doc["mqttUser"] = settings.mqttUser;
  doc["jmriChannel"] = settings.jmriChannel;
  doc["otaState"] = DeviceOta::lastState;

  String json;
  serializeJson(doc, json);
  configServer.send(200, "application/json; charset=utf-8", json);
}

void savePortalSettings() {
  copyArgument("wifi_ssid", settings.wifiSsid, sizeof(settings.wifiSsid));
  copyArgument("mqtt_broker", settings.mqttBroker, sizeof(settings.mqttBroker));
  copyArgument("mqtt_user", settings.mqttUser, sizeof(settings.mqttUser));
  copyArgument("jmri_channel", settings.jmriChannel, sizeof(settings.jmriChannel));
  settings.mqttEnabled = configServer.hasArg("mqtt_enabled");
  settings.mqttPort = boundedArgument("mqtt_port", settings.mqttPort, 1, 65535);
  settings.begButtonAction = boundedArgument("begaction", settings.begButtonAction, BEG_DISABLED, BEG_PLAYER_3);

  if (configServer.hasArg("wifi_password") && configServer.arg("wifi_password").length() > 0) {
    copyArgument("wifi_password", settings.wifiPassword, sizeof(settings.wifiPassword));
  }
  if (configServer.hasArg("mqtt_password") && configServer.arg("mqtt_password").length() > 0) {
    copyArgument("mqtt_password", settings.mqttPassword, sizeof(settings.mqttPassword));
  }

  for (int i = 0; i < 3; i++) {
    String number = String(i + 1);
    String modeName = "mode" + number;
    if (!configServer.hasArg(modeName)) continue;

    players[i].mode = stringToMode(configServer.arg(modeName).c_str());
    players[i].volume = boundedArgument(("volume" + number).c_str(), players[i].volume, 0, 30);
    players[i].track = boundedArgument(("track" + number).c_str(), players[i].track, 1, 255);
    players[i].folder = boundedArgument(("folder" + number).c_str(), players[i].folder, 1, 99);
    players[i].muted = configServer.hasArg("muted" + number);
    savePlayerSettings(i);
  }

  saveDeviceSettings();
  configServer.send(200, "text/html; charset=utf-8",
    "<!doctype html><meta name='viewport' content='width=device-width'><style>body{font-family:system-ui;padding:2rem;max-width:40rem;margin:auto}a{color:#7a43c4}</style><h1>Settings saved</h1><p>The Triple Audio Player is restarting now.</p><p>If this page does not close, reconnect to its normal Wi-Fi network.</p>");
  delay(1000);
  ESP.restart();
}

void clearAudioNamespace(const char* name) {
  prefs.begin(name, false);
  prefs.clear();
  prefs.end();
}

void resetStoredAudioSettings() {
  String action = configServer.arg("action");

  if (action == "wifi") {
    prefs.begin(CONFIG_NAMESPACE, false);
    prefs.remove("wifiSsid");
    prefs.remove("wifiPass");
    prefs.end();
  } else if (action == "mqtt") {
    prefs.begin(CONFIG_NAMESPACE, false);
    prefs.remove("mqttEn");
    prefs.remove("mqttHost");
    prefs.remove("mqttPort");
    prefs.remove("mqttUser");
    prefs.remove("mqttPass");
    prefs.remove("mqttPref");
    prefs.end();
  } else if (action == "factory") {
    clearAudioNamespace(CONFIG_NAMESPACE);
    clearAudioNamespace("dfp_cfg");
    clearAudioNamespace("dfp_names");
  } else {
    configServer.send(400, "application/json", "{\"ok\":false,\"error\":\"Unknown reset action\"}");
    return;
  }

  for (int i = 0; i < 3; i++) {
    if (dfpPresent[i]) players[i].dfp.stop();
  }
  configServer.sendHeader("Cache-Control", "no-store");
  configServer.send(200, "application/json", "{\"ok\":true,\"restarting\":true}");
  delay(700);
  ESP.restart();
}

void handlePortalOta() {
  String action = configServer.hasArg("action") ? configServer.arg("action") : "STATUS";
  action.trim();
  action.toUpperCase();
  bool safeForOta = true;
  for (int i = 0; i < 3; ++i) safeForOta = safeForOta && !players[i].playing;
  if (action == "CHECK") DeviceOta::check(mqtt, settings.mqttEnabled, safeForOta, false);
  else if (action == "FORCE") DeviceOta::check(mqtt, settings.mqttEnabled, safeForOta, true);
  configServer.sendHeader("Location", "/", true);
  configServer.send(303, "text/plain", "");
}

void startConfigAccessPoint() {
  if (configApActive) return;
  updateConfigAccessPointName();
  WiFi.softAP(configApName);
  configDns.start(53, "*", WiFi.softAPIP());
  configApActive = true;
  Serial.printf("Configuration Wi-Fi: %s (open)\n", configApName);
  Serial.printf("Configuration page: http://%s/\n", WiFi.softAPIP().toString().c_str());
}

void beginConfigPortal(bool startApNow) {
  WiFi.mode(WIFI_AP_STA);

  if (!configServerStarted) {
    configServer.on("/", HTTP_GET, sendPortalPage);
    configServer.on("/api/config", HTTP_GET, sendConfigJson);
    configServer.on("/save", HTTP_POST, savePortalSettings);
    configServer.on("/reset", HTTP_POST, resetStoredAudioSettings);
    configServer.on("/ota", HTTP_ANY, handlePortalOta);
    configServer.on("/generate_204", HTTP_GET, sendPortalPage);
    configServer.on("/hotspot-detect.html", HTTP_GET, sendPortalPage);
    configServer.on("/connecttest.txt", HTTP_GET, sendPortalPage);
    configServer.onNotFound(sendPortalPage);
    configServer.begin();
    configServerStarted = true;
  }

  if (startApNow) startConfigAccessPoint();
}

void handleConfigPortal() {
  configServer.handleClient();
  if (configApActive) configDns.processNextRequest();
}
