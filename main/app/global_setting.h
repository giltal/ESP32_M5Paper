#ifndef _GLOBAL_SETTING_H_
#define _GLOBAL_SETTING_H_

#include "m5paper_port.h"
#include <nvs.h>
#include <ctime>
#include <string>
#define WALLPAPER_NUM 3

// Power off automatically after a period without touch or button input
#define ENABLE_AUTO_POWER_SAVE true
// Idle time until power off (the reference app used 70 s)
static const uint32_t TIME_BEFORE_SHUTDOWN_MS = 5 * 60 * 1000;
// How long the "Shutdown to save power" warning is shown before that
static const uint32_t SHUTDOWN_PROMPT_DELAY_MS = 10 * 1000;
static const uint32_t TIME_BEFORE_SHUTDOWN_PROMPT_MS =
    TIME_BEFORE_SHUTDOWN_MS - SHUTDOWN_PROMPT_DELAY_MS;

enum {
    LANGUAGE_EN = 0,  // default, English
    LANGUAGE_JA,      // Japanese
    LANGUAGE_ZH       // Simplified Chinese
};

void SetLanguage(uint8_t language);
uint8_t GetLanguage(void);
void SetWallpaper(uint16_t wallpaper_id);
uint16_t GetWallpaperID(void);
const uint8_t* GetWallpaper(void);
const char* GetWallpaperName(uint16_t wallpaper_id);

esp_err_t LoadSetting(void);
esp_err_t SaveSetting(void);
// Credentials are stored on the SD card (/wifi.txt); nothing is remembered
// without a card. LoadWifiConfig() reads them (call after mounting the card).
void SetWifi(std::string ssid, std::string password);
void LoadWifiConfig(void);
std::string GetWifiSSID(void);
std::string GetWifiPassword(void);
uint8_t isWiFiConfiged(void);
bool SyncNTPTime(void);

// The RTC keeps UTC; local time follows the Israel time zone rules
// (IST UTC+2, IDT UTC+3), so daylight saving changes need no re-sync.
void InitTimeZone(void);
bool GetRtcLocalTime(struct tm *local);
const char *GetTimeZoneName(bool is_dst);
uint16_t GetTextSize();
void SetTextSize(uint16_t size);

const uint8_t* GetLoadingIMG_32x32(uint8_t id);
void LoadingAnime_32x32_Start(uint16_t x, uint16_t y);
void LoadingAnime_32x32_Stop();

uint8_t isTimeSynced(void);
void SetTimeSynced(uint8_t val);

void SetTTFLoaded(uint8_t val);
uint8_t isTTFLoaded(void);
void SetInitStatus(uint8_t idx, uint8_t val);
uint8_t GetInitStatus(uint8_t idx);
void Shutdown();

// "/dir/name.txt" -> "name"
inline std::string BaseName(const std::string &path) {
    size_t start = path.rfind('/');
    start        = (start == std::string::npos) ? 0 : start + 1;
    size_t end   = path.rfind('.');
    if (end == std::string::npos || end < start) end = path.size();
    return path.substr(start, end - start);
}

// "/dir/name.txt" -> ".txt" ("" if there is no extension)
inline std::string Extension(const std::string &path) {
    size_t dot = path.rfind('.');
    return dot == std::string::npos ? std::string() : path.substr(dot);
}

#endif  //_GLOBAL_SETTING_H_