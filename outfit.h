// outfit.h - turns a forecast window into clothing advice (pure C++)
#pragma once
#include <stdint.h>
#include <time.h>

struct OutfitInput {
  float feelsNow;        // apparent temperature now, C
  float humidity;        // %
  // next ~12 hours (index 0 = this hour)
  int n;
  const float* feels;    // apparent temp C
  const uint8_t* pop;    // precipitation probability %
  const uint8_t* code;   // WMO weather code
  const float* wind;     // km/h
  const float* uv;
  const time_t* t;       // local unix times (already shifted)
  bool fahrenheit;       // for text output
};

struct Outfit {
  char top[40];
  char bottom[40];
  char shoes[40];
  char extras[8][44];
  uint8_t extraLevel[8];  // 0 info, 1 suggest, 2 important
  int n;
  char headline[60];
};

void makeOutfit(const OutfitInput& in, Outfit& out);
bool isRainCode(uint8_t c);
bool isSnowCode(uint8_t c);
bool isStormCode(uint8_t c);
