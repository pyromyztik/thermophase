#include "power.hpp"
#include "config.hpp"
#include "display.hpp"
#include "rgb-led.hpp"
#include "sensor.hpp"
#include "wheel.hpp"
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <driver/gpio.h>

bool wasWokenFromDeepSleep() {
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    return (cause == ESP_SLEEP_WAKEUP_EXT0 || cause == ESP_SLEEP_WAKEUP_EXT1);
}

void powerWakeInit() {
    // Release any pin holds from sleep mode
    gpio_hold_dis((gpio_num_t)RGB_PIN);
    gpio_hold_dis((gpio_num_t)TFT_LED);

    // Immediately drive TFT backlight LOW to prevent white flash
    pinMode(TFT_LED, OUTPUT);
    digitalWrite(TFT_LED, LOW);

    if (wasWokenFromDeepSleep()) {
        Serial.println("\n[PWR] System awakened from Deep Sleep via HW-040 button click!");
        rtc_gpio_deinit((gpio_num_t)PIN_WHEEL_SW);
    }
}

void enterDeepSleep() {
    Serial.println("\n[PWR] Entering Deep Sleep mode (~10 uA)...");
    Serial.println("[PWR] Powering off display backlight, LCD panel, and RGB LED.");
    Serial.println("[PWR] Click the HW-040 rotary knob to power on.\n");
    Serial.flush();

    // 1. Delete background FreeRTOS tasks to stop LED animations and sensor polling
    if (ledTaskHandle != NULL) {
        vTaskDelete(ledTaskHandle);
        ledTaskHandle = NULL;
    }
    if (sensorTaskHandle != NULL) {
        vTaskDelete(sensorTaskHandle);
        sensorTaskHandle = NULL;
    }
    if (wheelTaskHandle != NULL) {
        vTaskDelete(wheelTaskHandle);
        wheelTaskHandle = NULL;
    }
    if (diagTaskHandle != NULL) {
        vTaskDelete(diagTaskHandle);
        diagTaskHandle = NULL;
    }

    // 2. Extinguish on-board NeoPixel RGB LED and latch the 0 value
    onboardLED.clear();
    onboardLED.setPixelColor(0, 0);
    onboardLED.show();
    delay(20);
    onboardLED.clear();
    onboardLED.setPixelColor(0, 0);
    onboardLED.show();
    delay(20);

    // 3. Drive NeoPixel pin LOW and lock it during sleep to prevent floating voltage
    pinMode(RGB_PIN, OUTPUT);
    digitalWrite(RGB_PIN, LOW);
    gpio_hold_en((gpio_num_t)RGB_PIN);

    // 4. Turn off TFT backlight pin immediately and lock it
    ledcWrite(TFT_LED_PWM_CHANNEL, 0);
    ledcDetachPin(TFT_LED);
    pinMode(TFT_LED, OUTPUT);
    digitalWrite(TFT_LED, LOW);
    gpio_hold_en((gpio_num_t)TFT_LED);

    // 5. Put ILI9341 display controller into hardware sleep mode
    tft.sendCommand(ILI9341_SLPIN);

    // 6. Wait for the user to release the HW-040 button to prevent instant re-trigger
    while (digitalRead(PIN_WHEEL_SW) == LOW) {
        delay(10);
    }
    delay(150); // Debounce delay after release

    // 7. Initialize and configure RTC GPIO 6 with internal pull-up
    rtc_gpio_init((gpio_num_t)PIN_WHEEL_SW);
    rtc_gpio_set_direction((gpio_num_t)PIN_WHEEL_SW, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_en((gpio_num_t)PIN_WHEEL_SW);
    rtc_gpio_pulldown_dis((gpio_num_t)PIN_WHEEL_SW);

    // 8. Enable EXT0 wake-up for active-LOW on GPIO 6
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_WHEEL_SW, 0);

    // 9. Enter Deep Sleep
    esp_deep_sleep_start();
}
