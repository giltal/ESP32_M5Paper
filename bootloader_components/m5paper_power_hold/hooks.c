// M5Paper power latch, set from the second-stage bootloader.
//
// On battery the power button only powers the board while it is held; the
// firmware keeps itself on by driving PS_ON (GPIO2) high. The application
// reaches that point ~2 s after reset (image load, PSRAM test), so a short
// press would not power the device on. Latch it here instead, a few ms in.
#include "esp_rom_gpio.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"

#define M5PAPER_PS_ON_GPIO 2

// Referenced by the bootloader's link options so this file is kept.
void bootloader_hooks_include(void)
{
}

void bootloader_before_init(void)
{
    esp_rom_gpio_pad_select_gpio(M5PAPER_PS_ON_GPIO);
    gpio_ll_set_level(&GPIO, M5PAPER_PS_ON_GPIO, 1);
    gpio_ll_output_enable(&GPIO, M5PAPER_PS_ON_GPIO);
}
