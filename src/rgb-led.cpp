#include "rgb-led.hpp"
#include "power.hpp"

// Initialize NeoPixel strip
Adafruit_NeoPixel onboardLED(NUM_PIXELS, RGB_PIN, NEO_GRB + NEO_KHZ800);

void initRGB() {
    onboardLED.begin();
    onboardLED.setBrightness(LED_BRIGHTNESS);
    onboardLED.show(); // Clear to off
}

void turnOffRGB() {
    onboardLED.clear();
    onboardLED.setPixelColor(0, 0);
    onboardLED.show();
    delay(5);
    onboardLED.clear();
    onboardLED.setPixelColor(0, 0);
    onboardLED.show();
}

#include "sensor.hpp"
#include "ui.hpp"
#include "ppt.hpp"
#include "display.hpp"

void ledAnimationTask(void *pvParameters) {
    uint32_t hue = 0;
    uint8_t pulseVal = 50;
    int8_t pulseDir = 3;

    while (true) {
        if (isCameraStandby()) {
            turnOffRGB();
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

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
            if (!getRgbLedEnabledSetting()) {
                onboardLED.clear();
                onboardLED.setPixelColor(0, 0);
                onboardLED.show();
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }

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
            } else if (state == APP_STATE_THERMAL_VIEW || state == APP_STATE_THERMAL_FREEZE) {
                // LIVE THERMAL VIEW: Smoothly interpolate RGB LED to match the center reticle color
                float range = 0.0f;
                float effectiveMin = 0.0f;
                computeEffectiveThermalRange(displayMinTemp, displayMaxTemp, effectiveMin, range);
                float norm = (displayCenterTemp - effectiveMin) / range;
                if (norm < 0.0f) norm = 0.0f;
                if (norm > 1.0f) norm = 1.0f;
                int palIdx = (int)(norm * 255.0f);
                if (palIdx > 255) palIdx = 255;
                if (palIdx < 0) palIdx = 0;

                const uint16_t* pal = getActivePalette();
                if (pal != nullptr) {
                    uint16_t rgb565 = pal[palIdx];
                    // Unpack target RGB565 to 8-bit RGB components
                    float targetR = (float)(((rgb565 >> 11) & 0x1F) * 255 / 31);
                    float targetG = (float)(((rgb565 >> 5) & 0x3F) * 255 / 63);
                    float targetB = (float)((rgb565 & 0x1F) * 255 / 31);

                    static float smoothR = 0.0f;
                    static float smoothG = 0.0f;
                    static float smoothB = 0.0f;
                    static bool smoothInited = false;

                    if (!smoothInited) {
                        smoothR = targetR;
                        smoothG = targetG;
                        smoothB = targetB;
                        smoothInited = true;
                    } else {
                        // Smooth exponential low-pass filter (alpha = 0.08 at 50Hz update rate)
                        // Filters out MLX90640 sensor noise and provides fluid liquid-like color morphing
                        const float alpha = 0.08f;
                        smoothR += (targetR - smoothR) * alpha;
                        smoothG += (targetG - smoothG) * alpha;
                        smoothB += (targetB - smoothB) * alpha;
                    }

                    uint8_t r = (uint8_t)(smoothR + 0.5f);
                    uint8_t g = (uint8_t)(smoothG + 0.5f);
                    uint8_t b = (uint8_t)(smoothB + 0.5f);
                    onboardLED.setPixelColor(0, onboardLED.Color(r, g, b));
                    onboardLED.show();
                }
                vTaskDelay(pdMS_TO_TICKS(20));
            } else {
                // MENUS & BOOT: Smooth rainbow cycle rotation
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

