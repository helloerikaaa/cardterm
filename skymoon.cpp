// skymoon.cpp - Sun & Moon screen (works offline once the clock is set)
#include "astro.h"
#include "settings.h"
#include "ui.h"
#include <math.h>

static const char* P = "~/sky";
static const char* WD[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char* MO[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

static void hmOrDash(char* b, const astro::Event& e) {
  if (!e.ok) { strcpy(b, "--:--"); return; }
  fmtHM(b, (time_t)e.t + S.tzOff);
}

static void row(int x, int y, const char* label, const char* val, uint16_t lc, uint16_t vc) {
  txt(x, y, label, lc);
  txt(x + 36, y, val, vc);
}

void drawMoonDisc(int cx, int cy, int r, double elong, bool south) {
  double k = cos(elong * M_PI / 180.0);
  bool waxing = elong < 180;
  uint16_t lit = cv.color565(240, 236, 200), dark = cv.color565(38, 40, 46);
  for (int dy = -r; dy <= r; dy++) {
    double w = sqrt((double)r * r - dy * dy);
    for (int dx = -r; dx <= r; dx++) {
      if (dx * dx + dy * dy > r * r) continue;
      double x = south ? -dx : dx;
      bool on = waxing ? (x >= w * k) : (x <= -w * k);
      cv.drawPixel(cx + dx, cy + dy, on ? lit : dark);
    }
  }
  // a few maria so it reads as the moon, only on the lit part
  const int8_t mar[][3] = {{-5, -4, 4}, {3, -6, 3}, {4, 2, 4}, {-3, 6, 2}, {-8, 3, 2}};
  uint16_t sea = cv.color565(200, 196, 165);
  for (auto& m : mar) {
    int rr = m[2] * r / 15;
    for (int dy = -rr; dy <= rr; dy++)
      for (int dx = -rr; dx <= rr; dx++) {
        if (dx * dx + dy * dy > rr * rr) continue;
        int px = m[0] * r / 15 + dx, py = m[1] * r / 15 + dy;
        if (px * px + py * py > r * r) continue;
        if (cv.readPixel(cx + px, cy + py) == lit) cv.drawPixel(cx + px, cy + py, sea);
      }
  }
  cv.drawCircle(cx, cy, r, cv.color565(90, 90, 80));
}

static void sunArc(int x0, int y0, int w, int h, const astro::SunDay& s, double now, bool today) {
  // horizon
  cv.drawFastHLine(x0, y0 + h, w, col::dim);
  int cx = x0 + w / 2, rx = w / 2 - 4;
  for (int a = 0; a <= 180; a += 6) {
    double ra = a * M_PI / 180;
    cv.drawPixel(cx + (int)(rx * cos(ra)), y0 + h - (int)(h * sin(ra)), col::amber);
  }
  if (!today) return;
  if (s.rise.ok && s.set.ok && now > s.rise.t && now < s.set.t) {
    double f = (now - s.rise.t) / (s.set.t - s.rise.t);
    double ra = M_PI * (1 - f);
    int px = cx + (int)(rx * cos(ra)), py = y0 + h - (int)(h * sin(ra));
    cv.fillCircle(px, py, 4, col::yellow);
    cv.drawCircle(px, py, 6, col::amber);
  } else {
    txt(cx - 15, y0 + h - 12, "night", col::dim);
  }
}

static void fmtPhaseDate(char* b, double t) {
  time_t lt = (time_t)t + S.tzOff;
  struct tm tmv;
  gmtime_r(&lt, &tmv);
  sprintf(b, "%s %s %d", WD[tmv.tm_wday], MO[tmv.tm_mon], tmv.tm_mday);
}

void runSky() {
  if (!timeValid()) {
    message(P, "clock not set", "connect wifi once to sync time", "settings > wifi");
    return;
  }
  int offsetDays = 0;
  bool dirty = true;
  uint32_t lastDraw = 0;
  for (;;) {
    if (dirty || millis() - lastDraw > 5000) {
      double now = time(nullptr);
      long localNow = (long)now + S.tzOff;
      double dayStart = floor(localNow / 86400.0) * 86400.0 - S.tzOff + offsetDays * 86400.0;
      astro::SunDay s = astro::sunDay(dayStart, S.lat, S.lon);
      astro::SunDay y = astro::sunDay(dayStart - 86400, S.lat, S.lon);
      astro::MoonDay m = astro::moonDay(dayStart, S.lat, S.lon);
      double ref = offsetDays == 0 ? now : dayStart + 12 * 3600;
      astro::MoonInfo mi = astro::moonInfo(ref);

      clearBody();
      header(P);
      // date line
      time_t dl = (time_t)(dayStart + S.tzOff + 3600);
      struct tm tmv;
      gmtime_r(&dl, &tmv);
      char d[40];
      snprintf(d, sizeof d, "< %s %d %s %d >", WD[tmv.tm_wday], tmv.tm_mday, MO[tmv.tm_mon],
               tmv.tm_year + 1900);
      txtC(13, d, col::amber);
      if (offsetDays == 0) txt(2, 13, "today", col::dim);
      else txtf(2, 13, col::dim, SMALL, "%+dd", offsetDays);

      cv.drawFastVLine(119, 24, 100, col::faint);

      // ---- SUN ----
      txt(2, 24, "SUN", col::yellow);
      sunArc(6, 30, 108, 24, s, now, offsetDays == 0);
      char b[16];
      int yy = 60;
      if (s.alwaysUp) { txt(2, yy, "midnight sun", col::yellow); yy += 10; }
      else if (s.alwaysDown) { txt(2, yy, "polar night", col::cyan); yy += 10; }
      hmOrDash(b, s.dawn);  row(2, yy, "dawn", b, col::dim, col::fg); yy += 10;
      hmOrDash(b, s.rise);  row(2, yy, "rise", b, col::amber, col::white); yy += 10;
      fmtHM(b, (time_t)s.noon + S.tzOff); row(2, yy, "noon", b, col::dim, col::fg); yy += 10;
      hmOrDash(b, s.set);   row(2, yy, "set", b, col::amber, col::white); yy += 10;
      hmOrDash(b, s.dusk);  row(2, yy, "dusk", b, col::dim, col::fg); yy += 10;
      if (s.rise.ok && s.set.ok && yy < 120) {
        int len = (int)((s.set.t - s.rise.t) / 60);
        int diff = 0;
        if (y.rise.ok && y.set.ok) diff = len - (int)((y.set.t - y.rise.t) / 60);
        snprintf(b, sizeof b, "%dh%02dm", len / 60, len % 60);
        row(2, yy, "day", b, col::dim, col::cyan);
        txtf(84, yy, col::dim, SMALL, "%+dm", diff);
      }

      // ---- MOON ----
      int mx = 124;
      txt(mx, 24, "MOON", col::cyan);
      drawMoonDisc(mx + 18, 52, 15, mi.elongation, S.lat < 0);
      txtf(mx + 40, 36, col::white, BIG, "%d%%", (int)lround(mi.fraction * 100));
      txtf(mx + 40, 54, col::dim, SMALL, "%.1f days", mi.ageDays);
      txt(mx, 72, astro::phaseName(mi.elongation), col::amber);
      yy = 84;
      if (m.alwaysUp) { txt(mx, yy, "up all day", col::fg); yy += 10; }
      else if (m.alwaysDown) { txt(mx, yy, "down all day", col::fg); yy += 10; }
      else {
        // list in chronological order
        bool riseFirst = !m.set.ok || (m.rise.ok && m.rise.t < m.set.t);
        for (int k = 0; k < 2; k++) {
          bool isRise = (k == 0) == riseFirst;
          hmOrDash(b, isRise ? m.rise : m.set);
          row(mx, yy, isRise ? "rise" : "set", b, col::cyan, col::white);
          yy += 10;
        }
      }
      double nf = astro::nextPhase(ref - 86400 * 0.5, 180);
      double nn = astro::nextPhase(ref - 86400 * 0.5, 0);
      char pd[20];
      fmtPhaseDate(pd, nf); row(mx, yy, "full", pd, col::dim, col::fg); yy += 10;
      fmtPhaseDate(pd, nn); row(mx, yy, "new", pd, col::dim, col::fg);

      footer("[,/] day  [t] today  [`] back");
      present();
      dirty = false;
      lastDraw = millis();
    }
    Key k = pollKey();
    if (!k) { delay(20); continue; }
    dirty = true;
    if (k.code == K_ESC || k.code == K_BACK) return;
    if (k.code == K_LEFT || k.code == K_UP) offsetDays--;
    if (k.code == K_RIGHT || k.code == K_DOWN) offsetDays++;
    if (k.ch == 't') offsetDays = 0;
  }
}
