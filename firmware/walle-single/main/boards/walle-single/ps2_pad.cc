#include "ps2_pad.h"
#include "board_config.h"
#include "esp_check.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
uint8_t Transfer(uint8_t output) {
    uint8_t input = 0;
    for (int bit = 0; bit < 8; ++bit) {
        gpio_set_level(WALLE_PS2_CMD, (output >> bit) & 1);
        gpio_set_level(WALLE_PS2_CLK, 0);
        esp_rom_delay_us(10);
        input |= gpio_get_level(WALLE_PS2_DAT) << bit;
        gpio_set_level(WALLE_PS2_CLK, 1);
        esp_rom_delay_us(10);
    }
    gpio_set_level(WALLE_PS2_CMD, 1);
    esp_rom_delay_us(20);
    return input;
}

void SendCommand(const uint8_t* command, size_t size) {
    gpio_set_level(WALLE_PS2_CLK, 1);
    gpio_set_level(WALLE_PS2_CMD, 1);
    gpio_set_level(WALLE_PS2_ATT, 0);
    esp_rom_delay_us(20);
    for (size_t i = 0; i < size; ++i) Transfer(command[i]);
    gpio_set_level(WALLE_PS2_ATT, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

void ConfigurePad() {
    // Start by leaving any configuration state left by a previous host/reset.
    constexpr uint8_t exit[] = {0x01,0x43,0x00,0x00,0x5a,0x5a,0x5a,0x5a,0x5a};
    constexpr uint8_t enter[] = {0x01,0x43,0x00,0x01,0x00,0x00,0x00,0x00,0x00};
    constexpr uint8_t analog[] = {0x01,0x44,0x00,0x01,0x03,0x00,0x00,0x00,0x00};
    SendCommand(exit, sizeof(exit));
    SendCommand(enter, sizeof(enter));
    SendCommand(analog, sizeof(analog));
    SendCommand(exit, sizeof(exit));
}
}

void InitPs2Pad() {
    // Set idle levels before enabling outputs to avoid a spurious selection.
    gpio_set_level(WALLE_PS2_ATT, 1);
    gpio_set_level(WALLE_PS2_CLK, 1);
    gpio_set_level(WALLE_PS2_CMD, 1);
    gpio_config_t config = {};
    config.pin_bit_mask = (1ULL << WALLE_PS2_ATT) | (1ULL << WALLE_PS2_CLK) |
                          (1ULL << WALLE_PS2_CMD);
    config.mode = GPIO_MODE_OUTPUT;
    ESP_ERROR_CHECK(gpio_config(&config));
    ESP_LOGI("PS2", "Driver v5 ready (dual sticks + arm buttons): DAT=%d CMD=%d ATT=%d CLK=%d",
             WALLE_PS2_DAT, WALLE_PS2_CMD, WALLE_PS2_ATT, WALLE_PS2_CLK);
    config.pin_bit_mask = 1ULL << WALLE_PS2_DAT;
    config.mode = GPIO_MODE_INPUT;
    config.pull_up_en = GPIO_PULLUP_ENABLE;
    ESP_ERROR_CHECK(gpio_config(&config));
}

bool ReadPs2Pad(uint16_t& pressed, int& left_x, int& left_y, int& right_x, int& right_y) {
    pressed = 0;
    left_x = left_y = right_x = right_y = 128;
    static bool needs_configuration = true;
    static int64_t next_configuration = 0;
    if (needs_configuration && esp_timer_get_time() >= next_configuration) {
        ESP_LOGI("PS2", "Initializing receiver (exit config / analog mode)");
        ConfigurePad();
        next_configuration = esp_timer_get_time() + 1000000;
    }
    gpio_set_level(WALLE_PS2_ATT, 0);
    esp_rom_delay_us(20);
    const uint8_t first = Transfer(0x01);
    const uint8_t mode = Transfer(0x42);
    const uint8_t header = Transfer(0x00);
    const uint8_t low = Transfer(0x00);
    const uint8_t high = Transfer(0x00);
    // Digital, analog, and pressure modes share the same button bytes.
    // Some wireless receivers expect the complete nine-byte poll even in digital mode.
    const int length = mode == 0x79 ? 21 : 9;
    for (int i = 5; i < length; ++i) {
        const uint8_t value = Transfer(0x00);
        if (mode == 0x73 || mode == 0x79) {
            if (i == 5) right_x = value;
            if (i == 6) right_y = value;
            if (i == 7) left_x = value;
            if (i == 8) left_y = value;
        }
    }
    gpio_set_level(WALLE_PS2_ATT, 1);
    const bool valid = header == 0x5a &&
        (mode == 0x41 || mode == 0x42 || mode == 0x73 || mode == 0x79);
    needs_configuration = !valid;
    // The first response byte varies between third-party receivers; validate
    // the mode and acknowledgement rather than requiring an idle 0xff byte.
    static int64_t next_log = 0;
    static uint16_t previous = 0xffff;
    const uint16_t buttons = static_cast<uint16_t>(~(low | (high << 8)));
    const int64_t now = esp_timer_get_time();
    if (now >= next_log || (valid && buttons != previous)) {
        ESP_LOGI("PS2", "rx=%02x %02x %02x %02x %02x valid=%d pressed=%04x LX=%d LY=%d RX=%d RY=%d",
                 first, mode, header, low, high, valid, buttons, left_x, left_y, right_x, right_y);
        next_log = now + 3000000;
    }
    previous = buttons;
    if (!valid) return false;
    pressed = static_cast<uint16_t>(~(low | (high << 8)));
    return true;
}
