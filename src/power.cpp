#include "power.hpp"
#include "config.hpp"
#include "display.hpp"
#include "rgb-led.hpp"
#include "sensor.hpp"
#include "wheel.hpp"
#include "storage.hpp"
#include <algorithm>
#include <numeric>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include <driver/gpio.h>

// Configuration constants
static constexpr size_t BATTERY_WINDOW_SECONDS = 30;
static constexpr int ADC_BURST_SAMPLES = 24;
static constexpr int ADC_TRIM_EACH_SIDE = 4;
static constexpr int ADC_USED_SAMPLES = ADC_BURST_SAMPLES - (2 * ADC_TRIM_EACH_SIDE);

// Software Charging Detection Thresholds
// Under 400mA system load, plugging in a ~1A TP4056 reverses cell current by ~1.0A (+600mA net charging),
// producing a +150mV to +300mV step jump across cell IR + wiring. Normal load swings are < 50mV.
static constexpr float STEP_JUMP_THRESHOLD_V    = 0.080f; // 80 mV step jump/drop between consecutive samples
static constexpr float WINDOW_STEP_THRESHOLD_V  = 0.080f; // 80 mV deviation from rolling average

// Internal state storage
static float voltWindow[BATTERY_WINDOW_SECONDS] = {0.0f};
static size_t voltWindowHead = 0;
static size_t voltWindowCount = 0;
static float voltWindowSum = 0.0f;

static uint32_t lastSampleTime = 0;
static bool bufferPrimed = false;
static float lastInstantVoltage = 0.0f;

static BatteryState currentConfirmedState = BATTERY_DISCHARGING;
static uint8_t chargeDebounce = 0;
static uint8_t dischargeDebounce = 0;

static const char* lastCandidateStr = "NONE";
static const char* lastCandidateReason = "-";
static float lastLoggedInstantStep = 0.0f;
static float lastLoggedWindowDrift = 0.0f;

static float displayedPctFloat = -1.0f;
static uint32_t lastPctStepTime = 0;

float getBatteryInstantVoltage() {
    uint32_t samples[ADC_BURST_SAMPLES];
    for (int i = 0; i < ADC_BURST_SAMPLES; ++i) {
        samples[i] = analogReadMilliVolts(PIN_BATTERY_ADC);
        delayMicroseconds(40);
    }

    // Sort to extract the trimmed median range (outlier rejection)
    std::sort(samples, samples + ADC_BURST_SAMPLES);

    uint32_t sum = 0;
    for (int i = ADC_TRIM_EACH_SIDE; i < ADC_BURST_SAMPLES - ADC_TRIM_EACH_SIDE; ++i) {
        sum += samples[i];
    }

    float pinVolts = (static_cast<float>(sum) / ADC_USED_SAMPLES) / 1000.0f;
    return pinVolts * BATTERY_DIVIDER_RATIO;
}

void sampleBattery() {
    uint32_t now = millis();
    if (bufferPrimed && (now - lastSampleTime < 1000)) {
        return;
    }
    lastSampleTime = now;

    float rawVolts = getBatteryInstantVoltage();

    // Re-prime on boot or when power switch reconnects
    if (!bufferPrimed || (currentConfirmedState == BATTERY_DISCONNECTED && rawVolts >= 1.80f)) {
        for (size_t i = 0; i < BATTERY_WINDOW_SECONDS; ++i) {
            voltWindow[i] = rawVolts;
        }
        voltWindowSum = rawVolts * BATTERY_WINDOW_SECONDS;
        voltWindowCount = BATTERY_WINDOW_SECONDS;
        voltWindowHead = 0;
        bufferPrimed = true;
        lastInstantVoltage = rawVolts;
        displayedPctFloat = -1.0f;
        currentConfirmedState = (rawVolts >= 1.80f) ? BATTERY_DISCHARGING : BATTERY_DISCONNECTED;
        chargeDebounce = 0;
        dischargeDebounce = 0;
        return;
    }

    // O(1) Running window update
    voltWindowSum -= voltWindow[voltWindowHead];
    voltWindow[voltWindowHead] = rawVolts;
    voltWindowSum += rawVolts;

    voltWindowHead = (voltWindowHead + 1) % BATTERY_WINDOW_SECONDS;
    if (voltWindowCount < BATTERY_WINDOW_SECONDS) {
        voltWindowCount++;
    }

    // Periodically recalculate exact sum when window wraps around to prevent floating-point accumulation drift
    if (voltWindowHead == 0) {
        voltWindowSum = std::accumulate(voltWindow, voltWindow + voltWindowCount, 0.0f);
    }
}

float getBatteryVoltage() {
    if (!bufferPrimed) {
        sampleBattery();
    }
    if (voltWindowCount == 0) return 0.0f;
    return voltWindowSum / static_cast<float>(voltWindowCount);
}

BatteryState updateBatteryState() {
    if (!bufferPrimed) {
        sampleBattery();
    }

    // Rely on the cached average and the latest slot in the buffer (avoids redundant ADC burst reads)
    size_t lastIdx = (voltWindowHead == 0) ? (BATTERY_WINDOW_SECONDS - 1) : (voltWindowHead - 1);
    float latestV = voltWindow[lastIdx];

    // 1. Hardware power-cut check (slide switch disconnected)
    if (latestV < 1.80f) {
        currentConfirmedState = BATTERY_DISCONNECTED;
        chargeDebounce = 0;
        dischargeDebounce = 0;
        return BATTERY_DISCONNECTED;
    }

    float avgV = getBatteryVoltage();

    // 2. Compute slope and step edge metrics:
    size_t oldestIdx = (voltWindowCount < BATTERY_WINDOW_SECONDS) ? 0 : voltWindowHead;
    float oldestV = voltWindow[oldestIdx];
    float windowDrift = latestV - oldestV; // Net voltage drift across the 30s window
    float stepFromAvg = latestV - avgV;    // Jump relative to 30-second rolling average
    float instantStep = (lastInstantVoltage > 0.0f) ? (latestV - lastInstantVoltage) : 0.0f;
    lastInstantVoltage = latestV;

    // 3. Multi-criteria software charging evaluation:
    bool isChargingCandidate = false;
    bool isDischargingCandidate = false;
    const char* candStr = "NONE";
    const char* reasonStr = "-";

    if (currentConfirmedState == BATTERY_CHARGING) {
        // We are currently charging. Look for UNPLUG indicators:
        if (instantStep <= -STEP_JUMP_THRESHOLD_V || stepFromAvg <= -WINDOW_STEP_THRESHOLD_V) {
            isDischargingCandidate = true;
            candStr = "DIS";
            reasonStr = "STEP_DROP";
        } else if (avgV < 4.14f && windowDrift <= -0.010f) {
            isDischargingCandidate = true;
            candStr = "DIS";
            reasonStr = "VOLT_SLOPE_DECAY";
        } else if (latestV < 4.05f && windowDrift <= 0.002f) {
            isDischargingCandidate = true;
            candStr = "DIS";
            reasonStr = "LOW_V_NO_SLOPE";
        }
    } else {
        // We are currently discharging. Look for PLUG-IN indicators:
        // (TP4056 1.0A reversal produces +150mV to +300mV step jump. Load rebounds are < 50mV)
        if (instantStep >= STEP_JUMP_THRESHOLD_V || stepFromAvg >= WINDOW_STEP_THRESHOLD_V) {
            isChargingCandidate = true;
            candStr = "CHG";
            reasonStr = "STEP_JUMP";
        }
    }

    lastCandidateStr = candStr;
    lastCandidateReason = reasonStr;
    lastLoggedInstantStep = instantStep;
    lastLoggedWindowDrift = windowDrift;

    // 4. Robust Debouncing:
    if (isChargingCandidate) {
        dischargeDebounce = 0;
        if (chargeDebounce < 4) chargeDebounce++;
        if (chargeDebounce >= 4) {
            currentConfirmedState = BATTERY_CHARGING;
        }
    } else if (isDischargingCandidate) {
        chargeDebounce = 0;
        if (dischargeDebounce < 3) dischargeDebounce++;
        if (dischargeDebounce >= 3) {
            currentConfirmedState = BATTERY_DISCHARGING;
        }
    } else {
        // Neutral / steady condition: gently decay debounce counters
        if (chargeDebounce > 0) chargeDebounce--;
        if (dischargeDebounce > 0) dischargeDebounce--;
    }

    return currentConfirmedState;
}

BatteryState getBatteryState() {
    return updateBatteryState();
}

void getBatteryTelemetry(BatteryTelemetry& out) {
    size_t lastIdx = (voltWindowHead == 0) ? (BATTERY_WINDOW_SECONDS - 1) : (voltWindowHead - 1);
    out.instantV = voltWindow[lastIdx];
    out.avgV = getBatteryVoltage();
    out.step = lastLoggedInstantStep;
    out.drift = lastLoggedWindowDrift;
    out.state = currentConfirmedState;
    out.percentage = getBatteryPercentage();
    out.chargeDebounce = chargeDebounce;
    out.dischargeDebounce = dischargeDebounce;
    out.candidate = lastCandidateStr;
    out.reason = lastCandidateReason;
}

struct TableEntry { float v; uint8_t pct; };

// Real-World Under-Load (0.2C / 400mA) LiPo Discharge Curve
// Calibrated for ESP32-S3 + ILI9341 LCD + Backlight + MLX90640 running under 5V boost.
// Accounts for ~150mV cell IR + protection circuit + switch + wiring drop from resting open-circuit.
static constexpr TableEntry lipoDischargeTable[] = {
    {4.04f, 100}, // 4.20V resting open-circuit sags to ~4.04V under 400mA load
    {3.98f,  95},
    {3.92f,  90},
    {3.87f,  85},
    {3.83f,  80},
    {3.79f,  75},
    {3.75f,  70},
    {3.72f,  65},
    {3.69f,  60},
    {3.66f,  55},
    {3.63f,  50},
    {3.60f,  45},
    {3.57f,  40},
    {3.54f,  35},
    {3.50f,  30},
    {3.46f,  25},
    {3.41f,  20},
    {3.35f,  15},
    {3.27f,  10},
    {3.15f,   5},
    {3.00f,   0}
};

// Real-World Under-Charge (~600mA net into cell) LiPo Charging Curve
// Compensates for internal resistance voltage drop reversal, ensuring monotonic,
// jump-free percentage continuity when plugging in or unplugging the charger.
static constexpr TableEntry lipoChargeTable[] = {
    {4.20f, 100}, {4.16f,  95}, {4.11f,  90}, {4.06f,  85},
    {4.01f,  80}, {3.97f,  75}, {3.93f,  70}, {3.89f,  65},
    {3.86f,  60}, {3.83f,  55}, {3.80f,  50}, {3.77f,  45},
    {3.74f,  40}, {3.70f,  35}, {3.66f,  30}, {3.61f,  25},
    {3.56f,  20}, {3.50f,  15}, {3.42f,  10}, {3.32f,   5},
    {3.15f,   0}
};

static uint8_t interpolateTable(const TableEntry* table, size_t size, float v) {
    if (v >= table[0].v) return table[0].pct;
    if (v <= table[size - 1].v) return table[size - 1].pct;

    for (size_t i = 0; i < size - 1; ++i) {
        if (v >= table[i + 1].v) {
            float span = table[i].v - table[i + 1].v;
            float ratio = (v - table[i + 1].v) / span;
            float pct = table[i + 1].pct + ratio * (table[i].pct - table[i + 1].pct);
            return static_cast<uint8_t>(pct + 0.5f);
        }
    }
    return 0;
}

static uint8_t calcTablePercentage(float v, BatteryState state) {
    if (state == BATTERY_CHARGING) {
        constexpr size_t sz = sizeof(lipoChargeTable) / sizeof(lipoChargeTable[0]);
        return interpolateTable(lipoChargeTable, sz, v);
    } else {
        constexpr size_t sz = sizeof(lipoDischargeTable) / sizeof(lipoDischargeTable[0]);
        return interpolateTable(lipoDischargeTable, sz, v);
    }
}

void reprimeBatteryUnderLoad() {
    // Re-primes the rolling window with the true under-load terminal voltage
    // after display backlight, MLX90640, and both cores are actively running.
    float rawVolts = getBatteryInstantVoltage();
    if (rawVolts >= 1.80f) {
        for (size_t i = 0; i < BATTERY_WINDOW_SECONDS; ++i) {
            voltWindow[i] = rawVolts;
        }
        voltWindowSum = rawVolts * BATTERY_WINDOW_SECONDS;
        voltWindowCount = BATTERY_WINDOW_SECONDS;
        voltWindowHead = 0;
        lastInstantVoltage = rawVolts;

        uint8_t targetPct = calcTablePercentage(rawVolts, currentConfirmedState);
        displayedPctFloat = static_cast<float>(targetPct);
        lastPctStepTime = millis();
        Serial.printf("[PWR] Battery calibrated under running load: %.3fV (%d%%)\n", rawVolts, targetPct);
    }
}

uint8_t getBatteryPercentage() {
    float v = getBatteryVoltage();
    uint8_t targetPct = calcTablePercentage(v, currentConfirmedState);
    uint32_t now = millis();

    if (displayedPctFloat < 0.0f || currentConfirmedState == BATTERY_DISCONNECTED) {
        displayedPctFloat = static_cast<float>(targetPct);
        lastPctStepTime = now;
        return targetPct;
    }

    if (currentConfirmedState == BATTERY_CHARGING) {
        // Smoothly increment toward targetPct during active charging (at most 1% per 20s)
        if (static_cast<float>(targetPct) > displayedPctFloat) {
            if (now - lastPctStepTime >= 20000) {
                displayedPctFloat += 1.0f;
                lastPctStepTime = now;
            }
        }
        displayedPctFloat = std::max(0.0f, std::min(100.0f, displayedPctFloat));
        return static_cast<uint8_t>(displayedPctFloat + 0.5f);
    }

    // Normal discharging:
    // displayedPctFloat smoothly tracks targetPct without artificial fast countdowns.
    if (static_cast<float>(targetPct) < displayedPctFloat) {
        // If targetPct is significantly lower (diverged by > 2%), catch up every 30s; otherwise step down at most 1% per 60s
        uint32_t stepInterval = (displayedPctFloat - static_cast<float>(targetPct) > 2.0f) ? 30000 : 60000;
        if (now - lastPctStepTime >= stepInterval) {
            displayedPctFloat -= 1.0f;
            lastPctStepTime = now;
        }
    } else if (static_cast<float>(targetPct) > displayedPctFloat + 1.0f) {
        // Allow slow upward recovery only if sustained (e.g. brightness decreased)
        if (now - lastPctStepTime >= 90000) {
            displayedPctFloat += 1.0f;
            lastPctStepTime = now;
        }
    }

    displayedPctFloat = std::max(0.0f, std::min(100.0f, displayedPctFloat));
    return static_cast<uint8_t>(displayedPctFloat + 0.5f);
}

void initBatteryMonitor() {
    pinMode(PIN_BATTERY_ADC, INPUT);
    analogSetPinAttenuation(PIN_BATTERY_ADC, ADC_11db);

    bufferPrimed = false;
    voltWindowCount = 0;
    voltWindowHead = 0;
    voltWindowSum = 0.0f;
    lastInstantVoltage = 0.0f;
    displayedPctFloat = -1.0f;
    chargeDebounce = 0;
    dischargeDebounce = 0;
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
