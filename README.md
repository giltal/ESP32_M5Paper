# ESP32_M5Paper

M5Stack's [M5Paper factory test](https://github.com/m5stack/M5Paper_FactoryTest)
ported from Arduino/PlatformIO to **ESP-IDF 5.5** with
[M5Unified](https://github.com/m5stack/M5Unified) and
[M5GFX](https://github.com/m5stack/M5GFX), plus:

- a **To-Do** app (replaces LifeGame) with the time and date in its title
  bar, shown on the e-paper while the device is off
- WiFi login and to-do list stored as plain text on the **SD card**
- clock in **Israel time** with automatic daylight saving
- SD card fixes for the M5Paper's shared SPI bus, including hot-plug
- a direct IT8951 display driver: faster screen updates (a full-screen
  update takes ~160 ms instead of ~250 ms) and all eight e-paper waveforms

Target: M5Paper v1.0 / v1.1 (ESP32-D0WDQ6-V3, 16 MB flash, 8 MB PSRAM,
IT8951 e-paper controller, GT911 touch). The board schematic is in
[documents/Paper_v1.1.pdf](documents/Paper_v1.1.pdf).

## Build and flash

Requires ESP-IDF **v5.5.x** (tested with v5.5.2). M5Unified, M5GFX and
FreeType are fetched by the IDF component manager on the first build
(versions pinned in `dependencies.lock`).

```bash
idf.py set-target esp32
idf.py build
idf.py -p <PORT> -b 115200 flash monitor
```

- `<PORT>` is the M5Paper's USB serial port (CH9102 or CP2104), e.g. `COM12`
  or `/dev/ttyUSB0`.
- Flash at **115200** baud. Faster rates sometimes fail mid-write on this
  board, which leaves an unbootable app; re-flashing at 115200 recovers it.
- All configuration is in `sdkconfig.defaults` (`sdkconfig` is generated and
  not committed). Notable settings: 32 KB main task stack (FreeType renders
  text on it), PSRAM enabled, WiFi IRAM optimisations off (IRAM is full).

On Windows, run the commands from a shell where the ESP-IDF environment is
loaded (`export.ps1` / ESP-IDF PowerShell).

## SD card

Use a microSD card formatted **FAT32** (cards of 32 GB or less are FAT32 by
default; exFAT is not supported). Without a card the device still works, but
WiFi logins and the to-do list are not remembered.

Cards can be inserted and removed while the device is on. Wait about a
second after inserting before opening **Storage** or **To-Do**.

| File | Purpose |
|---|---|
| `/wifi.txt` | WiFi login, written when you connect from the WLAN screen |
| `/todo.txt` | The to-do list |
| `/font.ttf` | Optional TrueType font (see below) |
| `*.txt` | Opened in the text reader from **Storage** |
| `*.jpg`, `*.png`, `*.bmp` | Opened in the picture viewer from **Storage** |

### `/wifi.txt`

```text
ssid=MyNetwork
password=my-secret
```

The password is stored in plain text; anyone with the card can read it.

### `/todo.txt`

One task per line, open (`[ ]`) or done (`[x]`):

```text
[ ] Move libs to Git
[x] Order SD card
Buy milk
```

Lines without a prefix (e.g. typed on a PC) load as open tasks. The list
holds up to 11 tasks of up to 60 bytes each. It is re-read every time the
To-Do screen opens, so edits made on a PC show up straight away.

### `/font.ttf`

As in the original firmware, a TrueType font in the card's root switches the
app from factory-test mode to full mode. Full mode enables the Japanese and
Chinese UI (use a font with CJK glyphs) and connects to the saved WiFi
network at boot. Without it, the built-in Latin font is used and the UI is
English.

## Using the device

- **Power on (battery):** press the centre of the side rocker switch.
- **Power off:** *Setting → Shutdown*, or automatically after 5 minutes
  without input (a warning appears 10 s before). The to-do list, with the
  time of shutdown, stays on the e-paper while the device is off; without an
  SD card the M5Stack logo is shown instead. The device waits about 5 s for
  that screen to finish drawing before it cuts power. On USB power the board
  cannot switch itself off; it goes to deep sleep instead.
- **Time:** *Setting → Sync Time* sets the RTC over NTP (WiFi must be
  connected). The RTC keeps UTC; the display follows Israel time (IST/IDT),
  including daylight saving changes, without re-syncing.
- **To-Do:** tap a task to tick it, **X** deletes it, **+ Add** opens the
  keyboard, **Clear done** removes ticked tasks. The title bar shows the
  current time and date (updated every minute, in the UI language).
- **Screen test:** hold the side rocker switch towards G39 (right) while the
  device starts to run M5Stack's panel life test.

## Project layout

```text
main/
  main.cpp            app_main: NVS, M5Unified, display, then the GUI loop
  app/                the factory test UI, close to M5Stack's original
    epdgui/           widgets (buttons, switches, keyboard, text box)
    frame/            screens (main menu, settings, WiFi, storage, To-Do, ...)
    resources/        images and the built-in font
  port/               replaces M5EPD and the Arduino core
    epd_canvas.*      16-gray canvas with M5EPD's buffer layout + FreeType text
    epd_display.*     full-screen shadow buffer, pushed to the panel by it8951.*
    it8951.*          direct IT8951 driver (4bpp loads, waveforms, busy flag)
    board.*           SD card, SHT30, WiFi, NTP, Grove port check
    font_render.*     FreeType glyph cache (from M5EPD)
bootloader_components/
  m5paper_power_hold/ latches the power-hold pin (GPIO2) at boot
documents/            M5Paper v1.1 product sheet and schematic
```

## Hardware notes

- The SD card shares SCK/MOSI/MISO (GPIO14/12/13) with the IT8951. The card
  runs at 10 MHz; 20 MHz corrupts reads on this board.
- The card's CMD/CLK pins are the display's MOSI/SCK. A card that powers up
  while the display is updating can lock up until it is power-cycled (its
  3.3 V stays on while USB is connected), so on insertion the firmware keeps
  the display off the bus until the card is in SPI mode.
- SD card-detect is GPIO34 (low = card present).
- Screen updates go straight to the IT8951 (`it8951.cpp`): 4bpp rows at
  20 MHz with the requested waveform. M5GFX still initialises the panel and
  reads touch, and is used for updates if the IT8951 cannot be read at
  start-up. The IT8951 busy register reads idle for a few ms after a
  refresh command is issued, so waits first give the command time to start.

## License

The factory test UI and the font renderer come from M5Stack's
M5Paper_FactoryTest and M5EPD, both MIT licensed; see
[main/app/LICENSE.M5Paper_FactoryTest](main/app/LICENSE.M5Paper_FactoryTest)
and [main/port/LICENSE.M5EPD](main/port/LICENSE.M5EPD).
