// Replaces <M5EPD.h> for the ported factory test: M5Unified for the board,
// the EPD/canvas layer for drawing, and the remaining board services.
#pragma once

#include <cstdio>
#include <cstring>
#include <string>

#include <M5Unified.h>

#include "board.h"
#include "epd_canvas.h"
#include "epd_display.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define log_d(fmt, ...) ESP_LOGD("app", fmt, ##__VA_ARGS__)
#define log_i(fmt, ...) ESP_LOGI("app", fmt, ##__VA_ARGS__)
#define log_e(fmt, ...) ESP_LOGE("app", fmt, ##__VA_ARGS__)

// M5Paper buttons as named by M5EPD, mapped to M5Unified:
// BtnL = G37 = BtnA, BtnP = G38 = BtnB, BtnR = G39 = BtnC.
