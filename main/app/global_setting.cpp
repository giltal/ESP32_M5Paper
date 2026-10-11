#include "global_setting.h"
#include "./resources/ImageResource.h"
#include "epdgui/epdgui.h"
#include "frame/frame_todo.h"
#include "freertos/semphr.h"
#define DEFAULT_WALLPAPER 2
SemaphoreHandle_t _xSemaphore_LoadingAnime = NULL;
static uint8_t _loading_anime_eixt_flag    = false;
esp_err_t __espret__;
#define NVS_CHECK(x)            \
    __espret__ = x;             \
    if (__espret__ != ESP_OK) { \
        nvs_close(nvs_arg);     \
        log_e("Check Err");     \
        return __espret__;      \
    }

const uint8_t *wallpapers[] = {ImageResource_wallpaper_m5stack_540x960, ImageResource_wallpaper_engine_540x960,
                               ImageResource_wallpaper_penrose_triangle_540x960};

const uint8_t *kIMGLoading[16] = {
    ImageResource_item_loading_01_32x32, ImageResource_item_loading_02_32x32, ImageResource_item_loading_03_32x32,
    ImageResource_item_loading_04_32x32, ImageResource_item_loading_05_32x32, ImageResource_item_loading_06_32x32,
    ImageResource_item_loading_07_32x32, ImageResource_item_loading_08_32x32, ImageResource_item_loading_09_32x32,
    ImageResource_item_loading_10_32x32, ImageResource_item_loading_11_32x32, ImageResource_item_loading_12_32x32,
    ImageResource_item_loading_13_32x32, ImageResource_item_loading_14_32x32, ImageResource_item_loading_15_32x32,
    ImageResource_item_loading_16_32x32};
const char *wallpapers_name_en[] = {"M5Paper", "Engine", "Penrose Triangle"};
const char *wallpapers_name_zh[] = {"M5Paper", "引擎", "彭罗斯三角"};
const char *wallpapers_name_ja[] = {"M5Paper", "エンジン", "ペンローズの三角形"};
uint16_t global_wallpaper        = DEFAULT_WALLPAPER;
uint8_t global_language          = LANGUAGE_EN;
std::string global_wifi_ssid;
std::string global_wifi_password;
uint8_t global_wifi_configed    = false;
uint16_t global_reader_textsize = 32;
uint8_t global_time_synced      = false;
uint8_t global_ttf_file_loaded  = false;
uint8_t global_init_status      = 0xFF;

void SetInitStatus(uint8_t idx, uint8_t val) {
    global_init_status &= ~(1 << idx);
    global_init_status |= (val << idx);
}

uint8_t GetInitStatus(uint8_t idx) {
    return (global_init_status & (1 << idx)) ? true : false;
}

void SetTTFLoaded(uint8_t val) {
    global_ttf_file_loaded = val;
}

uint8_t isTTFLoaded() {
    return global_ttf_file_loaded;
}

uint8_t isTimeSynced(void) {
    return global_time_synced;
}

void SetTimeSynced(uint8_t val) {
    global_time_synced = val;
    SaveSetting();
}

void SetLanguage(uint8_t language) {
    if (language >= LANGUAGE_EN && language <= LANGUAGE_ZH) {
        global_language = language;
    }
    SaveSetting();
}

uint8_t GetLanguage(void) {
    return global_language;
}

void SetWallpaper(uint16_t wallpaper_id) {
    global_wallpaper = wallpaper_id;
    SaveSetting();
}

uint16_t GetWallpaperID(void) {
    return global_wallpaper;
}

const uint8_t *GetWallpaper(void) {
    return wallpapers[global_wallpaper];
}

const char *GetWallpaperName(uint16_t wallpaper_id) {
    switch (global_language) {
        case LANGUAGE_ZH:
            return wallpapers_name_zh[wallpaper_id];
        case LANGUAGE_JA:
            return wallpapers_name_ja[wallpaper_id];
        default:
            return wallpapers_name_en[wallpaper_id];
    }
}

esp_err_t LoadSetting(void) {
    nvs_handle nvs_arg;
    NVS_CHECK(nvs_open("Setting", NVS_READONLY, &nvs_arg));
    NVS_CHECK(nvs_get_u16(nvs_arg, "Wallpaper", &global_wallpaper));
    NVS_CHECK(nvs_get_u8(nvs_arg, "Language", &global_language));
    NVS_CHECK(nvs_get_u8(nvs_arg, "Timesync", &global_time_synced));

    if (global_wallpaper >= WALLPAPER_NUM) {
        global_wallpaper = DEFAULT_WALLPAPER;
    }
    nvs_close(nvs_arg);
    return ESP_OK;
}

esp_err_t SaveSetting(void) {
    nvs_handle nvs_arg;
    NVS_CHECK(nvs_open("Setting", NVS_READWRITE, &nvs_arg));
    NVS_CHECK(nvs_set_u16(nvs_arg, "Wallpaper", global_wallpaper));
    NVS_CHECK(nvs_set_u8(nvs_arg, "Language", global_language));
    NVS_CHECK(nvs_set_u8(nvs_arg, "Timesync", global_time_synced));
    NVS_CHECK(nvs_commit(nvs_arg));
    nvs_close(nvs_arg);
    return ESP_OK;
}

// WiFi credentials live only on the SD card, in /wifi.txt:
//   ssid=<name>
//   password=<password>
static const char *kWifiFile = "/wifi.txt";

void SetWifi(std::string ssid, std::string password) {
    global_wifi_ssid     = ssid;
    global_wifi_password = password;
    global_wifi_configed = !ssid.empty();
    if (!sdcard::write_text(kWifiFile,
                            "ssid=" + ssid + "\npassword=" + password + "\n")) {
        log_e("WiFi credentials not saved: no SD card");
    }
}

// Older firmware kept the credentials in NVS. Move them to the card once,
// then erase them from flash.
static void MigrateWifiFromNvs(void) {
    nvs_handle_t nvs;
    if (nvs_open("Setting", NVS_READWRITE, &nvs) != ESP_OK) return;
    char ssid[128] = {}, pswd[128] = {};
    size_t len1 = sizeof(ssid), len2 = sizeof(pswd);
    bool found = nvs_get_str(nvs, "ssid", ssid, &len1) == ESP_OK;
    nvs_get_str(nvs, "pswd", pswd, &len2);
    bool done = !found;
    if (found && sdcard::mounted()) {
        done = sdcard::exists(kWifiFile) ||
               (ssid[0] == '\0') ||
               sdcard::write_text(kWifiFile, std::string("ssid=") + ssid +
                                                 "\npassword=" + pswd + "\n");
    }
    if (found && done) {
        nvs_erase_key(nvs, "ssid");
        nvs_erase_key(nvs, "pswd");
        nvs_commit(nvs);
        log_i("WiFi credentials moved from NVS to the SD card");
    }
    nvs_close(nvs);
}

void LoadWifiConfig(void) {
    MigrateWifiFromNvs();
    global_wifi_ssid.clear();
    global_wifi_password.clear();
    global_wifi_configed = false;

    std::string text;
    if (!sdcard::read_text(kWifiFile, &text)) return;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t end = text.find('\n', pos);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(pos, end - pos);
        pos              = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        if (key == "ssid") {
            global_wifi_ssid = line.substr(eq + 1);
        } else if (key == "password") {
            global_wifi_password = line.substr(eq + 1);
        }
    }
    global_wifi_configed = !global_wifi_ssid.empty();
}

uint8_t isWiFiConfiged(void) {
    return global_wifi_configed;
}

std::string GetWifiSSID(void) {
    return global_wifi_ssid;
}

std::string GetWifiPassword(void) {
    return global_wifi_password;
}

bool SyncNTPTime(void) {
    const char *ntpServer    = "time.cloudflare.com";
    const char *ntpServer_zh = "ntp.aliyun.com";
    const char *server = global_language == LANGUAGE_ZH ? ntpServer_zh : ntpServer;
    struct tm timeInfo;

    if (!sync_ntp(server, 10000)) {
        log_d("Time Sync failed, server is %s", server);
        return 0;
    }
    time_t now = time(NULL);
    gmtime_r(&now, &timeInfo);
    M5.Rtc.setDateTime(&timeInfo);  // the RTC keeps UTC

    SetTimeSynced(1);

    return 1;
}

// Asia/Jerusalem: IDT from the Friday before the last Sunday of March, 02:00,
// to the last Sunday of October, 02:00.
static const char *kTimeZone = "IST-2IDT,M3.4.4/26,M10.5.0";

void InitTimeZone(void) {
    setenv("TZ", kTimeZone, 1);
    tzset();
}

const char *GetTimeZoneName(bool is_dst) {
    return is_dst ? "UTC+3 IDT" : "UTC+2 IST";
}

// Days since 1970-01-01 for a proleptic Gregorian date (H. Hinnant).
static int64_t DaysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

bool GetRtcLocalTime(struct tm *local) {
    m5::rtc_date_t date;
    m5::rtc_time_t time;
    if (!M5.Rtc.getDateTime(&date, &time)) return false;
    time_t utc = DaysFromCivil(date.year, date.month, date.date) * 86400 +
                 time.hours * 3600 + time.minutes * 60 + time.seconds;
    localtime_r(&utc, local);
    return true;
}

uint16_t GetTextSize() {
    return global_reader_textsize;
}

void SetTextSize(uint16_t size) {
    global_reader_textsize = size;
}

const uint8_t *GetLoadingIMG_32x32(uint8_t id) {
    return kIMGLoading[id];
}

void __LoadingAnime_32x32(void *pargs) {
    uint16_t *args = (uint16_t *)pargs;
    uint16_t x     = args[0];
    uint16_t y     = args[1];
    free(pargs);
    EpdCanvas loading;
    loading.createCanvas(32, 32);
    loading.fillCanvas(0);
    loading.pushCanvas(x, y, UPDATE_MODE_GL16);
    int anime_cnt = 0;
    uint32_t time = 0;
    while (1) {
        if (now_ms() - time > 200) {
            time = now_ms();
            loading.pushImage(0, 0, 32, 32, GetLoadingIMG_32x32(anime_cnt));
            loading.pushCanvas(x, y, UPDATE_MODE_DU4);
            anime_cnt++;
            if (anime_cnt == 16) {
                anime_cnt = 0;
            }
        }

        xSemaphoreTake(_xSemaphore_LoadingAnime, portMAX_DELAY);
        if (_loading_anime_eixt_flag == true) {
            xSemaphoreGive(_xSemaphore_LoadingAnime);
            break;
        }
        xSemaphoreGive(_xSemaphore_LoadingAnime);
    }
    vTaskDelete(NULL);
}

void LoadingAnime_32x32_Start(uint16_t x, uint16_t y) {
    if (_xSemaphore_LoadingAnime == NULL) {
        _xSemaphore_LoadingAnime = xSemaphoreCreateMutex();
    }
    _loading_anime_eixt_flag = false;
    uint16_t *pos            = (uint16_t *)calloc(2, sizeof(uint16_t));
    pos[0]                   = x;
    pos[1]                   = y;
    xTaskCreatePinnedToCore(__LoadingAnime_32x32, "__LoadingAnime_32x32", 16 * 1024, pos, 1, NULL, 0);
}

void LoadingAnime_32x32_Stop() {
    xSemaphoreTake(_xSemaphore_LoadingAnime, portMAX_DELAY);
    _loading_anime_eixt_flag = true;
    xSemaphoreGive(_xSemaphore_LoadingAnime);
    vTaskDelay(pdMS_TO_TICKS(200));
}

void Shutdown() {
    log_i("Shutting down.");
    // E-paper keeps its image without power: leave the to-do list on screen,
    // or the logo when there is no SD card to read the list from.
    Frame_Todo *todo = (Frame_Todo *)EPDGUI_GetFrame("Frame_Todo");
    if (todo == NULL) {
        todo = new Frame_Todo();
        EPDGUI_AddFrame("Frame_Todo", todo);
    }
    if (!todo->DrawPowerOffScreen()) {
        EPD.Clear();
        EPD.WritePartGram4bpp(92, 182, 356, 300, ImageResource_logo_356x300);
    }
    SaveSetting();
    EPD.UpdateFull(UPDATE_MODE_GC16);

    // The panel must finish the refresh before power is cut, or the image is
    // left half drawn. The IT8951 busy register reads idle ~20 ms after a
    // full-screen GC16 is issued (measured), long before the refresh ends, so
    // the fixed delay is what guarantees completion. 5 s leaves a wide margin.
    uint32_t t0 = now_ms();
    EPD.CheckAFSR();
    uint32_t t1 = now_ms();
    vTaskDelay(pdMS_TO_TICKS(5000));
    EPD.CheckAFSR();
    log_i("Final refresh: idle after %u ms, then +%u ms", (unsigned)(t1 - t0),
          (unsigned)(now_ms() - t1));
    M5.Display.sleep();
    vTaskDelay(pdMS_TO_TICKS(100));
    M5.Power.setExtOutput(false);
    // Releases the main power hold (G2); on USB power this ends in deep sleep.
    M5.Power.powerOff();
    while (1);
}