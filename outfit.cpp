#include "outfit.h"
#include <stdio.h>
#include <string.h>

bool isRainCode(uint8_t c) {
  return (c >= 51 && c <= 67) || (c >= 80 && c <= 82) || c >= 95;
}
bool isSnowCode(uint8_t c) { return (c >= 71 && c <= 77) || c == 85 || c == 86; }
bool isStormCode(uint8_t c) { return c >= 95; }

static int band(float f) {  // 0 = hottest .. 6 = coldest
  if (f >= 28) return 0;
  if (f >= 22) return 1;
  if (f >= 17) return 2;
  if (f >= 12) return 3;
  if (f >= 6) return 4;
  if (f >= 0) return 5;
  return 6;
}

static const char* TOPS[] = {"tank top or light tee", "t-shirt", "long sleeve / light shirt",
                             "sweater or hoodie", "jacket over a sweater", "warm winter coat",
                             "heavy coat + thermal layer"};

static const char* layerFor(int b) {
  static const char* L[] = {"", "", "layer", "sweater", "jacket",
                            "warm coat", "heavy coat"};
  return L[b];
}

static int toUnit(float c, bool f) {
  float v = f ? c * 9 / 5 + 32 : c;
  return (int)(v < 0 ? v - 0.5f : v + 0.5f);
}

static void hm(char* buf, time_t t) {
  struct tm tmv;
  gmtime_r(&t, &tmv);
  sprintf(buf, "%d:%02d", tmv.tm_hour, tmv.tm_min);
}

static void add(Outfit& o, int lvl, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
#include <stdarg.h>
static void add(Outfit& o, int lvl, const char* fmt, ...) {
  if (o.n >= 8) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(o.extras[o.n], sizeof o.extras[0], fmt, ap);
  va_end(ap);
  o.extraLevel[o.n++] = lvl;
}

void makeOutfit(const OutfitInput& in, Outfit& o) {
  memset(&o, 0, sizeof o);
  const bool F = in.fahrenheit;
  float now = in.feelsNow;
  float fMin = now, fMax = now, windMax = 0, uvMax = 0;
  int iMin = 0, iMax = 0, popMax = 0, iPop = 0;
  bool rain = false, snow = false, storm = false;
  for (int i = 0; i < in.n; i++) {
    if (in.feels[i] < fMin) { fMin = in.feels[i]; iMin = i; }
    if (in.feels[i] > fMax) { fMax = in.feels[i]; iMax = i; }
    if (in.pop[i] > popMax) { popMax = in.pop[i]; iPop = i; }
    if (in.wind[i] > windMax) windMax = in.wind[i];
    if (in.uv[i] > uvMax) uvMax = in.uv[i];
    if (in.pop[i] >= 40) {
      if (isRainCode(in.code[i])) rain = true;
      if (isSnowCode(in.code[i])) snow = true;
      if (isStormCode(in.code[i])) storm = true;
    }
  }

  int b = band(now);
  // dress for now, but if it gets warmer soon (morning) lean a bit lighter
  strcpy(o.top, TOPS[b]);
  if (now >= 24) strcpy(o.bottom, "shorts or skirt");
  else if (now >= 14) strcpy(o.bottom, "jeans or light pants");
  else if (now >= 4) strcpy(o.bottom, "long pants");
  else strcpy(o.bottom, "lined pants / thermals");

  if (snow) strcpy(o.shoes, "insulated waterproof boots");
  else if (rain && popMax >= 50) strcpy(o.shoes, "waterproof shoes");
  else if (now >= 25) strcpy(o.shoes, "sandals or breathable sneakers");
  else if (now < 4) strcpy(o.shoes, "warm boots");
  else strcpy(o.shoes, "sneakers");

  char tbuf[8];
  // temperature swings (big in high-altitude / desert climates)
  int bMin = band(fMin);
  bool needLayer = bMin > b && (now - fMin) >= 5 && fMin < 20;
  if (needLayer) {
    hm(tbuf, in.t[iMin]);
    add(o, 2, "bring a %s: %d by %s", layerFor(bMin < 2 ? 2 : bMin), toUnit(fMin, F), tbuf);
  }
  if (fMax - now >= 6 && band(fMax) < b) {
    hm(tbuf, in.t[iMax]);
    add(o, 1, "layers: warms to %d by %s", toUnit(fMax, F), tbuf);
  }
  // precipitation
  if (storm && popMax >= 40) {
    hm(tbuf, in.t[iPop]);
    add(o, 2, "storms ~%s (%d%%): stay dry", tbuf, popMax);
  }
  if (snow) {
    add(o, 2, "snow: hat, gloves, boots");
  } else if (storm && popMax >= 40) {
    // already covered by the storm line
  } else if (popMax >= 60) {
    hm(tbuf, in.t[iPop]);
    add(o, 2, "umbrella/rain jacket %d%% ~%s", popMax, tbuf);
  } else if (popMax >= 30) {
    hm(tbuf, in.t[iPop]);
    add(o, 1, "maybe an umbrella %d%% ~%s", popMax, tbuf);
  }
  // wind
  if (windMax >= 40) add(o, 2, "windy %d%s: windbreaker", F ? (int)(windMax / 1.609f) : (int)windMax, F ? "mph" : "km/h");
  else if (windMax >= 25) add(o, 1, "breezy: windproof layer helps");
  // cold extras
  if (fMin < -5) add(o, 2, "scarf, gloves and a beanie");
  else if (fMin < 5) add(o, 1, "gloves + beanie");
  // sun
  if (uvMax >= 8) add(o, 2, "UV %d: SPF50, hat, sunglasses", (int)(uvMax + 0.5f));
  else if (uvMax >= 6) add(o, 1, "UV %d: sunscreen + hat", (int)(uvMax + 0.5f));
  else if (uvMax >= 3) add(o, 0, "UV %d: sunglasses + sunscreen", (int)(uvMax + 0.5f));
  // heat
  if (fMax >= 30) add(o, 1, "hot: carry water");
  if (now >= 26 && in.humidity >= 70) add(o, 0, "humid: linen or cotton");

  // headline: short summary for the dashboard
  const char* shortTop[] = {"tank", "tee", "long sleeve", "sweater", "jacket", "coat", "heavy coat"};
  const char* shortBot = now >= 24 ? "shorts" : now >= 4 ? "pants" : "thermals";
  snprintf(o.headline, sizeof o.headline, "%s + %s%s%s", shortTop[b], shortBot,
           needLayer ? ", layer later" : "",
           snow ? ", boots" : popMax >= 60 ? ", umbrella" : "");
}
