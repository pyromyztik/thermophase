#include "wheel.hpp"
#include "sensor.hpp"

TaskHandle_t wheelTaskHandle = NULL;
static QueueHandle_t wheelQueue = NULL;

// 16-element quadrature state transition lookup table
// Index = (prev_state << 2) | curr_state
// Bit 1 = CLK, Bit 0 = DT
static const int8_t QEM[16] = {
    0, -1,  1,  0,
    1,  0,  0, -1,
   -1,  0,  0,  1,
    0,  1, -1,  0
};

bool postWheelEvent(WheelEventType evt) {
    bool ok = false;
    if (wheelQueue != NULL) {
        ok = (xQueueSend(wheelQueue, &evt, 0) == pdTRUE);
    }
    // Instantly wake up the display/UI task if waiting
    if (displayTaskHandle != NULL) {
        xTaskNotifyGive(displayTaskHandle);
    }
    return ok;
}

bool initWheel() {
    Serial.println("[WHEEL] Initializing HW-040 rotary wheel pins...");
    Serial.printf("  CLK: GPIO %d | DT: GPIO %d | SW: GPIO %d\n", 
                  PIN_WHEEL_CLK, PIN_WHEEL_DT, PIN_WHEEL_SW);

    pinMode(PIN_WHEEL_CLK, INPUT_PULLUP);
    pinMode(PIN_WHEEL_DT, INPUT_PULLUP);
    pinMode(PIN_WHEEL_SW, INPUT_PULLUP);

    wheelQueue = xQueueCreate(16, sizeof(WheelEventType));
    if (wheelQueue == NULL) {
        Serial.println("[WHEEL] ERROR: Failed to create FreeRTOS wheelQueue!");
        return false;
    }

    Serial.println("[WHEEL] HW-040 rotary wheel successfully initialized.");
    return true;
}

bool getWheelEvent(WheelEventType& outEvent, TickType_t waitTicks) {
    if (wheelQueue == NULL) {
        return false;
    }
    return (xQueueReceive(wheelQueue, &outEvent, waitTicks) == pdTRUE);
}

void wheelTask(void *pvParameters) {
    Serial.println("[TASK] HW-040 Wheel polling task started on Core " + String(xPortGetCoreID()));

    // Initial pin states
    uint8_t prevState = (digitalRead(PIN_WHEEL_CLK) << 1) | digitalRead(PIN_WHEEL_DT);
    int8_t rotAccum = 0;

    // Button debounce tracking
    bool lastRawBtn = HIGH;
    bool btnIsPressed = false;
    bool longPressTriggered = false;
    uint32_t btnPressTime = 0;

    while (true) {
        // --- 1. ROTARY QUADRATURE SCANNING ---
        uint8_t clkVal = digitalRead(PIN_WHEEL_CLK);
        uint8_t dtVal  = digitalRead(PIN_WHEEL_DT);
        uint8_t currState = (clkVal << 1) | dtVal;

        if (currState != prevState) {
            uint8_t idx = ((prevState & 0x03) << 2) | (currState & 0x03);
            int8_t delta = QEM[idx];
            rotAccum += delta;
            prevState = currState;

            // HW-040 encoders have physical detents at both state 3 (1,1) and state 0 (0,0) (2 transitions per detent click)
            if (currState == 0x03 || currState == 0x00) {
                if (rotAccum >= 1) {
                    postWheelEvent(WHEEL_EVENT_ROT_CW);
                } else if (rotAccum <= -1) {
                    postWheelEvent(WHEEL_EVENT_ROT_CCW);
                }
                rotAccum = 0;
            }
        }

        // --- 2. PUSH BUTTON DEBOUNCING & LONG-PRESS DETECTION ---
        bool rawBtn = digitalRead(PIN_WHEEL_SW);
        uint32_t now = millis();

        if (rawBtn == LOW && lastRawBtn == HIGH) {
            // Button pressed down
            btnPressTime = now;
            lastRawBtn = LOW;
            longPressTriggered = false;
        } else if (rawBtn == LOW && lastRawBtn == LOW) {
            // Holding down - verify stable debounce period >= 25ms
            if (!btnIsPressed && (now - btnPressTime >= 25)) {
                btnIsPressed = true;
            }
            // Trigger long-press immediately once duration reaches 600ms
            if (btnIsPressed && !longPressTriggered && (now - btnPressTime >= 600)) {
                longPressTriggered = true;
                postWheelEvent(WHEEL_EVENT_LONG_PRESS);
            }
        } else if (rawBtn == HIGH && lastRawBtn == LOW) {
            // Released
            if (btnIsPressed && !longPressTriggered) {
                // Valid debounced short click event on button release
                postWheelEvent(WHEEL_EVENT_CLICK);
            }
            btnIsPressed = false;
            longPressTriggered = false;
            lastRawBtn = HIGH;
        }

        // Sample at 2ms interval (500 Hz) - ultra-low CPU overhead (<0.1%) and perfect contact debouncing
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
