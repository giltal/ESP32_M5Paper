// M5Paper factory test, ported from M5Stack's Arduino M5Paper_FactoryTest
// to ESP-IDF with M5Unified/M5GFX.
#include "app/epdgui/epdgui.h"
#include "app/systeminit.h"
#include "m5paper_port.h"
#include "nvs_flash.h"

extern "C" void app_main(void) {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    auto cfg          = M5.config();
    cfg.clear_display = false;  // SysInit_Start clears the panel itself
    M5.begin(cfg);
    M5.Power.setExtOutput(true);  // 5V on the Grove ports
    EPD.begin();

    SysInit_Start();

    while (true) {
        EPDGUI_MainLoop();
    }
}
