// weather.h - Open-Meteo forecast + "what to wear" advice
#pragma once
#include <Arduino.h>

struct WxHour { time_t t; float temp, feels, wind, uv; uint8_t pop, code; };
struct WxDay { time_t t; float tmax, tmin, precip, uv, wind; uint8_t pop, code; };

struct Weather {
  bool ok = false;
  uint32_t fetchedMs = 0;
  float temp, feels, wind, gusts, precip, uv;
  uint8_t hum, code;
  bool isDay;
  WxHour hours[24];
  int nh = 0;
  WxDay days[4];
  int nd = 0;
};
extern Weather WX;

bool weatherFetch(String& err);
void weatherInvalidate();
const char* wmoText(uint8_t code);
String fmtTemp(float c);  // "23" in the user's unit, no symbol
// one-liner for the dashboard; empty if no data
String weatherSummary();
String outfitHeadline();
void runWeather();
