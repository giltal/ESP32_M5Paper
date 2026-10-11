#include "board.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>

#include <algorithm>
#include <cstring>

#include <M5Unified.h>

#include "driver/sdspi_host.h"
#include "epd_display.h"
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

// Some cards answer CMD59 (CRC_ON_OFF) with "illegal command", which makes
// ESP-IDF abort the mount. CRC is optional in SPI mode (the Arduino SD library
// never enables it), so treat that answer as "continue without CRC".
// Hooked in with -Wl,--wrap in main/CMakeLists.txt.
extern "C" esp_err_t __real_sdmmc_send_cmd_crc_on_off(sdmmc_card_t *card,
                                                      bool crc_enable);
extern "C" esp_err_t __wrap_sdmmc_send_cmd_crc_on_off(sdmmc_card_t *card,
                                                      bool crc_enable) {
    esp_err_t err = __real_sdmmc_send_cmd_crc_on_off(card, crc_enable);
    if (err == ESP_ERR_NOT_SUPPORTED) {
        ESP_LOGW(TAG, "SD card does not support CRC in SPI mode; continuing without it");
        return ESP_OK;
    }
    return err;
}

namespace sdcard {

static sdmmc_card_t *s_card = nullptr;

// The card shares SPI3 with the IT8951, which M5GFX drives from whichever task
// is drawing (e.g. the loading animation). Run every SD transaction under the
// display lock so card commands never interleave with panel traffic.
static esp_err_t LockedTransaction(int slot, sdmmc_command_t *cmd) {
    EPD.lock();
    esp_err_t err = sdspi_host_do_transaction(slot, cmd);
    EPD.unlock();
    return err;
}

// The card's CMD pin is the panel's MOSI and its CLK the panel's SCK. A card
// powers up in native SD mode, where it ignores CS and takes any traffic on
// those lines as commands, so it must be switched to SPI mode (CMD0 with CS
// low) before the panel is used again - otherwise stray panel data can leave it
// in a state only a power cycle clears, and on USB its 3.3 V never drops.
// This also recovers a card left mid-transfer by an ESP32 reset: with CS low,
// clock out any pending data block and send CMD12 (stop transmission) first.
static void PrepareCard() {
    spi_device_interface_config_t dev = {};
    dev.clock_speed_hz                = 400000;
    dev.spics_io_num                  = -1;
    dev.queue_size                    = 1;
    spi_device_handle_t spi;
    if (spi_bus_add_device(SPI3_HOST, &dev, &spi) != ESP_OK) return;

    uint8_t *buf = (uint8_t *)heap_caps_malloc(64, MALLOC_CAP_DMA);
    if (buf) {
        auto clock_out = [&](int bytes) {
            memset(buf, 0xFF, 64);
            for (; bytes > 0; bytes -= 64) {
                spi_transaction_t t = {};
                t.length    = (bytes < 64 ? bytes : 64) * 8;
                t.tx_buffer = buf;
                spi_device_polling_transmit(spi, &t);
            }
        };
        static const uint8_t kCmd12[6] = {0x4C, 0, 0, 0, 0, 0x61};
        static const uint8_t kCmd0[6]  = {0x40, 0, 0, 0, 0, 0x95};
        auto send_cmd = [&](const uint8_t *cmd, uint8_t *resp) {
            memset(buf, 0xFF, 64);
            memcpy(buf, cmd, 6);
            spi_transaction_t t = {};
            t.length    = 16 * 8;
            t.tx_buffer = buf;
            t.rx_buffer = buf + 32;
            spi_device_polling_transmit(spi, &t);
            if (resp) memcpy(resp, buf + 32 + 6, 10);
        };
        EPD.lock();
        gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
        gpio_set_level(GPIO_NUM_4, 0);
        clock_out(640);  // a 512-byte block, CRC and response slack
        send_cmd(kCmd12, nullptr);
        clock_out(64);  // let CMD12 finish (R1b busy)
        gpio_set_level(GPIO_NUM_4, 1);
        clock_out(16);

        // A card that powered up while the panel was busy is still in native
        // SD mode, where it ignores CS and takes panel traffic on MOSI/SCK as
        // commands. A native CMD0 (CS high) resets it to idle; the CMD0 below
        // with CS low then switches it to SPI mode.
        send_cmd(kCmd0, nullptr);
        clock_out(16);
        gpio_set_level(GPIO_NUM_4, 0);
        send_cmd(kCmd0, nullptr);
        gpio_set_level(GPIO_NUM_4, 1);
        clock_out(16);
        EPD.unlock();
        heap_caps_free(buf);
    }
    spi_bus_remove_device(spi);
}

// Slot card-detect switch (schematic: CARD_CD on GPIO34, 10K pull-up):
// low while a card is inserted.
bool present() {
    static bool configured = false;
    if (!configured) {
        gpio_reset_pin(GPIO_NUM_34);
        gpio_set_direction(GPIO_NUM_34, GPIO_MODE_INPUT);
        configured = true;
    }
    return gpio_get_level(GPIO_NUM_34) == 0;
}

void unmount() {
    if (!s_card) return;
    esp_vfs_fat_sdcard_unmount(kMountPoint, s_card);
    s_card = nullptr;
}

bool poll() {
    static bool stable = present();
    static int count   = 0;
    bool now           = present();
    if (now == stable) {
        count = 0;
        return false;
    }
    if (++count < 5) return false;  // ~50 ms debounce at the GUI loop rate
    count  = 0;
    stable = now;
    if (now) {
        ESP_LOGI(TAG, "SD card inserted");
        // Contacts bounce and the card's supply ramps for a while after the
        // detect switch closes. Keep the panel off the bus (the card is still
        // in native mode) until it has settled and been switched to SPI mode.
        for (int attempt = 0; attempt < 3; attempt++) {
            EPD.lock();
            vTaskDelay(pdMS_TO_TICKS(attempt == 0 ? 500 : 300));
            PrepareCard();
            EPD.unlock();
            if (mount()) break;
        }
    } else {
        ESP_LOGI(TAG, "SD card removed");
        unmount();
    }
    return true;
}

bool mount() {
    if (s_card) return true;
    if (!present()) {
        ESP_LOGI(TAG, "No SD card");
        return false;
    }
    PrepareCard();
    // The bus (SPI3: SCLK 14, MOSI 12, MISO 13) already belongs to M5GFX for
    // the IT8951; the card is added as a second device on it.
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot         = SPI3_HOST;
    // The card shares SCK/MOSI/MISO with the IT8951 through the GPIO matrix
    // (not the SPI3 IO_MUX pins), and SCK has a 200R series resistor and a
    // 10K pull-up. At 20 MHz reads come back corrupted (bad R1 / data CRC
    // right after the switch from the 400 kHz probing clock) and can leave
    // the card in a state that only a power cycle clears.
    host.max_freq_khz = 10000;
    host.do_transaction = LockedTransaction;
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

bool read_text(const std::string &card_path, std::string *text) {
    if (!s_card) return false;
    FILE *f = fopen(path(card_path).c_str(), "rb");
    if (!f) return false;
    text->clear();
    char buf[256];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text->append(buf, n);
    fclose(f);
    return true;
}

bool write_text(const std::string &card_path, const std::string &text) {
    if (!s_card) return false;
    std::string target = path(card_path);
    std::string tmp    = target + ".tmp";
    FILE *f            = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    ok      = (fclose(f) == 0) && ok;
    if (ok) {
        unlink(target.c_str());  // FAT rename() does not replace
        ok = rename(tmp.c_str(), target.c_str()) == 0;
    }
    if (!ok) {
        unlink(tmp.c_str());
        ESP_LOGE(TAG, "Cannot write %s", card_path.c_str());
    }
    return ok;
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

bool sync_ntp(const char *server, uint32_t timeout_ms) {
    esp_sntp_config_t cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG(server);
    if (esp_netif_sntp_init(&cfg) != ESP_OK) return false;
    bool ok = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms)) == ESP_OK;
    esp_netif_sntp_deinit();
    return ok;
}

// ---------------------------------------------------------------- Grove

bool grove_check(int sda, int scl) {
    if (!M5.Ex_I2C.begin(I2C_NUM_1, sda, scl)) return false;
    uint8_t id = 0;
    bool found = M5.Ex_I2C.readRegister(0x76, 0xD0, &id, 1, 100000);
    M5.Ex_I2C.release();
    return found && id == 0x58;
}
