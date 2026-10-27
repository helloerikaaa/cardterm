// astro.h - offline sun & moon calculations (no network needed).
// Times are Unix seconds (UTC). Accuracy: ~1-2 min for the sun,
// a few minutes for moon rise/set, < 1 h for moon phase dates.
#pragma once

namespace astro {

struct Event {
  bool ok = false;      // false when the event doesn't happen that day
  double t = 0;         // unix seconds UTC
};

struct SunDay {
  Event dawn, rise, set, dusk;   // civil twilight + sunrise/sunset
  Event goldenEnd, goldenStart;  // morning golden hour ends / evening starts
  double noon = 0;               // solar noon (unix)
  double noonAlt = 0;            // sun altitude at noon, degrees
  bool alwaysUp = false, alwaysDown = false;
};

struct MoonDay {
  Event rise, set;
  bool alwaysUp = false, alwaysDown = false;
};

struct MoonInfo {
  double elongation;  // 0..360 deg, 0 = new, 180 = full
  double fraction;    // illuminated fraction 0..1
  double ageDays;     // days since new moon (approx)
  double distanceKm;
};

// dayStart = unix time of local midnight for the day you want.
SunDay sunDay(double dayStart, double lat, double lon);
MoonDay moonDay(double dayStart, double lat, double lon);
MoonInfo moonInfo(double t);

double sunAltitude(double t, double lat, double lon);   // degrees
double moonAltitude(double t, double lat, double lon);  // degrees

// Next time (after t) when the moon reaches the given phase angle
// (0 = new, 90 = first quarter, 180 = full, 270 = last quarter).
double nextPhase(double t, double targetDeg);

const char* phaseName(double elongation);

}  // namespace astro
