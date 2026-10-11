#ifndef _GLOBAL_SETTING_H_
#define _GLOBAL_SETTING_H_

#include "m5paper_port.h"
#include <nvs.h>
#include <string>

#define WALLPAPER_NUM 3

// 是否开启自动关机省电
#define ENABLE_AUTO_POWER_SAVE true

// 1分钟提醒即将关机
static const uint32_t TIME_BEFORE_SHUTDOWN_PROMPT_MS = 60 * 1000;
// 显示10秒钟的关机提示
static const uint32_t SHUTDOWN_PROMPT_DELAY_MS = 10 * 1000;
// 最终关机时间
static const uint32_t TIME_BEFORE_SHUTDOWN_MS =
    TIME_BEFORE_SHUTDOWN_PROMPT_MS + SHUTDOWN_PROMPT_DELAY_MS;

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
void SetWifi(std::string ssid, std::string password);
std::string GetWifiSSID(void);
std::string GetWifiPassword(void);
uint8_t isWiFiConfiged(void);
bool SyncNTPTime(void);
int8_t GetTimeZone(void);
void SetTimeZone(int8_t time_zone);

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