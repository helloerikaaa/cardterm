# CardTerm

A little green-phosphor terminal dashboard for the **M5Stack Cardputer ADV**
(also runs on the original Cardputer — the keyboard is auto-detected).

```
~                              ▮▮▮ ▭ 16:26
16:26   Sunday
        25 Oct 2026
        Guadalupe, ZAC
1 weather           22C Partly cloudy
2 sun+moon          set 18:17 moon 99%
3 sudoku            saved: hard 12:40
4 settings          wifi: MyHouse
$ wear: long sleeve + pants, layer later_
```

## What's inside

| Screen | What it shows |
|---|---|
| **weather** | 4 tabs: **now** (ASCII icon, temp, feels-like, hi/lo, rain, wind, UV + outfit), **wear** (full clothing checklist), **hours** (next 24 h temperature curve + rain bars), **days** (4-day forecast) |
| **what to wear** | Top / bottom / shoes plus extras: bring a layer (with the time it gets cold), umbrella, storms, wind, gloves/beanie, UV protection, water. It looks at your waking hours over the next 12 h, so a cold 3 a.m. doesn't put you in a winter coat at noon — handy for big day/night swings like Zacatecas'. |
| **sun+moon** | Dawn, sunrise, solar noon, sunset, dusk, day length (and change vs. yesterday), live sun position on an arc, moon phase drawing, illumination %, moon rise/set, next full & new moon. Browse other days with `,` `/`. **Calculated on the device — works offline** once the clock is set. |
| **sudoku** | Easy / medium / hard, a fresh puzzle every time with exactly one solution. Pencil notes, hints, optional mistake check, best time per difficulty. Auto-saved, so you can switch off mid-game and continue later. |
| **settings** | WiFi (scan + password), find city by name, manual lat/lon, °C/°F, time zone (auto from the weather service, handles DST), brightness, sync clock, set clock by hand for offline use. |

Weather comes from [Open-Meteo](https://open-meteo.com) — free, no API key, no account.

Default location is Guadalupe, Zacatecas; change it in **settings → find city**.

## Keys

| Key | Action |
|---|---|
| `;` `.` `,` `/` | up / down / left / right (the arrow keys printed on the Cardputer, no Fn needed) |
| `Enter` | select / confirm |
| `` ` `` (or Backspace in menus) | back |
| `1`–`9` | jump straight to a menu item |
| `w` `s` `p` | from the dashboard: weather, sky, sudoku |
| In text boxes | type normally; `Tab` shows/hides a password; `Fn` + `` ` `` cancels |

Sudoku keys are shown on its side panel: `1`–`9` place, `0` clear, `n` notes, `h` hint, `c` check.
The screen dims after 45 s idle; any key wakes it.

## Install

### Option A — flash the prebuilt firmware (easiest)

`firmware/CardTerm-full.bin` is a complete image (bootloader + partitions + app).

1. Plug the Cardputer in with USB-C. If it isn't detected, hold the **G0** button while
   switching it on / plugging it in to enter download mode.
2. Open <https://espressif.github.io/esptool-js/> in Chrome or Edge, click **Connect**, pick the port.
3. Flash address **0x0**, file **CardTerm-full.bin**, click **Program**.
4. Switch it off and on again. First boot: **settings → wifi**.

This erases saved settings on the device. For later updates, flash
`firmware/CardTerm-app.bin` at **0x10000** instead to keep your WiFi and scores.

If you use **M5Launcher** to run apps from the SD card, copy `CardTerm-app.bin` to the
card and launch it from there.

### Option B — build it yourself (Arduino IDE)

1. Boards Manager: install **esp32 by Espressif** (built and tested with **3.2.0**).
2. Library Manager: install **M5Cardputer** (≥ 1.1.1, which added ADV support — it pulls in
   M5Unified and M5GFX) and **ArduinoJson** (7.x).
3. Open `CardTerm/CardTerm.ino`. Board: **M5Cardputer**.
   Tools → Partition Scheme: **Huge APP (3MB No OTA/1MB SPIFFS)**
   (the default 1.2 MB app slot is too small).
   ⚠️ Don't pick the "8M with spiffs" scheme unless you also set **Tools → Flash Size → 8MB**.
   The Flash Size menu defaults to 4MB, and that mismatch makes the board reboot forever
   with a black screen and no serial output.
4. Upload.

### Option C — PlatformIO

`platformio.ini` is included (`pio run -t upload`). It targets the pioarduino
platform (Arduino core 3.x). The Arduino IDE route above is the one I compiled and tested.

## Notes

- **No real-time clock** on the Cardputer ADV, so time comes from the internet at boot.
  WiFi is switched off afterwards to save battery and reconnects by itself when you refresh
  the weather. Without WiFi, use **settings → set clock** and the sun/moon screen keeps working.
- Sun times match a professional ephemeris (PyEphem) within ~2 minutes; moon rise/set within
  ~10 minutes at mid-latitudes; next full/new moon within about an hour.
- Weather is fetched over HTTPS without certificate pinning (it's public, read-only data).

## Files

```
CardTerm/
  CardTerm.ino      boot sequence + dashboard
  ui.*              terminal drawing, keyboard, menus, text input
  settings.*        WiFi, NTP, location search, preferences
  weather.*         Open-Meteo client + the four weather tabs
  outfit.*          clothing rules (plain C++, easy to tweak)
  skymoon.cpp       sun & moon screen
  astro.*           offline solar/lunar math
  sudoku.cpp        the Sudoku game
  sudoku_core.*     puzzle generator/solver (iterative, low memory)
```

Want different clothing advice? The thresholds live at the top of `outfit.cpp`.
