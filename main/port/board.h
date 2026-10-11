// M5Paper board services used by the app that M5Unified does not provide:
// SD card, SHT30 sensor, WiFi station, NTP and the Grove port probe.
#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

#include "esp_timer.h"

// Milliseconds since boot (wraps like Arduino's millis()).
inline uint32_t now_ms() {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

namespace sdcard {
constexpr const char *kMountPoint = "/sd";
// Mounts the card on the panel's SPI bus (CS = GPIO4). Call after M5.begin().
bool mount();
void unmount();
bool mounted();
// Card-detect switch: true while a card is in the slot.
bool present();
// Call regularly (GUI loop): mounts a newly inserted card, unmounts a removed
// one. Returns true when the card state changed.
bool poll();
uint64_t size_bytes();
// App paths are card-relative ("/font.ttf"); this returns the VFS path.
std::string path(const std::string &card_path);
bool exists(const std::string &card_path);
// Whole-file text helpers for small files (settings, lists). write_text()
// writes a temporary file and renames it over the target, so a card pulled
// mid-write leaves either the old or the new contents.
bool read_text(const std::string &card_path, std::string *text);
bool write_text(const std::string &card_path, const std::string &text);
}  // namespace sdcard

// Sensirion SHT30 on the internal I2C bus (0x44).
class Sht30 {
   public:
    // Starts a measurement and reads it back. Returns false on I2C/CRC error.
    bool update();
    bool ok() const {
        return _ok;
    }
    float temperature() const {
        return _temperature;
    }
    float humidity() const {
        return _humidity;
    }

   private:
    bool _ok           = false;
    float _temperature = 0;
    float _humidity    = 0;
};

namespace wifi {
struct ap_t {
    std::string ssid;
    int rssi;
};
// Starts the station interface (idempotent).
void init();
// Blocking scan, strongest first.
std::vector<ap_t> scan();
// Starts connecting; poll connected().
void begin(const std::string &ssid, const std::string &password);
bool connected();
int rssi();
void disconnect();
}  // namespace wifi

// Sets the system clock (UTC) over SNTP.
bool sync_ntp(const char *server, uint32_t timeout_ms);

// True if a BMP280/BMP388-style sensor (0x76, chip id 0x58) answers on the
// given Grove pins; the factory test plugs one into each port.
bool grove_check(int sda, int scl);
