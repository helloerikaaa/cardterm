#include "settings.h"
#include "ui.h"
#include "weather.h"
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

Settings S;
static Preferences prefs;

void loadSettings() {
  prefs.begin("cardterm", true);
  S.ssid = prefs.getString("ssid", "");
  S.pass = prefs.getString("pass", "");
  S.place = prefs.getString("place", S.place);
  S.lat = prefs.getDouble("lat", S.lat);
  S.lon = prefs.getDouble("lon", S.lon);
  S.tzOff = prefs.getInt("tz", S.tzOff);
  S.tzAuto = prefs.getBool("tzauto", true);
  S.fahrenheit = prefs.getBool("fahr", false);
  S.bright = prefs.getUChar("bright", S.bright);
  prefs.end();
}

void saveSettings() {
  prefs.begin("cardterm", false);
  prefs.putString("ssid", S.ssid);
  prefs.putString("pass", S.pass);
  prefs.putString("place", S.place);
  prefs.putDouble("lat", S.lat);
  prefs.putDouble("lon", S.lon);
  prefs.putInt("tz", S.tzOff);
  prefs.putBool("tzauto", S.tzAuto);
  prefs.putBool("fahr", S.fahrenheit);
  prefs.putUChar("bright", S.bright);
  prefs.end();
}

bool wifiOk() { return WiFi.status() == WL_CONNECTED; }

void wifiOff() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

bool wifiConnect(uint32_t timeoutMs) {
  if (S.ssid.isEmpty()) return false;
  if (wifiOk()) return true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(S.ssid.c_str(), S.pass.c_str());
  uint32_t t0 = millis();
  while (!wifiOk() && millis() - t0 < timeoutMs) {
    M5Cardputer.update();
    delay(100);
  }
  if (wifiOk() && !timeValid()) syncTime();
  return wifiOk();
}

bool syncTime(uint32_t timeoutMs) {
  if (!wifiOk()) return timeValid();
  if (timeValid()) {  // re-sync: wait for the next NTP answer
    configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
    delay(1500);
    return true;
  }
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time.cloudflare.com");
  uint32_t t0 = millis();
  while (!timeValid() && millis() - t0 < timeoutMs) delay(100);
  return timeValid();
}

String urlEncode(const String& s) {
  String o;
  const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t c = s[i];
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
    else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
  }
  return o;
}

bool httpGet(const String& url, String& out, String& err) {
  if (!wifiOk() && !wifiConnect()) { err = "no wifi"; return false; }
  WiFiClientSecure client;
  client.setInsecure();  // public, read-only weather data
  HTTPClient http;
  http.setTimeout(12000);
  if (!http.begin(client, url)) { err = "bad url"; return false; }
  int code = http.GET();
  if (code != 200) {
    err = code < 0 ? HTTPClient::errorToString(code) : String("http ") + code;
    http.end();
    return false;
  }
  out = http.getString();
  http.end();
  return true;
}

// ---------------- settings screens ----------------
static const char* P = "~/settings";

static void wifiSetup() {
  message(P, "scanning...", "looking for networks", nullptr, false);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  int n = WiFi.scanNetworks();
  if (n <= 0) {
    message(P, "no networks", "press any key");
    return;
  }
  if (n > 16) n = 16;
  static String names[17];
  static String sig[17];
  const char* items[17];
  const char* hints[17];
  for (int i = 0; i < n; i++) {
    names[i] = WiFi.SSID(i).substring(0, 20);
    if (names[i].isEmpty()) names[i] = "(hidden)";
    int r = WiFi.RSSI(i);
    sig[i] = String(r > -55 ? "####" : r > -67 ? "###." : r > -78 ? "##.." : "#...") +
             (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? " open" : "");
    items[i] = names[i].c_str();
    hints[i] = sig[i].c_str();
  }
  names[n] = "enter manually";
  items[n] = names[n].c_str();
  hints[n] = nullptr;
  int pick = listMenu(P, "pick a network", items, n + 1, 0, hints);
  if (pick < 0) return;
  String ssid = pick < n ? WiFi.SSID(pick) : "";
  bool open = pick < n && WiFi.encryptionType(pick) == WIFI_AUTH_OPEN;
  WiFi.scanDelete();
  if (pick == n && !textInput(P, "network name (SSID)", ssid)) return;
  String pass = (ssid == S.ssid) ? S.pass : "";
  if (!open && !textInput(P, ("password for " + ssid).c_str(), pass, true)) return;
  S.ssid = ssid;
  S.pass = open ? "" : pass;
  message(P, "connecting...", S.ssid.c_str(), nullptr, false);
  WiFi.disconnect();
  if (wifiConnect(15000)) {
    saveSettings();
    syncTime();
    message(P, "connected", WiFi.localIP().toString().c_str(), "saved. press any key");
  } else {
    saveSettings();
    message(P, "could not connect", "check the password", "(saved anyway)", true, col::red);
  }
}

static void locationSearch() {
  String q;
  if (!textInput(P, "city name (e.g. Zacatecas)", q) || q.isEmpty()) return;
  message(P, "searching...", q.c_str(), nullptr, false);
  String body, err;
  String url = "https://geocoding-api.open-meteo.com/v1/search?count=8&language=en&format=json&name=" +
               urlEncode(q);
  if (!httpGet(url, body, err)) {
    message(P, "search failed", err.c_str(), nullptr, true, col::red);
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) { message(P, "bad reply", nullptr); return; }
  JsonArray res = doc["results"].as<JsonArray>();
  int n = res.size();
  if (n == 0) { message(P, "no matches", q.c_str()); return; }
  if (n > 8) n = 8;
  static String lab[8], hin[8];
  const char* items[8];
  const char* hints[8];
  for (int i = 0; i < n; i++) {
    JsonObject r = res[i];
    lab[i] = String((const char*)(r["name"] | "?")).substring(0, 16);
    String a1 = r["admin1"] | "";
    String cc = r["country_code"] | "";
    long pop = r["population"] | 0L;
    String ps = pop >= 1000000 ? String(pop / 1000000) + "M" : pop >= 1000 ? String(pop / 1000) + "k" : String();
    hin[i] = (a1.length() ? a1.substring(0, 9) + " " : String()) + cc + (ps.length() ? " " + ps : String());
    items[i] = lab[i].c_str();
    hints[i] = hin[i].c_str();
  }
  int pick = listMenu(P, "which one?", items, n, 0, hints);
  if (pick < 0) return;
  JsonObject r = res[pick];
  S.lat = r["latitude"] | S.lat;
  S.lon = r["longitude"] | S.lon;
  String cc = r["country_code"] | "";
  S.place = String((const char*)(r["name"] | "?")) + (cc.length() ? ", " + cc : "");
  saveSettings();
  weatherInvalidate();
  String werr;
  weatherFetch(werr);  // also refreshes the UTC offset
  char buf[40];
  snprintf(buf, sizeof buf, "%.3f, %.3f", S.lat, S.lon);
  message(P, S.place.c_str(), buf, "saved. press any key");
}

static void locationManual() {
  String a = String(S.lat, 4), b = String(S.lon, 4), nm = S.place;
  if (!textInput(P, "latitude (north +)", a)) return;
  if (!textInput(P, "longitude (east +, west -)", b)) return;
  if (!textInput(P, "label", nm)) return;
  double la = a.toDouble(), lo = b.toDouble();
  if (la < -90 || la > 90 || lo < -180 || lo > 180) {
    message(P, "out of range", nullptr, nullptr, true, col::red);
    return;
  }
  S.lat = la; S.lon = lo; S.place = nm;
  saveSettings();
  weatherInvalidate();
}

static void tzSetup() {
  for (;;) {
    clearBody();
    header(P);
    txt(2, 16, "# time zone (UTC offset)", col::dim);
    int m = S.tzOff / 60;
    txtf(20, 40, col::white, BIG, "UTC%c%02d:%02d", m < 0 ? '-' : '+', abs(m) / 60, abs(m) % 60);
    txtf(20, 64, S.tzAuto ? col::fg : col::grey, SMALL, "[a] auto from weather: %s",
         S.tzAuto ? "ON" : "off");
    txt(20, 76, "    (handles daylight saving)", col::dim);
    footer("[,/] -/+30min  [a] auto  [ent] save");
    present();
    Key k = waitKey();
    if (k.code == K_LEFT) { S.tzOff -= 1800; S.tzAuto = false; }
    else if (k.code == K_RIGHT) { S.tzOff += 1800; S.tzAuto = false; }
    else if (k.ch == 'a') S.tzAuto = !S.tzAuto;
    else if (k.code == K_ENTER || k.code == K_ESC || k.code == K_BACK) break;
    if (S.tzOff > 14 * 3600) S.tzOff = 14 * 3600;
    if (S.tzOff < -12 * 3600) S.tzOff = -12 * 3600;
  }
  saveSettings();
}

static void brightness() {
  for (;;) {
    clearBody();
    header(P);
    txt(2, 16, "# brightness", col::dim);
    int pct = S.bright * 100 / 255;
    cv.drawRect(20, 50, 200, 14, col::dim);
    cv.fillRect(22, 52, 196 * S.bright / 255, 10, col::fg);
    txtf(20, 72, col::white, SMALL, "%d%%", pct);
    footer("[,/] adjust   [ent] save");
    present();
    Key k = waitKey();
    int b = S.bright;
    if (k.code == K_LEFT) b -= 15;
    else if (k.code == K_RIGHT) b += 15;
    else if (k.code == K_ENTER || k.code == K_ESC || k.code == K_BACK) break;
    S.bright = constrain(b, 15, 255);
    M5Cardputer.Display.setBrightness(S.bright);
  }
  saveSettings();
}

static void manualClock() {
  String v;
  if (!textInput(P, "local time YYYY-MM-DD HH:MM", v)) return;
  int Y, Mo, D, h, mi;
  if (sscanf(v.c_str(), "%d-%d-%d %d:%d", &Y, &Mo, &D, &h, &mi) != 5 || Y < 2024 || Mo < 1 ||
      Mo > 12 || D < 1 || D > 31 || h > 23 || mi > 59) {
    message(P, "bad format", "example: 2026-10-25 16:30", nullptr, true, col::red);
    return;
  }
  // days from civil (Howard Hinnant)
  Y -= Mo <= 2;
  long era = (Y >= 0 ? Y : Y - 399) / 400;
  long yoe = Y - era * 400;
  long doy = (153 * (Mo + (Mo > 2 ? -3 : 9)) + 2) / 5 + D - 1;
  long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097 + doe - 719468;
  struct timeval tv = {(time_t)(days * 86400 + h * 3600 + mi * 60 - S.tzOff), 0};
  settimeofday(&tv, nullptr);
  message(P, "clock set", "(resets on power off)");
}

void runSettings() {
  int sel = 0;
  for (;;) {
    static String h[8];
    h[0] = S.ssid.isEmpty() ? "not set" : S.ssid.substring(0, 14);
    h[1] = S.place.substring(0, 16);
    h[2] = "lat/lon";
    h[3] = S.fahrenheit ? "F, mph" : "C, km/h";
    int m = S.tzOff / 60;
    char tz[24];
    snprintf(tz, sizeof tz, "UTC%c%d:%02d%s", m < 0 ? '-' : '+', abs(m) / 60, abs(m) % 60,
             S.tzAuto ? " auto" : "");
    h[4] = tz;
    h[5] = String(S.bright * 100 / 255) + "%";
    h[6] = timeValid() ? "ok" : "not set";
    h[7] = "offline use";
    const char* items[] = {"wifi", "find city", "set coords", "units", "time zone", "brightness",
                           "sync clock", "set clock"};
    const char* hints[8];
    for (int i = 0; i < 8; i++) hints[i] = h[i].c_str();
    sel = listMenu(P, "settings", items, 8, sel, hints);
    switch (sel) {
      case -1: wifiOff(); return;
      case 0: wifiSetup(); break;
      case 1: locationSearch(); break;
      case 2: locationManual(); break;
      case 3: S.fahrenheit = !S.fahrenheit; saveSettings(); break;
      case 4: tzSetup(); break;
      case 5: brightness(); break;
      case 6:
        message(P, "syncing...", nullptr, nullptr, false);
        if (!wifiOk()) wifiConnect();
        message(P, syncTime() ? "clock synced" : "sync failed", wifiOk() ? nullptr : "no wifi");
        break;
      case 7: manualClock(); break;
    }
  }
}
