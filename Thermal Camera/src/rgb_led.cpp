#include "rgb_led.hpp"

// Initialize NeoPixel strip
Adafruit_NeoPixel onboardLED(NUM_PIXELS, RGB_PIN, NEO_GRB + NEO_KHZ800);

void initRGB() {
    onboardLED.begin();
    onboardLED.setBrightness(LED_BRIGHTNESS);
    onboardLED.show(); // Clear to off
}

#include "thermal_types.hpp"

void ledAnimationTask(void *pvParameters) {
    uint32_t hue = 0;
    while (true) {
        bool displayErr = (globalDeviceStatus != STATUS_OK);
        bool sensorErr = (globalSensorStatus != SENSOR_OK);

        if (displayErr && sensorErr) {
            // BOTH DISPLAY & SENSOR DISCONNECTED: Rapid Alternating RED and YELLOW
            onboardLED.setPixelColor(0, onboardLED.Color(255, 0, 0)); // Red
            onboardLED.show();
            vTaskDelay(pdMS_TO_TICKS(150));

            onboardLED.setPixelColor(0, onboardLED.Color(255, 210, 0)); // Bright Yellow
            onboardLED.show();
            vTaskDelay(pdMS_TO_TICKS(150));
        } else if (displayErr) {
            // DISPLAY ERROR ONLY: Blinking RED
            onboardLED.setPixelColor(0, onboardLED.Color(255, 0, 0));
            onboardLED.show();
            vTaskDelay(pdMS_TO_TICKS(250));
            onboardLED.setPixelColor(0, 0);
            onboardLED.show();
            vTaskDelay(pdMS_TO_TICKS(250));
        } else if (sensorErr) {
            // SENSOR ERROR ONLY: Blinking BRIGHT YELLOW
            onboardLED.setPixelColor(0, onboardLED.Color(255, 210, 0));
            onboardLED.show();
            vTaskDelay(pdMS_TO_TICKS(300));
            onboardLED.setPixelColor(0, 0);
            onboardLED.show();
            vTaskDelay(pdMS_TO_TICKS(300));
        } else {
            // ALL SYSTEMS NORMAL: Smooth rainbow color cycle
            uint32_t color = onboardLED.gamma32(onboardLED.ColorHSV(hue));
            onboardLED.setPixelColor(0, color);
            onboardLED.show();
            hue += 256;
            if (hue >= 65536) {
                hue = 0;
            }
            vTaskDelay(pdMS_TO_TICKS(15));
        }
    }
}

