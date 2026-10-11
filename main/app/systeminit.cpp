#include "systeminit.h"
#include "epdgui/epdgui.h"
#include "frame/frame.h"
#include "global_setting.h"
#include "resources/binaryttf.h"
#include "esp_random.h"
#include "freertos/queue.h"

EpdCanvas _initcanvas;

QueueHandle_t xQueue_Info = NULL;

void WaitForUser(void) {
    SysInit_UpdateInfo("$ERR");
    while (1) {
        M5.update();
        if (M5.BtnB.wasReleased()) {
            SysInit_UpdateInfo("$RESUME");
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void Screen_Test(void) {
    log_i("Start Screen Life Test...");
    EpdCanvas::loadFont(binaryttf, sizeof(binaryttf));
    EpdCanvas::createRender(48);
    _initcanvas.createCanvas(540, 960);
    _initcanvas.setTextSize(48);
    vTaskDelay(pdMS_TO_TICKS(1000));
    float min = 0;
    char buf[48];
    while (1) {
        for (uint8_t pos = 0; pos < 2; pos++) {
            for (uint8_t index = 0; index < 16; index++) {
                _initcanvas.fillRect(0, index * 60, 540, 60, index);
            }
            int possition = esp_random() % 960;
            snprintf(buf, sizeof(buf), "Test Time: %.2fmin", min);
            _initcanvas.drawString(buf, 20, possition);
            _initcanvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
            vTaskDelay(pdMS_TO_TICKS(10000));
            for (uint8_t index = 0; index < 16; index++) {
                _initcanvas.fillRect(0, index * 60, 540, 60, (15 - index));
            }
            _initcanvas.drawString(buf, 20, possition);
            _initcanvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
            vTaskDelay(pdMS_TO_TICKS(10000));
        }
        min += 1;
    }
}

void SysInit_Start(void) {
    log_i("M5Paper initializing...");
    xQueue_Info = xQueueCreate(20, sizeof(char *));

    EPD.Clear(true);

    // Holding the right side of the rocker switch (G39) at boot runs the
    // panel life test.
    if (!gpio_get_level(GPIO_NUM_39)) {
        vTaskDelay(pdMS_TO_TICKS(10));
        if (!gpio_get_level(GPIO_NUM_39)) {
            Screen_Test();
        }
    }

    xTaskCreatePinnedToCore(SysInit_Loading, "SysInit_Loading", 4096, NULL, 5,
                            NULL, 1);
    vTaskDelay(50);
    vTaskPrioritySet(NULL, 10);
    // SysInit_UpdateInfo("Initializing SD card...");
    bool is_factory_test;
    if (!sdcard::mount()) {
        is_factory_test = true;
        SetInitStatus(0, 0);
    } else {
        is_factory_test = sdcard::exists("/__factory_test_flag__");
    }

    SysInit_UpdateInfo("Initializing Touch pad...");
    if (!M5.Touch.isEnabled()) {
        SetInitStatus(1, 0);
        log_e("Touch pad initialization failed.");
        SysInit_UpdateInfo("[ERROR] Failed to initialize Touch pad.");
        WaitForUser();
    }
    taskYIELD();

    LoadSetting();
    LoadWifiConfig();
    InitTimeZone();
    if ((!is_factory_test) && sdcard::exists("/font.ttf") &&
        EpdCanvas::loadFont(sdcard::path("/font.ttf").c_str()) == ESP_OK) {
        SetTTFLoaded(true);
    } else {
        EpdCanvas::loadFont(binaryttf, sizeof(binaryttf));
        SetTTFLoaded(false);
        SetLanguage(LANGUAGE_EN);
        is_factory_test = true;
    }
    taskYIELD();

    if (is_factory_test) {
        SysInit_UpdateInfo("$OK");
    } else {
        SysInit_UpdateInfo("Initializing system...");
    }
    taskYIELD();

    EpdCanvas::createRender(26, 128);

    Frame_Main *frame_main = new Frame_Main();
    EPDGUI_PushFrame(frame_main);
    Frame_FactoryTest *frame_factorytest = new Frame_FactoryTest();
    EPDGUI_AddFrame("Frame_FactoryTest", frame_factorytest);
    if (!is_factory_test) {
        Frame_Setting *frame_setting = new Frame_Setting();
        EPDGUI_AddFrame("Frame_Setting", frame_setting);
        Frame_Setting_Wallpaper *frame_wallpaper =
            new Frame_Setting_Wallpaper();
        EPDGUI_AddFrame("Frame_Setting_Wallpaper", frame_wallpaper);
        Frame_Setting_Language *frame_language = new Frame_Setting_Language();
        EPDGUI_AddFrame("Frame_Setting_Language", frame_language);
        Frame_Keyboard *frame_keyboard = new Frame_Keyboard(0);
        EPDGUI_AddFrame("Frame_Keyboard", frame_keyboard);
        Frame_WifiScan *frame_wifiscan = new Frame_WifiScan();
        EPDGUI_AddFrame("Frame_WifiScan", frame_wifiscan);
        Frame_WifiPassword *frame_wifipassword = new Frame_WifiPassword();
        EPDGUI_AddFrame("Frame_WifiPassword", frame_wifipassword);
        Frame_Todo *frame_todo = new Frame_Todo();
        EPDGUI_AddFrame("Frame_Todo", frame_todo);
        Frame_Compare *frame_compare = new Frame_Compare();
        EPDGUI_AddFrame("Frame_Compare", frame_compare);
        Frame_Home *frame_home = new Frame_Home();
        EPDGUI_AddFrame("Frame_Home", frame_home);

        if (isWiFiConfiged()) {
            SysInit_UpdateInfo("Connect to " + GetWifiSSID() + "...");
            wifi::begin(GetWifiSSID(), GetWifiPassword());
            uint32_t t = now_ms();
            while (1) {
                if (now_ms() - t > 8000) {
                    break;
                }

                if (wifi::connected()) {
                    frame_wifiscan->SetConnected(GetWifiSSID(), wifi::rssi());
                    break;
                }

                vTaskDelay(500);
            }
        }
    }

    log_d("done");

    while (uxQueueMessagesWaiting(xQueue_Info)) {
        vTaskDelay(100);
    }

    if (!is_factory_test) {
        SysInit_UpdateInfo("$OK");
    }

    log_i("OK");

    vTaskDelay(pdMS_TO_TICKS(500));
}

// The status line under the logo uses an M5GFX font: the TTF is not loaded
// yet when the first messages arrive.
static void DrawInfo(EpdCanvas &info, const std::string &str) {
    M5Canvas text;
    text.setColorDepth(1);
    if (text.createSprite(info.width(), info.height()) == nullptr) return;
    text.fillSprite(0);
    text.setFont(&fonts::FreeSans12pt7b);
    text.setTextDatum(middle_center);
    text.setTextColor(1);
    text.drawString(str.c_str(), 270, 20);
    for (int y = 0; y < info.height(); y++) {
        for (int x = 0; x < info.width(); x++) {
            if (text.readPixelValue(x, y)) info.drawPixel(x, y, 15);
        }
    }
}

void SysInit_Loading(void *pvParameters) {
    const uint16_t kPosy = 548;
    const uint8_t *kLD[] = {
        ImageResource_loading_01_96x96, ImageResource_loading_02_96x96,
        ImageResource_loading_03_96x96, ImageResource_loading_04_96x96,
        ImageResource_loading_05_96x96, ImageResource_loading_06_96x96,
        ImageResource_loading_07_96x96, ImageResource_loading_08_96x96,
        ImageResource_loading_09_96x96, ImageResource_loading_10_96x96,
        ImageResource_loading_11_96x96, ImageResource_loading_12_96x96,
        ImageResource_loading_13_96x96, ImageResource_loading_14_96x96,
        ImageResource_loading_15_96x96, ImageResource_loading_16_96x96};

    EpdCanvas LoadingIMG;
    EpdCanvas Info;
    LoadingIMG.createCanvas(96, 96);
    Info.createCanvas(540, 50);

    EPD.WritePartGram4bpp(92, 182, 356, 300, ImageResource_logo_356x300);
    EPD.UpdateFull(UPDATE_MODE_GC16);

    int i = 0;
    char *p;
    uint32_t time = 0;
    while (1) {
        vTaskPrioritySet(NULL, 15);
        if (now_ms() - time > 250) {
            time = now_ms();
            LoadingIMG.pushImage(0, 0, 96, 96, kLD[i]);
            LoadingIMG.pushCanvas(220, kPosy + 80, UPDATE_MODE_DU4);
            i++;
            if (i == 16) {
                i = 0;
            }
        }

        if (xQueueReceive(xQueue_Info, &p, 0)) {
            std::string str(p);
            free(p);
            if (str.find("$OK") != std::string::npos) {
                LoadingIMG.pushImage(0, 0, 96, 96,
                                     ImageResource_loading_success_96x96);
                LoadingIMG.pushCanvas(220, kPosy + 80, UPDATE_MODE_GL16);
                break;
            } else if (str.find("$ERR") != std::string::npos) {
                LoadingIMG.pushImage(0, 0, 96, 96,
                                     ImageResource_loading_error_96x96);
                LoadingIMG.pushCanvas(220, kPosy + 80, UPDATE_MODE_GL16);
                LoadingIMG.fillCanvas(0);
                while (1) {
                    if (xQueueReceive(xQueue_Info, &p, 0)) {
                        std::string str(p);
                        free(p);
                        if (str.find("$RESUME") != std::string::npos) {
                            LoadingIMG.pushCanvas(220, kPosy + 80,
                                                  UPDATE_MODE_GC16);
                            break;
                        }
                    }
                    vTaskDelay(10);
                }
            } else {
                Info.fillCanvas(0);
                DrawInfo(Info, str);
                Info.pushCanvas(0, kPosy, UPDATE_MODE_DU);
            }
        }
        vTaskPrioritySet(NULL, 5);
        vTaskDelay(10);
    }
    vTaskDelete(NULL);
}

void SysInit_UpdateInfo(std::string info) {
    char *p = (char *)malloc(info.length() + 1);
    memcpy(p, info.c_str(), info.length());
    p[info.length()] = '\0';
    if (xQueueSend(xQueue_Info, &p, 0) == 0) {
        free(p);
    }
}
