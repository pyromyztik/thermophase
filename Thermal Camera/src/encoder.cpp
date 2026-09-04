#include "encoder.hpp"
#include "thermal_types.hpp"

TaskHandle_t encoderTaskHandle = NULL;
static QueueHandle_t encoderQueue = NULL;

// 16-element quadrature state transition lookup table
// Index = (prev_state << 2) | curr_state
// Bit 1 = CLK, Bit 0 = DT
static const int8_t QEM[16] = {
    0, -1,  1,  0,
    1,  0,  0, -1,
   -1,  0,  0,  1,
    0,  1, -1,  0
};

static void postEvent(EncoderEventType evt) {
    if (encoderQueue != NULL) {
        xQueueSend(encoderQueue, &evt, 0);
    }
    // Instantly wake up the display/UI task if waiting
    if (displayTaskHandle != NULL) {
        xTaskNotifyGive(displayTaskHandle);
    }
}

bool initEncoder() {
    Serial.println("[ENCODER] Initializing HW-040 rotary encoder pins...");
    Serial.printf("  CLK: GPIO %d | DT: GPIO %d | SW: GPIO %d\n", 
                  PIN_ENCODER_CLK, PIN_ENCODER_DT, PIN_ENCODER_SW);

    pinMode(PIN_ENCODER_CLK, INPUT_PULLUP);
    pinMode(PIN_ENCODER_DT, INPUT_PULLUP);
    pinMode(PIN_ENCODER_SW, INPUT_PULLUP);

    encoderQueue = xQueueCreate(16, sizeof(EncoderEventType));
    if (encoderQueue == NULL) {
        Serial.println("[ENCODER] ERROR: Failed to create FreeRTOS encoderQueue!");
        return false;
    }

    Serial.println("[ENCODER] HW-040 encoder successfully initialized.");
    return true;
}

bool getEncoderEvent(EncoderEventType& outEvent, TickType_t waitTicks) {
    if (encoderQueue == NULL) {
        return false;
    }
    return (xQueueReceive(encoderQueue, &outEvent, waitTicks) == pdTRUE);
}

void encoderTask(void *pvParameters) {
    Serial.println("[TASK] HW-040 Encoder polling task started on Core " + String(xPortGetCoreID()));

    // Initial pin states
    uint8_t prevState = (digitalRead(PIN_ENCODER_CLK) << 1) | digitalRead(PIN_ENCODER_DT);
    int8_t rotAccum = 0;

    // Button debounce tracking
    bool lastRawBtn = HIGH;
    bool btnIsPressed = false;
    uint32_t btnPressTime = 0;

    while (true) {
        // --- 1. ROTARY QUADRATURE SCANNING ---
        uint8_t clkVal = digitalRead(PIN_ENCODER_CLK);
        uint8_t dtVal  = digitalRead(PIN_ENCODER_DT);
        uint8_t currState = (clkVal << 1) | dtVal;

        if (currState != prevState) {
            uint8_t idx = ((prevState & 0x03) << 2) | (currState & 0x03);
            int8_t delta = QEM[idx];
            rotAccum += delta;
            prevState = currState;

            // HW-040 encoders have physical detents at both state 3 (1,1) and state 0 (0,0) (2 transitions per detent click)
            if (currState == 0x03 || currState == 0x00) {
                if (rotAccum >= 1) {
                    postEvent(ENCODER_EVENT_ROT_CW);
                } else if (rotAccum <= -1) {
                    postEvent(ENCODER_EVENT_ROT_CCW);
                }
                rotAccum = 0;
            }
        }

        // --- 2. PUSH BUTTON DEBOUNCING ---
        bool rawBtn = digitalRead(PIN_ENCODER_SW);
        uint32_t now = millis();

        if (rawBtn == LOW && lastRawBtn == HIGH) {
            // Button pressed down
            btnPressTime = now;
            lastRawBtn = LOW;
        } else if (rawBtn == LOW && lastRawBtn == LOW) {
            // Holding down - verify stable debounce period >= 25ms
            if (!btnIsPressed && (now - btnPressTime >= 25)) {
                btnIsPressed = true;
            }
        } else if (rawBtn == HIGH && lastRawBtn == LOW) {
            // Released
            if (btnIsPressed) {
                // Valid debounced click event on button release
                postEvent(ENCODER_EVENT_CLICK);
            }
            btnIsPressed = false;
            lastRawBtn = HIGH;
        }

        // Sample at 2ms interval (500 Hz) - ultra-low CPU overhead (<0.1%) and perfect contact debouncing
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
