// settings.h - persisted configuration + connectivity
#pragma once
#include <Arduino.h>

struct Settings {
  String ssid, pass;
  String place = "Guadalupe, ZAC";
  double lat = 22.7471, lon = -102.5185;
  int32_t tzOff = -6 * 3600;   // seconds east of UTC
  bool tzAuto = true;          // update offset from weather API
  bool fahrenheit = false;
  uint8_t bright = 120;
};
extern Settings S;

void loadSettings();
void saveSettings();

bool wifiConnect(uint32_t timeoutMs = 10000);
bool wifiOk();
void wifiOff();  // radio off to save battery; reconnects on demand
bool syncTime(uint32_t timeoutMs = 6000);

// HTTPS GET (certificate not pinned). Returns HTTP body in out.
bool httpGet(const String& url, String& out, String& err);
String urlEncode(const String& s);

void runSettings();
