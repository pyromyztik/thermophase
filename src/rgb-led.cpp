#include "rgb-led.hpp"

// Initialize NeoPixel strip
Adafruit_NeoPixel onboardLED(NUM_PIXELS, RGB_PIN, NEO_GRB + NEO_KHZ800);

void initRGB() {
    onboardLED.begin();
    onboardLED.setBrightness(LED_BRIGHTNESS);
    onboardLED.show(); // Clear to off
}

#include "sensor.hpp"
#include "ui.hpp"
#include "ppt.hpp"

void ledAnimationTask(void *pvParameters) {
    uint32_t hue = 0;
    uint8_t pulseVal = 50;
    int8_t pulseDir = 3;

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
            // ALL SYSTEMS NORMAL: Contextual mode feedback
            AppState state = getAppState();
            if (state == APP_STATE_PPT_ARMED) {
                // PPT ARMED: Pulsing Amber / Golden Yellow
                pulseVal += pulseDir;
                if (pulseVal >= 220 || pulseVal <= 40) pulseDir = -pulseDir;
                onboardLED.setPixelColor(0, onboardLED.Color(pulseVal, (pulseVal * 180) / 255, 0));
                onboardLED.show();
                vTaskDelay(pdMS_TO_TICKS(20));
            } else if (state == APP_STATE_PPT_ACQUIRING) {
                // PPT ACQUIRING: Fast flashing Red burst indicator
                onboardLED.setPixelColor(0, onboardLED.Color(255, 0, 0));
                onboardLED.show();
                vTaskDelay(pdMS_TO_TICKS(60));
                onboardLED.setPixelColor(0, onboardLED.Color(30, 0, 0));
                onboardLED.show();
                vTaskDelay(pdMS_TO_TICKS(60));
            } else if (state == APP_STATE_PPT_INSPECT) {
                // PPT INSPECT: Color mapped to frequency / depth slice (Cyan for Deep -> Magenta for Shallow)
                int bin = pptGetBin();
                uint32_t binHue = 35000 + ((uint32_t)(bin - 1) * 26000) / 15;
                uint32_t color = onboardLED.gamma32(onboardLED.ColorHSV(binHue, 255, 180));
                onboardLED.setPixelColor(0, color);
                onboardLED.show();
                vTaskDelay(pdMS_TO_TICKS(40));
            } else {
                // LIVE VIEW & MENUS: Smooth rainbow cycle
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
}

