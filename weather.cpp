#include "weather.h"
#include "outfit.h"
#include "settings.h"
#include "ui.h"
#include <ArduinoJson.h>

Weather WX;
static const char* P = "~/weather";

void weatherInvalidate() { WX.ok = false; WX.fetchedMs = 0; }

const char* wmoText(uint8_t c) {
  switch (c) {
    case 0: return "Clear sky";
    case 1: return "Mainly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Light showers";
    case 81: return "Showers";
    case 82: return "Violent showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Storm + hail";
  }
  return "Unknown";
}

static float toU(float c) { return S.fahrenheit ? c * 9 / 5 + 32 : c; }
String fmtTemp(float c) {
  float v = toU(c);
  return String((int)lroundf(v));
}
static int windU(float kmh) { return (int)lroundf(S.fahrenheit ? kmh / 1.609f : kmh); }
static const char* windUnit() { return S.fahrenheit ? "mph" : "km/h"; }

bool weatherFetch(String& err) {
  char url[640];
  snprintf(url, sizeof url,
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,relative_humidity_2m,apparent_temperature,precipitation,"
           "weather_code,wind_speed_10m,wind_gusts_10m,uv_index,is_day"
           "&hourly=temperature_2m,apparent_temperature,precipitation_probability,weather_code,"
           "wind_speed_10m,uv_index&forecast_hours=24"
           "&daily=weather_code,temperature_2m_max,temperature_2m_min,"
           "precipitation_probability_max,precipitation_sum,uv_index_max,wind_speed_10m_max"
           "&forecast_days=4&timezone=auto&timeformat=unixtime",
           S.lat, S.lon);
  String body;
  if (!httpGet(url, body, err)) return false;
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, body);
  body = String();
  if (e) { err = e.c_str(); return false; }
  if (doc["error"] | false) { err = doc["reason"] | "api error"; return false; }

  JsonObject c = doc["current"];
  WX.temp = c["temperature_2m"] | 0.0f;
  WX.feels = c["apparent_temperature"] | WX.temp;
  WX.hum = c["relative_humidity_2m"] | 0;
  WX.precip = c["precipitation"] | 0.0f;
  WX.code = c["weather_code"] | 0;
  WX.wind = c["wind_speed_10m"] | 0.0f;
  WX.gusts = c["wind_gusts_10m"] | 0.0f;
  WX.uv = c["uv_index"] | 0.0f;
  WX.isDay = (c["is_day"] | 1) == 1;

  JsonObject h = doc["hourly"];
  JsonArray ht = h["time"];
  WX.nh = min((int)ht.size(), 24);
  for (int i = 0; i < WX.nh; i++) {
    WxHour& x = WX.hours[i];
    x.t = ht[i].as<long>();
    x.temp = h["temperature_2m"][i] | 0.0f;
    x.feels = h["apparent_temperature"][i] | x.temp;
    x.pop = h["precipitation_probability"][i] | 0;
    x.code = h["weather_code"][i] | 0;
    x.wind = h["wind_speed_10m"][i] | 0.0f;
    x.uv = h["uv_index"][i] | 0.0f;
  }
  JsonObject d = doc["daily"];
  JsonArray dt = d["time"];
  WX.nd = min((int)dt.size(), 4);
  for (int i = 0; i < WX.nd; i++) {
    WxDay& x = WX.days[i];
    x.t = dt[i].as<long>();
    x.code = d["weather_code"][i] | 0;
    x.tmax = d["temperature_2m_max"][i] | 0.0f;
    x.tmin = d["temperature_2m_min"][i] | 0.0f;
    x.pop = d["precipitation_probability_max"][i] | 0;
    x.precip = d["precipitation_sum"][i] | 0.0f;
    x.uv = d["uv_index_max"][i] | 0.0f;
    x.wind = d["wind_speed_10m_max"][i] | 0.0f;
  }
  int off = doc["utc_offset_seconds"] | S.tzOff;
  if (S.tzAuto && off != S.tzOff) { S.tzOff = off; saveSettings(); }
  WX.ok = true;
  WX.fetchedMs = millis();
  return true;
}

// outfit window: next 12 hours, skipping 00:00-05:59 (asleep)
// unless it's already the middle of the night
static bool inWindow(int i) {
  if (i == 0) return true;
  if (i >= 12 || i >= WX.nh) return false;
  int nowH = (int)((WX.hours[0].t + S.tzOff) / 3600 % 24);
  int hr = (int)((WX.hours[i].t + S.tzOff) / 3600 % 24);
  return nowH < 6 || hr >= 6;
}

static void buildOutfit(Outfit& o) {
  static float fe[12], wi[12], uv[12];
  static uint8_t po[12], co[12];
  static time_t tt[12];
  int n = 0;
  for (int i = 0; i < min(WX.nh, 12); i++) {
    if (!inWindow(i)) continue;
    time_t lt = WX.hours[i].t + S.tzOff;
    fe[n] = WX.hours[i].feels; wi[n] = WX.hours[i].wind; uv[n] = WX.hours[i].uv;
    po[n] = WX.hours[i].pop; co[n] = WX.hours[i].code; tt[n] = lt;
    n++;
  }
  OutfitInput in{WX.feels, (float)WX.hum, n, fe, po, co, wi, uv, tt, S.fahrenheit};
  makeOutfit(in, o);
}

String weatherSummary() {
  if (!WX.ok) return "";
  return fmtTemp(WX.temp) + (S.fahrenheit ? "F " : "C ") + wmoText(WX.code);
}

String outfitHeadline() {
  if (!WX.ok) return "";
  Outfit o;
  buildOutfit(o);
  return o.headline;
}

// ---------------- drawing ----------------
// wttr.in-inspired ascii icons, 5 rows x 13 cols in the 6x8 font
enum IconKind { IC_SUN, IC_MOON, IC_PARTLY, IC_CLOUD, IC_FOG, IC_RAIN, IC_SNOW, IC_STORM };

static IconKind iconFor(uint8_t c, bool day) {
  if (c == 0 || c == 1) return day ? IC_SUN : IC_MOON;
  if (c == 2) return IC_PARTLY;
  if (c == 3) return IC_CLOUD;
  if (c == 45 || c == 48) return IC_FOG;
  if (isStormCode(c)) return IC_STORM;
  if (isSnowCode(c)) return IC_SNOW;
  if (isRainCode(c)) return IC_RAIN;
  return IC_CLOUD;
}

static bool night = false;
static void drawIcon(int x, int y, IconKind k) {
  static const char* art[8][5] = {
      {"    \\   /    ", "     .-.     ", "  - (   ) -  ", "     `-'     ", "    /   \\    "},
      {"      _      ", "    /' )   * ", "   (  (      ", " *  \\  `._   ", "     `---'   "},
      {"   \\  /      ", " _ /\"\".-.    ", "   \\_(   ).  ", "   /(___(__) ", "             "},
      {"             ", "     .--.    ", "  .-(    ).  ", " (___.__)__) ", "             "},
      {"             ", " _ - _ - _ - ", "  _ - _ - _  ", " _ - _ - _ - ", "             "},
      {"     .-.     ", "    (   ).   ", "   (___(__)  ", "    ' ' ' '  ", "   ' ' ' '   "},
      {"     .-.     ", "    (   ).   ", "   (___(__)  ", "    *  *  *  ", "   *  *  *   "},
      {"     .-.     ", "    (   ).   ", "   (___(__)  ", "    /_  /_   ", "     /   /   "},
  };
  for (int r = 0; r < 5; r++) {
    uint16_t c = col::white;
    switch (k) {
      case IC_SUN: c = col::yellow; break;
      case IC_MOON: c = col::cyan; break;
      case IC_PARTLY: c = r < 2 ? (night ? col::cyan : col::yellow) : col::white; break;
      case IC_CLOUD: c = col::grey; break;
      case IC_FOG: c = col::grey; break;
      case IC_RAIN: c = r >= 3 ? col::blue : col::grey; break;
      case IC_SNOW: c = r >= 3 ? col::white : col::grey; break;
      case IC_STORM: c = r >= 3 ? col::yellow : col::grey; break;
    }
    txt(x, y + r * 8, art[k][r], c);
  }
  if (k == IC_PARTLY) {  // recolor the sun-ray in row 2
    txt(x, y + 16, "   \\", night ? col::cyan : col::yellow);
  }
}

static void drawDegree(int x, int y, int r, uint16_t c) { cv.drawCircle(x + r, y + r, r, c); }

static void tabs(int page) {
  const char* names[] = {"now", "wear", "hours", "days"};
  int x = 2;
  for (int i = 0; i < 4; i++) {
    char b[12];
    snprintf(b, sizeof b, i == page ? "[%s]" : " %s ", names[i]);
    txt(x, 13, b, i == page ? col::amber : col::dim);
    x += strlen(b) * 6 + 2;
  }
  int room = (W - 2 - x - 4) / 6;
  if (room > 3) {
    String pl = S.place;
    if ((int)pl.length() > room && pl.indexOf(',') > 0) pl = pl.substring(0, pl.indexOf(','));
    pl = pl.substring(0, room);
    txtR(W - 2, 13, pl.c_str(), col::dim);
  }
}

static void pageNow() {
  night = !WX.isDay;
  drawIcon(0, 26, iconFor(WX.code, WX.isDay));
  // big temperature
  String t = fmtTemp(WX.temp);
  cv.setFont(&fonts::AsciiFont8x16);
  cv.setTextSize(2);
  cv.setTextColor(col::white);
  cv.drawString(t, 84, 24);
  int tw = t.length() * 16;
  cv.setTextSize(1);
  drawDegree(84 + tw + 1, 26, 3, col::white);
  txt(84 + tw + 9, 26, S.fahrenheit ? "F" : "C", col::white, BIG);
  int x2 = 168;
  txtf(x2, 24, col::fg, SMALL, "feels %s", fmtTemp(WX.feels).c_str());
  if (WX.nd > 0)
    txtf(x2, 34, col::fg, SMALL, "hi %s lo %s", fmtTemp(WX.days[0].tmax).c_str(),
         fmtTemp(WX.days[0].tmin).c_str());
  txtf(x2, 44, col::fg, SMALL, "hum %d%%", WX.hum);
  txt(84, 58, wmoText(WX.code), col::amber);

  int pop = WX.nh ? WX.hours[0].pop : 0;
  int popToday = WX.nd ? WX.days[0].pop : pop;
  txtf(2, 70, col::cyan, SMALL, "rain %d%%", popToday);
  txtf(64, 70, col::cyan, SMALL, "wind %d%s", windU(WX.wind), windUnit());
  txtf(148, 70, col::cyan, SMALL, "uv %.0f", WX.uv);
  if (WX.nd) txtf(190, 70, col::cyan, SMALL, "max %.0f", WX.days[0].uv);
  hline(81, col::faint);

  Outfit o;
  buildOutfit(o);
  txt(2, 85, "wear>", col::amber);
  txt(38, 85, o.top, col::white);
  txt(38, 95, o.bottom, col::white);
  txt(38, 105, o.shoes, col::white);
  if (o.n > 0) {
    uint16_t c = o.extraLevel[0] == 2 ? col::amber : col::fg;
    String e = String("+ ") + o.extras[0];
    txt(38, 115, e.substring(0, 33).c_str(), c);
  }
}

static void pageWear() {
  Outfit o;
  buildOutfit(o);
  int y = 24;
  txt(2, y, "# what to wear (waking hours, next 12h)", col::dim);
  y += 11;
  txt(2, y, "top", col::amber); txt(44, y, o.top, col::white); y += 10;
  txt(2, y, "bottom", col::amber); txt(44, y, o.bottom, col::white); y += 10;
  txt(2, y, "shoes", col::amber); txt(44, y, o.shoes, col::white); y += 12;
  if (o.n == 0) {
    txt(2, y, "+ nothing special, enjoy it", col::fg);
  }
  for (int i = 0; i < o.n && y < 108; i++) {
    uint16_t c = o.extraLevel[i] == 2 ? col::amber : o.extraLevel[i] == 1 ? col::fg : col::dim;
    txt(2, y, o.extraLevel[i] == 2 ? "!" : "+", c);
    txt(12, y, o.extras[i], c);
    y += 10;
  }
  // the numbers behind the advice
  float lo = WX.feels, hi = WX.feels;
  int pop = 0;
  for (int i = 0; i < min(WX.nh, 12); i++) {
    if (!inWindow(i)) continue;
    lo = min(lo, WX.hours[i].feels);
    hi = max(hi, WX.hours[i].feels);
    pop = max(pop, (int)WX.hours[i].pop);
  }
  txtf(2, 116, col::dim, SMALL, "feels %s now, range %s..%s, rain %d%%",
       fmtTemp(WX.feels).c_str(), fmtTemp(lo).c_str(), fmtTemp(hi).c_str(), pop);
}

static void pageHours() {
  int n = WX.nh;
  if (n < 2) { txt(2, 40, "no hourly data", col::red); return; }
  float lo = 1e9, hi = -1e9;
  for (int i = 0; i < n; i++) {
    lo = min(lo, WX.hours[i].temp);
    hi = max(hi, WX.hours[i].temp);
  }
  if (hi - lo < 4) { hi += 2; lo -= 2; }
  const int x0 = 28, dx = 9, yTop = 34, yBot = 86;
  auto Y = [&](float v) { return (int)(yBot - (v - lo) / (hi - lo) * (yBot - yTop)); };
  for (int y = yTop; y <= yBot; y += 9) cv.drawFastHLine(x0, y, dx * (n - 1), col::faint);
  // rain bars
  for (int i = 0; i < n; i++) {
    int h = WX.hours[i].pop * 22 / 100;
    if (h) cv.fillRect(x0 + i * dx - 3, 112 - h, 7, h, col::blue);
  }
  cv.drawFastHLine(x0 - 4, 112, dx * (n - 1) + 8, col::faint);
  txt(2, 100, "rn", col::blue);
  // temp line (feels dotted)
  for (int i = 1; i < n; i++) {
    int xa = x0 + (i - 1) * dx, xb = x0 + i * dx;
    cv.drawLine(xa, Y(WX.hours[i - 1].temp), xb, Y(WX.hours[i].temp), col::amber);
    cv.drawLine(xa, Y(WX.hours[i - 1].temp) + 1, xb, Y(WX.hours[i].temp) + 1, col::amber);
    if (i % 2 == 0) cv.fillRect(xb, Y(WX.hours[i].feels), 2, 2, col::cyan);
  }
  // labels every 3 hours
  for (int i = 0; i < n; i += 3) {
    struct tm tmv;
    time_t lt = WX.hours[i].t + S.tzOff;
    gmtime_r(&lt, &tmv);
    char b[4];
    snprintf(b, sizeof b, "%d", tmv.tm_hour);
    int x = x0 + i * dx - (strlen(b) * 6) / 2;
    txt(x, 115, b, col::dim);
    txt(x0 + i * dx - (int)fmtTemp(WX.hours[i].temp).length() * 3,
        Y(WX.hours[i].temp) - 10, fmtTemp(WX.hours[i].temp).c_str(), col::white);
  }
  txt(2, 88, "temp", col::amber);
  txt(2, 76, "feel", col::cyan);
}

static void pageDays() {
  static const char* wd[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  for (int i = 0; i < WX.nd; i++) {
    const WxDay& d = WX.days[i];
    int y = 24 + i * 25;
    struct tm tmv;
    time_t lt = d.t + S.tzOff + 6 * 3600;  // midday avoids edge issues
    gmtime_r(&lt, &tmv);
    txt(2, y, i == 0 ? "Tdy" : wd[tmv.tm_wday], i == 0 ? col::amber : col::fg, BIG);
    txt(34, y, wmoText(d.code), col::white);
    txtf(34, y + 9, col::cyan, SMALL, "rain %d%%  uv %.0f  wind %d", d.pop, d.uv, windU(d.wind));
    String hl = fmtTemp(d.tmax) + "/" + fmtTemp(d.tmin);
    txtR(W - 2, y, hl.c_str(), col::white, BIG);
    if (i < WX.nd - 1) hline(y + 21, col::faint);
  }
}

void runWeather() {
  int page = 0;
  bool dirty = true;
  uint32_t lastDraw = 0;
  // auto refresh when stale (> 20 min)
  if (!WX.ok || millis() - WX.fetchedMs > 20 * 60 * 1000UL) {
    message(P, "fetching forecast...", S.place.c_str(), nullptr, false);
    String err;
    if (!weatherFetch(err) && !WX.ok) {
      wifiOff();
      message(P, "no weather data", err.c_str(),
              S.ssid.isEmpty() ? "set up wifi in settings" : "[r] retry later", true, col::red);
      return;
    }
  }
  for (;;) {
    if (dirty || millis() - lastDraw > 1000) {
      clearBody();
      header(P);
      tabs(page);
      switch (page) {
        case 0: pageNow(); break;
        case 1: pageWear(); break;
        case 2: pageHours(); break;
        case 3: pageDays(); break;
      }
      char f[48];
      uint32_t mins = (millis() - WX.fetchedMs) / 60000;
      snprintf(f, sizeof f, "[,/] page [r] refresh [`] back  %lum", (unsigned long)mins);
      footer(f);
      present();
      dirty = false;
      lastDraw = millis();
    }
    Key k = pollKey();
    if (!k) { delay(15); continue; }
    dirty = true;
    if (k.code == K_ESC || k.code == K_BACK) { wifiOff(); return; }
    if (k.code == K_RIGHT || k.code == K_TAB || k.code == K_DOWN) page = (page + 1) % 4;
    if (k.code == K_LEFT || k.code == K_UP) page = (page + 3) % 4;
    if (k.ch >= '1' && k.ch <= '4') page = k.ch - '1';
    if (k.ch == 'r') {
      message(P, "refreshing...", nullptr, nullptr, false);
      String err;
      if (!weatherFetch(err)) message(P, "refresh failed", err.c_str(), nullptr, true, col::red);
    }
  }
}
