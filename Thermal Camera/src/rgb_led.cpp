#include "rgb_led.hpp"

// Initialize NeoPixel strip
Adafruit_NeoPixel onboardLED(NUM_PIXELS, RGB_PIN, NEO_GRB + NEO_KHZ800);

void initRGB() {
    onboardLED.begin();
    onboardLED.setBrightness(LED_BRIGHTNESS);
    onboardLED.show(); // Clear to off
}

void ledAnimationTask(void *pvParameters) {
    uint32_t hue = 0;
    while (true) {
        uint32_t color = onboardLED.gamma32(onboardLED.ColorHSV(hue));
        onboardLED.setPixelColor(0, color);
        onboardLED.show();
        hue += 256;
        if (hue >= 65536) {
            hue = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(15)); // Non-blocking delay
    }
}

