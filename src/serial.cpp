#include "serial.hpp"
#include "config.hpp"
#include "wheel.hpp"
#include "display.hpp"
#include "ui.hpp"
#include "diagnostics.hpp"

extern TaskHandle_t diagTaskHandle;

enum EscState {
    ESC_IDLE,
    ESC_SEEN,
    ESC_BRACKET
};

static void handleSerialCommand(const String& cmd) {
    if (cmd.equalsIgnoreCase("diagnostics")) {
        if (diagTaskHandle == NULL) {
            Serial.println("\n[CMD] Spawning System Diagnostics Task...");
            xTaskCreatePinnedToCore(
                diagnosticsMessageTask,
                "Diag_Task",
                DIAG_TASK_STACK,
                NULL,
                DIAG_TASK_PRIO,
                &diagTaskHandle,
                DIAG_TASK_CORE
            );
        } else {
            Serial.println("\n[WARN] Diagnostics task is already running.");
        }
    } else if (cmd.equalsIgnoreCase("compute") ||
               cmd.equalsIgnoreCase("perf") ||
               cmd.equalsIgnoreCase("headroom") ||
               cmd.equalsIgnoreCase("profile")) {
        printComputeHeadroomReport();
    } else if (cmd.equalsIgnoreCase("up") || cmd.equalsIgnoreCase("u")) {
        postWheelEvent(WHEEL_EVENT_ROT_CCW);
        Serial.println("[SERIAL] Wheel Up (CCW)");
    } else if (cmd.equalsIgnoreCase("down") || cmd.equalsIgnoreCase("d")) {
        postWheelEvent(WHEEL_EVENT_ROT_CW);
        Serial.println("[SERIAL] Wheel Down (CW)");
    } else if (cmd.equalsIgnoreCase("click") || cmd.equalsIgnoreCase("c")) {
        postWheelEvent(WHEEL_EVENT_CLICK);
        Serial.println("[SERIAL] Wheel Click");
    } else if (cmd.equalsIgnoreCase("hold") || cmd.equalsIgnoreCase("h")) {
        postWheelEvent(WHEEL_EVENT_LONG_PRESS);
        Serial.println("[SERIAL] Wheel Long Press (Hold)");
    } else if (cmd.startsWith("brightness") || cmd.startsWith("bright")) {
        int spaceIdx = cmd.indexOf(' ');
        if (spaceIdx > 0) {
            int val = cmd.substring(spaceIdx + 1).toInt();
            if (val >= 10 && val <= 100) {
                setDisplayBrightness((uint8_t)val);
                setDisplayBrightnessSetting((uint8_t)val);
                updateSettingsValues();
                updateBrightnessValues();
                Serial.printf("[SERIAL] Display Brightness set to %d%%\n", val);
            } else {
                Serial.println("[SERIAL] Error: Brightness must be between 10 and 100.");
            }
        } else {
            Serial.printf("[SERIAL] Current Display Brightness: %d%%\n", getDisplayBrightness());
        }
    } else if (cmd.equalsIgnoreCase("help") || cmd.equalsIgnoreCase("?")) {
        Serial.println("\n================ Serial Controls & Commands ================");
        Serial.println("  [Keyboard Controls]");
        Serial.println("    Up / Left Arrow     : Rotate wheel UP / CCW (continuous repeat)");
        Serial.println("    Down / Right Arrow  : Rotate wheel DOWN / CW (continuous repeat)");
        Serial.println("    Enter (tap)         : Click encoder wheel");
        Serial.println("    Enter (hold >=600ms): Hold encoder wheel (opens main menu)");
        Serial.println("  [Text Fallbacks]");
        Serial.println("    'u' / 'up'          : Rotate UP");
        Serial.println("    'd' / 'down'        : Rotate DOWN");
        Serial.println("    'c' / 'click'       : Click");
        Serial.println("    'h' / 'hold'        : Long press / hold");
        Serial.println("  [System Commands]");
        Serial.println("    'brightness'        : Query current display brightness");
        Serial.println("    'brightness <10-100>': Set display brightness percentage");
        Serial.println("    'compute' / 'perf'  : Print real-time CPU compute & headroom audit");
        Serial.println("    'diagnostics'       : Run complete system & Wi-Fi scan diagnostics");
        Serial.println("============================================================\n");
    } else {
        Serial.printf("[CMD] Unknown command '%s'. Type 'help' for available commands.\n", cmd.c_str());
    }
}

void processSerialInput() {
    static String cmdBuffer = "";
    static EscState escState = ESC_IDLE;
    static uint32_t escStartTime = 0;
    static uint32_t lastCrTime = 0;

    static bool enterHolding = false;
    static uint32_t enterHoldStartTime = 0;
    static uint32_t lastEnterCharTime = 0;
    static bool enterLongPressSent = false;

    while (Serial.available() > 0) {
        char c = (char)Serial.read();

        // 1. ANSI Escape Sequence parsing for Arrow keys
        if (escState == ESC_IDLE) {
            if (c == 0x1B) { // ESC byte
                escState = ESC_SEEN;
                escStartTime = millis();
                continue;
            }
        } else if (escState == ESC_SEEN) {
            if (c == '[' || c == 'O') {
                escState = ESC_BRACKET;
                continue;
            } else {
                escState = ESC_IDLE;
            }
        } else if (escState == ESC_BRACKET) {
            escState = ESC_IDLE;
            if (c == 'A') {
                // UP ARROW -> rotate up (counter-clockwise)
                postWheelEvent(WHEEL_EVENT_ROT_CCW);
                continue;
            } else if (c == 'B') {
                // DOWN ARROW -> rotate down (clockwise)
                postWheelEvent(WHEEL_EVENT_ROT_CW);
                continue;
            } else if (c == 'C') {
                // RIGHT ARROW -> rotate down (clockwise)
                postWheelEvent(WHEEL_EVENT_ROT_CW);
                continue;
            } else if (c == 'D') {
                // LEFT ARROW -> rotate up (counter-clockwise)
                postWheelEvent(WHEEL_EVENT_ROT_CCW);
                continue;
            }
            // If parameter character in extended sequence (e.g. \x1b[1;5A)
            if ((c >= '0' && c <= '9') || c == ';') {
                escState = ESC_BRACKET;
                continue;
            }
            continue;
        }

        // Timeout guard for escape sequences
        if (escState != ESC_IDLE && (millis() - escStartTime > 100)) {
            escState = ESC_IDLE;
        }

        // 2. Carriage Return / Line Feed (Enter key)
        if (c == '\r' || c == '\n') {
            // Deduplicate CRLF sequence (ignore LF immediately following CR)
            if (c == '\n' && (millis() - lastCrTime < 35)) {
                continue;
            }
            if (c == '\r') {
                lastCrTime = millis();
            }

            // If user has typed a command, process the command line
            if (cmdBuffer.length() > 0) {
                String cmd = cmdBuffer;
                cmdBuffer = "";
                cmd.trim();
                Serial.println();
                handleSerialCommand(cmd);
                continue;
            }

            // Otherwise, Enter emulates the encoder push button
            uint32_t now = millis();
            if (!enterHolding) {
                enterHolding = true;
                enterHoldStartTime = now;
                enterLongPressSent = false;
            }
            lastEnterCharTime = now;
            continue;
        }

        // 3. Backspace
        if (c == '\b' || c == 127) {
            if (cmdBuffer.length() > 0) {
                cmdBuffer.remove(cmdBuffer.length() - 1);
                Serial.print("\b \b");
            }
            continue;
        }

        // 4. Accumulate normal printable characters for text commands
        if (c >= 32 && c <= 126) {
            cmdBuffer += c;
            Serial.print(c);
        }
    }

    // 5. Enter hold / release timing detection
    if (enterHolding) {
        uint32_t now = millis();
        // Check for long-press hold (>= 600ms)
        if (!enterLongPressSent && (now - enterHoldStartTime >= 600)) {
            enterLongPressSent = true;
            postWheelEvent(WHEEL_EVENT_LONG_PRESS);
            Serial.println("\n[KEY] ENTER (Held >= 600ms) -> Wheel Long Press");
        }
        // Check if Enter key was released (no repeat char for >= 200ms)
        if (now - lastEnterCharTime >= 200) {
            enterHolding = false;
            if (!enterLongPressSent) {
                postWheelEvent(WHEEL_EVENT_CLICK);
                Serial.println("\n[KEY] ENTER -> Wheel Click");
            }
        }
    }
}

