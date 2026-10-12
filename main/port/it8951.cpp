#include "it8951.h"

#include <cstring>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "it8951";

namespace it8951 {

namespace {

constexpr spi_host_device_t kHost = SPI3_HOST;  // as set up by M5GFX
constexpr gpio_num_t kCs          = GPIO_NUM_15;
constexpr gpio_num_t kBusy        = GPIO_NUM_27;  // HRDY: high = ready
constexpr int kWriteHz            = 20 * 1000 * 1000;
constexpr int kReadHz             = 2 * 1000 * 1000;  // reads fail faster

// Commands and registers (M5EPD IT8951_Defines.h)
constexpr uint16_t kCmdSysRun     = 0x0001;
constexpr uint16_t kCmdRegRead    = 0x0010;
constexpr uint16_t kCmdRegWrite   = 0x0011;
constexpr uint16_t kCmdMemReadT   = 0x0012;
constexpr uint16_t kCmdMemReadS   = 0x0013;
constexpr uint16_t kCmdMemEnd     = 0x0015;
constexpr uint16_t kCmdLoadArea   = 0x0021;
constexpr uint16_t kCmdLoadEnd    = 0x0022;
constexpr uint16_t kCmdDisplay    = 0x0037;
constexpr uint16_t kCmdDevInfo    = 0x0302;
constexpr uint16_t kRegLisar      = 0x0208;
constexpr uint16_t kRegLutAfsr    = 0x1224;
constexpr uint16_t kLoadBigEndian = 1;
constexpr uint16_t kLoad4bpp      = 2;

constexpr size_t kChunk = 4096;  // DMA burst size

spi_device_handle_t s_write = nullptr;
spi_device_handle_t s_read  = nullptr;
uint8_t *s_buf              = nullptr;  // kChunk, DMA capable
uint32_t s_image_addr       = 0x001236E0;  // M5Paper default

bool WaitReady(uint32_t timeout_ms = 3000) {
    if (gpio_get_level(kBusy)) return true;
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    int spins        = 0;
    while (!gpio_get_level(kBusy)) {
        if (esp_timer_get_time() > deadline) {
            ESP_LOGE(TAG, "HRDY timeout");
            return false;
        }
        // Short waits are common between packets; only sleep on long ones.
        if (++spins > 200) vTaskDelay(1);
    }
    return true;
}

void Send(spi_device_handle_t dev, const void *data, size_t bytes,
          void *rx = nullptr) {
    spi_transaction_t t = {};
    t.length            = bytes * 8;
    t.tx_buffer         = data;
    t.rx_buffer         = rx;
    spi_device_polling_transmit(dev, &t);
}

// One packet: preamble + payload words, CS asserted around it.
void Packet(uint16_t preamble, const uint16_t *words, size_t count) {
    uint8_t b[2 + 2 * 8];
    b[0]     = preamble >> 8;
    b[1]     = preamble & 0xFF;
    size_t n = 2;
    for (size_t i = 0; i < count; i++) {
        b[n++] = words[i] >> 8;
        b[n++] = words[i] & 0xFF;
    }
    WaitReady();
    gpio_set_level(kCs, 0);
    Send(s_write, b, n);
    gpio_set_level(kCs, 1);
}

void Command(uint16_t cmd) {
    Packet(0x6000, &cmd, 1);
}

void Word(uint16_t w) {
    Packet(0x0000, &w, 1);
}

// Command followed by its arguments as one data packet, HRDY checked before
// every word (M5GFX framing; one packet per argument corrupts image loads).
void Args(uint16_t cmd, const uint16_t *args, size_t count) {
    Command(cmd);
    uint8_t b[2];
    WaitReady();
    gpio_set_level(kCs, 0);
    b[0] = 0;
    b[1] = 0;
    Send(s_write, b, 2);
    for (size_t i = 0; i < count; i++) {
        WaitReady();
        b[0] = args[i] >> 8;
        b[1] = args[i] & 0xFF;
        Send(s_write, b, 2);
    }
    gpio_set_level(kCs, 1);
}

// Read `count` words after a 0x1000 preamble and one dummy word.
void ReadWords(uint16_t *out, size_t count) {
    WaitReady();
    gpio_set_level(kCs, 0);
    size_t done = 0;
    bool first  = true;
    while (done < count || first) {
        size_t lead   = first ? 4 : 0;  // preamble + dummy word
        size_t words  = count - done;
        size_t max_w  = (kChunk - lead) / 2;
        if (words > max_w) words = max_w;
        size_t bytes  = lead + words * 2;
        memset(s_buf, 0, bytes);
        if (first) s_buf[0] = 0x10;  // 0x1000 read preamble
        uint8_t *rx = (uint8_t *)heap_caps_malloc(bytes, MALLOC_CAP_DMA);
        if (!rx) break;
        Send(s_read, s_buf, bytes, rx);
        for (size_t i = 0; i < words; i++) {
            out[done + i] = (rx[lead + 2 * i] << 8) | rx[lead + 2 * i + 1];
        }
        heap_caps_free(rx);
        done += words;
        first = false;
    }
    gpio_set_level(kCs, 1);
}

uint16_t ReadReg(uint16_t reg) {
    Command(kCmdRegRead);
    Word(reg);
    uint16_t v = 0xFFFF;
    ReadWords(&v, 1);
    return v;
}

void WriteReg(uint16_t reg, uint16_t value) {
    Command(kCmdRegWrite);
    Word(reg);
    Word(value);
}

void SetTarget(uint32_t addr) {
    WriteReg(kRegLisar + 2, addr >> 16);
    WriteReg(kRegLisar, addr & 0xFFFF);
}

}  // namespace

bool begin() {
    gpio_set_direction(kBusy, GPIO_MODE_INPUT);
    gpio_set_pull_mode(kBusy, GPIO_PULLUP_ONLY);
    gpio_set_direction(kCs, GPIO_MODE_OUTPUT);
    gpio_set_level(kCs, 1);

    s_buf = (uint8_t *)heap_caps_malloc(kChunk, MALLOC_CAP_DMA);
    if (!s_buf) return false;

    spi_device_interface_config_t dev = {};
    dev.mode                          = 0;
    dev.spics_io_num                  = -1;  // CS is GPIO15, driven by hand
    dev.queue_size                    = 1;
    dev.clock_speed_hz                = kWriteHz;
    if (spi_bus_add_device(kHost, &dev, &s_write) != ESP_OK) return false;
    dev.clock_speed_hz = kReadHz;
    if (spi_bus_add_device(kHost, &dev, &s_read) != ESP_OK) return false;

    // Device info: panel size, image buffer address, firmware strings.
    uint16_t info[20] = {};
    Command(kCmdDevInfo);
    vTaskDelay(1);
    ReadWords(info, 20);
    uint32_t addr = ((uint32_t)info[3] << 16) | info[2];
    if (addr != 0 && addr != 0xFFFFFFFF) s_image_addr = addr;
    ESP_LOGI(TAG, "IT8951 %ux%u, image buffer 0x%08lx", info[0], info[1],
             (unsigned long)s_image_addr);
    Command(kCmdSysRun);
    return info[0] == kPanelW && info[1] == kPanelH;
}

uint32_t image_address() {
    return s_image_addr;
}

void load_area(int x, int y, int w, int h, const uint8_t *src, int stride,
               bool invert) {
    SetTarget(s_image_addr);
    const uint16_t args[5] = {
        (uint16_t)(kLoadBigEndian << 8 | kLoad4bpp << 4 | kRotation),
        (uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h};
    Args(kCmdLoadArea, args, 5);

    // One data packet per row, each after HRDY (as M5GFX does). Streaming the
    // whole area as a single packet loses data while the controller is busy
    // (verified by reading the image memory back).
    const int row_b = w / 2;
    for (int row = 0; row < h; row++) {
        const uint8_t *s = src + (size_t)row * stride;
        s_buf[0]         = 0x00;  // 0x0000 data preamble
        s_buf[1]         = 0x00;
        if (invert) {
            for (int i = 0; i < row_b; i++) s_buf[2 + i] = ~s[i];
        } else {
            memcpy(s_buf + 2, s, row_b);
        }
        WaitReady();
        gpio_set_level(kCs, 0);
        Send(s_write, s_buf, 2 + row_b);
        gpio_set_level(kCs, 1);
    }
    Command(kCmdLoadEnd);
}

void display_area(int x, int y, int w, int h, int mode) {
    // Logical (portrait) -> native panel coordinates for rotation 1.
    int left = y, top = kPanelH - w - x, width = h, height = w;
    // The IT8951 can hang when an update spans a single 4-pixel group
    // (M5GFX note); widen such areas.
    int r = left + width - 1;
    if ((left & ~3) == (r & ~3)) {
        if (left >= 4) {
            left = (left & ~3) - 1;
        } else {
            r = (r + 4) & ~3;
        }
        width = r - left + 1;
    }
    const uint16_t args[7] = {(uint16_t)left,
                              (uint16_t)top,
                              (uint16_t)width,
                              (uint16_t)height,
                              (uint16_t)mode,
                              (uint16_t)(s_image_addr & 0xFFFF),
                              (uint16_t)(s_image_addr >> 16)};
    Args(kCmdDisplay, args, 7);
}

bool busy() {
    return ReadReg(kRegLutAfsr) != 0;
}

bool wait_idle(uint32_t timeout_ms) {
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (busy()) {
        if (esp_timer_get_time() > deadline) return false;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return true;
}

bool read_memory(uint32_t address, uint16_t *words, size_t count) {
    const uint16_t args[4] = {(uint16_t)(address & 0xFFFF),
                              (uint16_t)(address >> 16), (uint16_t)count, 0};
    Args(kCmdMemReadT, args, 4);
    Command(kCmdMemReadS);
    ReadWords(words, count);
    Command(kCmdMemEnd);
    return true;
}

}  // namespace it8951
