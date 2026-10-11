#include "board.h"

#include <sys/stat.h>

#include <algorithm>
#include <cstring>

#include <M5Unified.h>

#include "driver/sdspi_host.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "freertos/event_groups.h"
#include "sdmmc_cmd.h"

static const char *TAG = "board";

// ---------------------------------------------------------------- SD card

namespace sdcard {

static sdmmc_card_t *s_card = nullptr;

bool mount() {
    if (s_card) return true;

    // The bus (SPI3: SCLK 14, MOSI 12, MISO 13) already belongs to M5GFX for
    // the IT8951; the card is added as a second device on it.
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot         = SPI3_HOST;
    host.max_freq_khz = 20000;

    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs               = GPIO_NUM_4;
    slot.host_id               = SPI3_HOST;

    esp_vfs_fat_sdmmc_mount_config_t cfg = {};
    cfg.format_if_mount_failed           = false;
    cfg.max_files                        = 5;
    cfg.allocation_unit_size             = 16 * 1024;

    esp_err_t err = esp_vfs_fat_sdspi_mount(kMountPoint, &host, &slot, &cfg,
                                            &s_card);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "SD card not mounted: %s", esp_err_to_name(err));
        s_card = nullptr;
        return false;
    }
    sdmmc_card_print_info(stdout, s_card);
    return true;
}

bool mounted() {
    return s_card != nullptr;
}

uint64_t size_bytes() {
    if (!s_card) return 0;
    return (uint64_t)s_card->csd.capacity * s_card->csd.sector_size;
}

std::string path(const std::string &card_path) {
    if (card_path.empty() || card_path[0] != '/') {
        return std::string(kMountPoint) + "/" + card_path;
    }
    return std::string(kMountPoint) + card_path;
}

bool exists(const std::string &card_path) {
    struct stat st;
    return s_card && stat(path(card_path).c_str(), &st) == 0;
}

}  // namespace sdcard

// ---------------------------------------------------------------- SHT30

static uint8_t Crc8(const uint8_t *data, int len) {
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
        }
    }
    return crc;
}

bool Sht30::update() {
    constexpr uint8_t kAddr = 0x44;
    constexpr uint32_t kFreq = 400000;
    // Single shot, high repeatability, no clock stretching.
    const uint8_t cmd[2] = {0x24, 0x00};
    uint8_t buf[6];

    _ok = false;
    if (!M5.In_I2C.start(kAddr, false, kFreq) || !M5.In_I2C.write(cmd, 2) ||
        !M5.In_I2C.stop()) {
        M5.In_I2C.stop();
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
    if (!M5.In_I2C.start(kAddr, true, kFreq) || !M5.In_I2C.read(buf, 6, true) ||
        !M5.In_I2C.stop()) {
        M5.In_I2C.stop();
        return false;
    }
    if (Crc8(buf, 2) != buf[2] || Crc8(buf + 3, 2) != buf[5]) return false;

    _temperature = -45.0f + 175.0f * ((buf[0] << 8) | buf[1]) / 65535.0f;
    _humidity    = 100.0f * ((buf[3] << 8) | buf[4]) / 65535.0f;
    _ok          = true;
    return true;
}

// ---------------------------------------------------------------- WiFi

namespace wifi {

static EventGroupHandle_t s_events = nullptr;
static constexpr EventBits_t kConnectedBit = BIT0;
static bool s_started = false;

static void OnEvent(void *, esp_event_base_t base, int32_t id, void *) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupClearBits(s_events, kConnectedBit);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(s_events, kConnectedBit);
    }
}

void init() {
    if (s_events) return;
    s_events = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) ESP_ERROR_CHECK(err);
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    err = esp_wifi_init(&cfg);
    if (err != ESP_OK) {
        // Usually internal RAM: the driver needs ~30 KB of DMA-capable memory.
        ESP_LOGE(TAG, "esp_wifi_init failed: %s (internal free %u)",
                 esp_err_to_name(err),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
        return;
    }
    s_started = true;
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               OnEvent, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               OnEvent, nullptr));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

std::vector<ap_t> scan() {
    init();
    std::vector<ap_t> result;
    if (!s_started) return result;
    esp_err_t err = esp_wifi_scan_start(nullptr, true);
    if (err != ESP_OK) return result;

    uint16_t num = 0;
    esp_wifi_scan_get_ap_num(&num);
    std::vector<wifi_ap_record_t> records(num);
    if (num && esp_wifi_scan_get_ap_records(&num, records.data()) == ESP_OK) {
        for (uint16_t i = 0; i < num; i++) {
            result.push_back({(const char *)records[i].ssid, records[i].rssi});
        }
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const ap_t &a, const ap_t &b) { return a.rssi > b.rssi; });
    return result;
}

void begin(const std::string &ssid, const std::string &password) {
    init();
    if (!s_started) return;
    esp_wifi_disconnect();
    wifi_config_t cfg = {};
    strncpy((char *)cfg.sta.ssid, ssid.c_str(), sizeof(cfg.sta.ssid));
    strncpy((char *)cfg.sta.password, password.c_str(),
            sizeof(cfg.sta.password));
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_connect();
}

bool connected() {
    return s_events && (xEventGroupGetBits(s_events) & kConnectedBit);
}

int rssi() {
    wifi_ap_record_t info;
    if (!connected() || esp_wifi_sta_get_ap_info(&info) != ESP_OK) return 0;
    return info.rssi;
}

void disconnect() {
    if (!s_events) return;
    esp_wifi_disconnect();
    xEventGroupClearBits(s_events, kConnectedBit);
}

}  // namespace wifi

// ---------------------------------------------------------------- NTP

bool sync_ntp(const char *server, int tz_hours, struct tm *local,
              uint32_t timeout_ms) {
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(server);
    if (esp_netif_sntp_init(&cfg) != ESP_OK) return false;
    bool ok = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms)) == ESP_OK;
    esp_netif_sntp_deinit();
    if (!ok) return false;

    time_t t = time(nullptr) + (time_t)tz_hours * 3600;
    gmtime_r(&t, local);
    return true;
}

// ---------------------------------------------------------------- Grove

bool grove_check(int sda, int scl) {
    if (!M5.Ex_I2C.begin(I2C_NUM_1, sda, scl)) return false;
    uint8_t id = 0;
    bool found = M5.Ex_I2C.readRegister(0x76, 0xD0, &id, 1, 100000);
    M5.Ex_I2C.release();
    return found && id == 0x58;
}
