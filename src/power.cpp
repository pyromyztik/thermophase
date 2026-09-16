#include "power.hpp"
#include "config.hpp"
#include "display.hpp"
#include "rgb-led.hpp"
#include "sensor.hpp"
#include "wheel.hpp"
#include "storage.hpp"
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <driver/gpio.h>

static constexpr size_t BATTERY_WINDOW_SECONDS = 30;
static float voltWindow[BATTERY_WINDOW_SECONDS] = {0};
static size_t voltWindowHead = 0;
static size_t voltWindowCount = 0;
static uint32_t lastSampleTime = 0;
static bool bufferPrimed = false;

static BatteryState currentConfirmedState = BATTERY_DISCHARGING;
static uint8_t chargeDebounce = 0;
static uint8_t dischargeDebounce = 0;
static float displayedPctFloat = -1.0f;
static uint32_t lastPctStepTime = 0;

float getBatteryInstantVoltage() {
    // 24-sample multi-read with trimmed-mean outlier rejection
    // Completely eliminates SPI/I2C digital switching noise spikes
    constexpr int TOTAL_SAMPLES = 24;
    uint32_t samples[TOTAL_SAMPLES];
    for (int i = 0; i < TOTAL_SAMPLES; i++) {
        samples[i] = analogReadMilliVolts(PIN_BATTERY_ADC);
        delayMicroseconds(40);
    }

    // In-place insertion sort
    for (int i = 1; i < TOTAL_SAMPLES; i++) {
        uint32_t key = samples[i];
        int j = i - 1;
        while (j >= 0 && samples[j] > key) {
            samples[j + 1] = samples[j];
            j--;
        }
        samples[j + 1] = key;
    }

    // Discard top 4 and bottom 4 samples (average middle 16 samples)
    uint32_t sum = 0;
    for (int i = 4; i < TOTAL_SAMPLES - 4; i++) {
        sum += samples[i];
    }
    float pinVolts = ((float)sum / 16.0f) / 1000.0f;
    return pinVolts * BATTERY_DIVIDER_RATIO;
}

void sampleBattery() {
    uint32_t now = millis();
    if (now - lastSampleTime < 950 && bufferPrimed) {
        return;
    }
    lastSampleTime = now;

    float rawVolts = getBatteryInstantVoltage();

    // Cold boot under-load initialization:
    // When the first sample runs after the display & sensor tasks are active,
    // prime all 30 slots with this actual under-load operating voltage.
    if (!bufferPrimed) {
        for (size_t i = 0; i < BATTERY_WINDOW_SECONDS; i++) {
            voltWindow[i] = rawVolts;
        }
        voltWindowCount = BATTERY_WINDOW_SECONDS;
        voltWindowHead = 0;
        bufferPrimed = true;
        return;
    }

    // If recovering from switch-off (< 1.8V to >= 1.8V), re-prime immediately
    if (currentConfirmedState == BATTERY_DISCONNECTED && rawVolts >= 1.80f) {
        for (size_t i = 0; i < BATTERY_WINDOW_SECONDS; i++) {
            voltWindow[i] = rawVolts;
        }
        voltWindowCount = BATTERY_WINDOW_SECONDS;
        voltWindowHead = 0;
        displayedPctFloat = -1.0f;
        currentConfirmedState = BATTERY_DISCHARGING;
        chargeDebounce = 0;
        dischargeDebounce = 0;
    } else {
        voltWindow[voltWindowHead] = rawVolts;
        voltWindowHead = (voltWindowHead + 1) % BATTERY_WINDOW_SECONDS;
        if (voltWindowCount < BATTERY_WINDOW_SECONDS) {
            voltWindowCount++;
        }
    }
}

void initBatteryMonitor() {
    pinMode(PIN_BATTERY_ADC, INPUT);
    analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);
    // Note: Buffer priming is deferred until under-load operation in displayTask / UI init
    bufferPrimed = false;
    voltWindowCount = 0;
    voltWindowHead = 0;
    displayedPctFloat = -1.0f;
}

float getBatteryVoltage() {
    sampleBattery();
    if (voltWindowCount == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < voltWindowCount; i++) {
        sum += voltWindow[i];
    }
    return sum / (float)voltWindowCount;
}

BatteryState getBatteryState() {
    float instV = getBatteryInstantVoltage();

    // 1. If instantaneous voltage is below 1.8V, the slide switch has disconnected the battery.
    if (instV < 1.80f) {
        currentConfirmedState = BATTERY_DISCONNECTED;
        chargeDebounce = 0;
        dischargeDebounce = 0;
        return BATTERY_DISCONNECTED;
    }

    float avgV = getBatteryVoltage();

    // 2. Charging candidate evaluation:
    // Under 400mA system load, a discharging battery physically CANNOT exceed 4.10V.
    // When connected to the TP4056 USB charger, the rail is clamped at >= 4.12V.
    // This completely eliminates false charging triggers from thermal/load fluctuations.
    bool isCharging = (avgV >= 4.12f);

    if (isCharging) {
        dischargeDebounce = 0;
        if (chargeDebounce < 5) chargeDebounce++;
        if (chargeDebounce >= 5) {
            currentConfirmedState = BATTERY_CHARGING;
        }
    } else {
        chargeDebounce = 0;
        if (dischargeDebounce < 5) dischargeDebounce++;
        if (dischargeDebounce >= 5) {
            currentConfirmedState = BATTERY_DISCHARGING;
        }
    }

    return currentConfirmedState;
}

static uint8_t calcTablePercentage(float v) {
    if (v >= 4.12f) return 100;
    if (v <= 3.00f) return 0;

    // Real-World Under-Load (0.2C / 400mA) LiPo Discharge Curve
    // Calibrated for ESP32-S3 + ILI9341 LCD + Backlight + MLX90640 running under 5V boost
    static const struct { float v; uint8_t pct; } lipoTableLoaded[] = {
        {4.12f, 100},
        {4.02f,  95},
        {3.92f,  90},
        {3.85f,  85},
        {3.79f,  80},
        {3.74f,  75},
        {3.70f,  70},
        {3.67f,  65},
        {3.64f,  60},
        {3.61f,  55},
        {3.58f,  50},
        {3.55f,  45},
        {3.52f,  40},
        {3.49f,  35},
        {3.46f,  30},
        {3.42f,  25},
        {3.38f,  20},
        {3.33f,  15},
        {3.27f,  10},
        {3.18f,   5},
        {3.00f,   0}
    };
    constexpr size_t tableSize = sizeof(lipoTableLoaded) / sizeof(lipoTableLoaded[0]);

    for (size_t i = 0; i < tableSize - 1; i++) {
        if (v >= lipoTableLoaded[i + 1].v) {
            float span = lipoTableLoaded[i].v - lipoTableLoaded[i + 1].v;
            float ratio = (v - lipoTableLoaded[i + 1].v) / span;
            float pct = lipoTableLoaded[i + 1].pct + ratio * (lipoTableLoaded[i].pct - lipoTableLoaded[i + 1].pct);
            return (uint8_t)(pct + 0.5f);
        }
    }
    return 0;
}

uint8_t getBatteryPercentage() {
    float v = getBatteryVoltage();
    uint8_t targetPct = calcTablePercentage(v);
    uint32_t now = millis();

    // 1. Initial boot / initialization under load:
    if (displayedPctFloat < 0.0f) {
        displayedPctFloat = (float)targetPct;
        lastPctStepTime = now;
        return targetPct;
    }

    // 2. If recovering from switch disconnected:
    if (currentConfirmedState == BATTERY_DISCONNECTED) {
        displayedPctFloat = (float)targetPct;
        lastPctStepTime = now;
        return targetPct;
    }

    // 3. When actively charging:
    if (currentConfirmedState == BATTERY_CHARGING) {
        if ((float)targetPct > displayedPctFloat) {
            displayedPctFloat += ((float)targetPct - displayedPctFloat) * 0.1f;
        }
        return (uint8_t)(displayedPctFloat + 0.5f);
    }

    // 4. Normal battery discharging (ZERO flash writes! Pure rate-limited filter):
    // Under 400mA load on 2000mAh battery, true chemical drain is at most 1% per 150-180 seconds.
    // To prevent noise spikes from causing sudden downward ratcheting, the displayed percentage
    // is physically rate-limited to decrease by at most 1% per 25 seconds towards targetPct.
    if ((float)targetPct < displayedPctFloat) {
        if (now - lastPctStepTime >= 25000) {
            displayedPctFloat -= 1.0f;
            lastPctStepTime = now;
        }
    } else if ((float)targetPct > displayedPctFloat + 2.0f) {
        // If voltage genuinely rebounded significantly (e.g. rested or low-current charge),
        // allow gentle catch-up only after 60 seconds
        if (now - lastPctStepTime >= 60000) {
            displayedPctFloat += 0.5f;
            lastPctStepTime = now;
        }
    }

    if (displayedPctFloat < 0.0f) displayedPctFloat = 0.0f;
    if (displayedPctFloat > 100.0f) displayedPctFloat = 100.0f;

    return (uint8_t)(displayedPctFloat + 0.5f);
}

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
    // Save any pending configuration changes to NVS before powering down
    saveSettingsIfDirty();

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
