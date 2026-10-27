// astro.cpp - low-precision solar and lunar positions (Astronomical Almanac
// style series) plus a generic rise/set finder using hourly quadratic
// interpolation of altitude.
#include "astro.h"
#include <math.h>

namespace astro {

static const double PI_ = 3.14159265358979323846;
static const double RAD = PI_ / 180.0;
static const double DEG = 180.0 / PI_;
static const double OBL = 23.4397 * RAD;  // obliquity of the ecliptic

static double norm360(double a) {
  a = fmod(a, 360.0);
  return a < 0 ? a + 360.0 : a;
}

// days since J2000.0
static double daysJ2000(double t) { return t / 86400.0 + 2440587.5 - 2451545.0; }

struct Eq { double ra, dec, dist; };

static void eclToEq(double l, double b, double& ra, double& dec) {
  ra = atan2(sin(l) * cos(OBL) - tan(b) * sin(OBL), cos(l));
  dec = asin(sin(b) * cos(OBL) + cos(b) * sin(OBL) * sin(l));
}

// Sun ecliptic longitude (radians)
static double sunLon(double d) {
  double M = RAD * (357.5291 + 0.98560028 * d);
  double C = RAD * (1.9148 * sin(M) + 0.02 * sin(2 * M) + 0.0003 * sin(3 * M));
  double P = RAD * 102.9372;
  return M + C + P + PI_;
}

static Eq sunEq(double d) {
  Eq e;
  eclToEq(sunLon(d), 0, e.ra, e.dec);
  e.dist = 149598000.0;
  return e;
}

// Moon ecliptic longitude/latitude (radians) & distance (km)
static void moonEcl(double d, double& l, double& b, double& dist) {
  double L = 218.316 + 13.176396 * d;   // mean longitude
  double M = RAD * (134.963 + 13.064993 * d);   // moon mean anomaly
  double F = RAD * (93.272 + 13.229350 * d);    // argument of latitude
  double D = RAD * (297.850 + 12.190749 * d);   // mean elongation
  double Ms = RAD * (357.529 + 0.98560028 * d); // sun mean anomaly
  double lon = L + 6.289 * sin(M) + 1.274 * sin(2 * D - M) + 0.658 * sin(2 * D) +
               0.214 * sin(2 * M) - 0.186 * sin(Ms) - 0.114 * sin(2 * F);
  double lat = 5.128 * sin(F) + 0.281 * sin(M + F) + 0.278 * sin(M - F) +
               0.173 * sin(2 * D - F);
  l = RAD * lon;
  b = RAD * lat;
  dist = 385001 - 20905 * cos(M) - 3699 * cos(2 * D - M) - 2956 * cos(2 * D);
}

static Eq moonEq(double d) {
  double l, b, dist;
  moonEcl(d, l, b, dist);
  Eq e;
  eclToEq(l, b, e.ra, e.dec);
  e.dist = dist;
  return e;
}

static double siderealTime(double d, double lw) {
  return RAD * (280.16 + 360.9856235 * d) - lw;
}

static double altitudeOf(const Eq& e, double d, double lat, double lon) {
  double lw = RAD * -lon, phi = RAD * lat;
  double H = siderealTime(d, lw) - e.ra;
  return asin(sin(phi) * sin(e.dec) + cos(phi) * cos(e.dec) * cos(H));
}

double sunAltitude(double t, double lat, double lon) {
  double d = daysJ2000(t);
  return altitudeOf(sunEq(d), d, lat, lon) * DEG;
}

double moonAltitude(double t, double lat, double lon) {
  double d = daysJ2000(t);
  Eq e = moonEq(d);
  double h = altitudeOf(e, d, lat, lon);
  // topocentric parallax correction
  double par = asin(6378.14 / e.dist);
  h -= par * cos(h);
  // atmospheric refraction (Bennett), only near/above horizon
  double hd = h * DEG;
  if (hd > -1.0) {
    double r = 1.02 / tan(RAD * (hd + 10.3 / (hd + 5.11))) / 60.0;
    hd += r;
  }
  return hd;
}

// Finds the first upward and downward crossing of f(t) == h0 in the 24 h
// after t0, scanning in 1-hour steps with quadratic interpolation.
template <typename F>
static void crossings(double t0, double h0, F f, Event& up, Event& down,
                      bool& alwaysUp, bool& alwaysDown) {
  const double H = 3600.0;
  double y0 = f(t0) - h0;
  bool allAbove = y0 > 0, allBelow = y0 <= 0;
  up.ok = down.ok = false;
  for (int i = 1; i <= 24; i += 2) {
    double y1 = f(t0 + i * H) - h0;
    double y2 = f(t0 + (i + 1) * H) - h0;
    if (y1 > 0 || y2 > 0) allBelow = false;
    if (y1 <= 0 || y2 <= 0) allAbove = false;
    double a = (y0 + y2) / 2 - y1;
    double b = (y2 - y0) / 2;
    double xe = -b / (2 * a);
    double ye = (a * xe + b) * xe + y1;
    double disc = b * b - 4 * a * y1;
    int roots = 0;
    double x1 = 0, x2 = 0;
    if (fabs(a) < 1e-12) {
      // essentially linear
      if (b != 0) {
        double x = -y1 / b;
        if (fabs(x) <= 1) { roots = 1; x1 = x; ye = y2; }
      }
    } else if (disc >= 0) {
      double dx = sqrt(disc) / (fabs(a) * 2);
      x1 = xe - dx;
      x2 = xe + dx;
      if (fabs(x1) <= 1) roots++;
      if (fabs(x2) <= 1) roots++;
      if (x1 < -1) x1 = x2;
    }
    if (roots == 1) {
      double tt = t0 + (i + x1) * H;
      if (y0 < 0) { if (!up.ok) { up.ok = true; up.t = tt; } }
      else { if (!down.ok) { down.ok = true; down.t = tt; } }
    } else if (roots == 2) {
      double ta = t0 + (i + (ye < 0 ? x2 : x1)) * H;
      double tb = t0 + (i + (ye < 0 ? x1 : x2)) * H;
      if (!up.ok) { up.ok = true; up.t = ta; }
      if (!down.ok) { down.ok = true; down.t = tb; }
    }
    y0 = y2;
  }
  alwaysUp = !up.ok && !down.ok && allAbove;
  alwaysDown = !up.ok && !down.ok && allBelow;
}

SunDay sunDay(double dayStart, double lat, double lon) {
  SunDay s;
  auto f = [&](double t) { return sunAltitude(t, lat, lon); };
  bool au, ad, x1, x2;
  crossings(dayStart, -0.833, f, s.rise, s.set, au, ad);
  s.alwaysUp = au; s.alwaysDown = ad;
  crossings(dayStart, -6.0, f, s.dawn, s.dusk, x1, x2);
  crossings(dayStart, 6.0, f, s.goldenEnd, s.goldenStart, x1, x2);
  // solar noon: coarse scan then refine
  double best = dayStart, bestAlt = -1e9;
  for (int m = 0; m < 24 * 60; m += 10) {
    double a = f(dayStart + m * 60.0);
    if (a > bestAlt) { bestAlt = a; best = dayStart + m * 60.0; }
  }
  for (int m = -10; m <= 10; m++) {
    double t = best + m * 60.0;
    double a = f(t);
    if (a > bestAlt) { bestAlt = a; s.noon = t; }
  }
  if (s.noon == 0) s.noon = best;
  s.noonAlt = bestAlt;
  return s;
}

MoonDay moonDay(double dayStart, double lat, double lon) {
  MoonDay m;
  auto f = [&](double t) { return moonAltitude(t, lat, lon); };
  // altitude already includes parallax+refraction; 0.27 deg = semi-diameter
  crossings(dayStart, -0.27, f, m.rise, m.set, m.alwaysUp, m.alwaysDown);
  return m;
}

static double elongationDeg(double t) {
  double d = daysJ2000(t);
  double l, b, dist;
  moonEcl(d, l, b, dist);
  return norm360((l - sunLon(d)) * DEG);
}

MoonInfo moonInfo(double t) {
  double d = daysJ2000(t);
  Eq s = sunEq(d), m = moonEq(d);
  double phi = acos(sin(s.dec) * sin(m.dec) +
                    cos(s.dec) * cos(m.dec) * cos(s.ra - m.ra));
  double inc = atan2(s.dist * sin(phi), m.dist - s.dist * cos(phi));
  MoonInfo mi;
  mi.fraction = (1 + cos(inc)) / 2;
  mi.elongation = elongationDeg(t);
  mi.ageDays = mi.elongation / 360.0 * 29.530588;
  mi.distanceKm = m.dist;
  return mi;
}

double nextPhase(double t, double target) {
  // step forward until the (elongation - target) wraps through zero
  auto g = [&](double tt) {
    double x = norm360(elongationDeg(tt) - target);
    return x > 180 ? x - 360 : x;  // -180..180
  };
  double step = 6 * 3600.0;
  double a = t + 60, ga = g(a);
  for (int i = 0; i < 4 * 31; i++) {
    double b = a + step, gb = g(b);
    if (ga < 0 && gb >= 0 && gb - ga < 90) {
      for (int k = 0; k < 30; k++) {  // bisection
        double m = (a + b) / 2, gm = g(m);
        if (gm < 0) { a = m; } else { b = m; }
      }
      return (a + b) / 2;
    }
    a = b; ga = gb;
  }
  return 0;
}

const char* phaseName(double e) {
  if (e < 6 || e >= 354) return "New Moon";
  if (e < 84) return "Waxing Crescent";
  if (e < 96) return "First Quarter";
  if (e < 174) return "Waxing Gibbous";
  if (e < 186) return "Full Moon";
  if (e < 264) return "Waning Gibbous";
  if (e < 276) return "Last Quarter";
  return "Waning Crescent";
}

}  // namespace astro
