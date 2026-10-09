#include "serial.hpp"
#include "config.hpp"
#include "wheel.hpp"
#include "display.hpp"
#include "ui.hpp"
#include "diagnostics.hpp"
#include "ble.hpp"

extern TaskHandle_t diagTaskHandle;

enum EscState {
    ESC_IDLE,
    ESC_SEEN,
    ESC_BRACKET
};

void handleSerialCommand(const String& cmd) {
    if (cmd.equalsIgnoreCase("diagnostics") || cmd.equalsIgnoreCase("diag")) {
        if (diagTaskHandle == NULL) {
            outPrintln("\n[CMD] Spawning System Diagnostics Task...");
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
            outPrintln("\n[WARN] Diagnostics task is already running.");
        }
    } else if (cmd.equalsIgnoreCase("compute") ||
               cmd.equalsIgnoreCase("perf") ||
               cmd.equalsIgnoreCase("headroom") ||
               cmd.equalsIgnoreCase("profile")) {
        printComputeHeadroomReport();
    } else if (cmd.equalsIgnoreCase("up") || cmd.equalsIgnoreCase("u")) {
        postWheelEvent(WHEEL_EVENT_ROT_CCW);
        outPrintln("[SERIAL] Wheel Up (CCW)");
    } else if (cmd.equalsIgnoreCase("down") || cmd.equalsIgnoreCase("d")) {
        postWheelEvent(WHEEL_EVENT_ROT_CW);
        outPrintln("[SERIAL] Wheel Down (CW)");
    } else if (cmd.equalsIgnoreCase("click") || cmd.equalsIgnoreCase("c")) {
        postWheelEvent(WHEEL_EVENT_CLICK);
        outPrintln("[SERIAL] Wheel Click");
    } else if (cmd.equalsIgnoreCase("hold") || cmd.equalsIgnoreCase("h")) {
        postWheelEvent(WHEEL_EVENT_LONG_PRESS);
        outPrintln("[SERIAL] Wheel Long Press (Hold)");
    } else if (cmd.startsWith("brightness") || cmd.startsWith("bright")) {
        int spaceIdx = cmd.indexOf(' ');
        if (spaceIdx > 0) {
            int val = cmd.substring(spaceIdx + 1).toInt();
            if (val >= 10 && val <= 100) {
                setDisplayBrightness((uint8_t)val);
                setDisplayBrightnessSetting((uint8_t)val);
                updateSettingsValues();
                updateBrightnessValues();
                outPrintf("[SERIAL] Display Brightness set to %d%%\n", val);
            } else {
                outPrintln("[SERIAL] Error: Brightness must be between 10 and 100.");
            }
        } else {
            outPrintf("[SERIAL] Current Display Brightness: %d%%\n", getDisplayBrightness());
        }
    } else if (cmd.equalsIgnoreCase("status") || cmd.equalsIgnoreCase("stat")) {
        outPrintf("[STATUS] Spot: %.1f C | Min: %.1f C | Max: %.1f C | Heap: %u KB | Uptime: %lu s\n",
                  displayCenterTemp, displayMinTemp, displayMaxTemp,
                  ESP.getFreeHeap() / 1024, (unsigned long)(millis() / 1000));
    } else if (cmd.equalsIgnoreCase("clear")) {
        outPrintln("[TERMINAL] Cleared");
    } else if (cmd.equalsIgnoreCase("ppt_send") || cmd.equalsIgnoreCase("ppt_transfer")) {
        outPrintln("\n[CMD] Manual PPT Spectral Phasegram BLE Transfer Triggered");
        triggerPPTBLETransfer();
    } else if (cmd.equalsIgnoreCase("help") || cmd.equalsIgnoreCase("?")) {
        outPrintln("\n================ Serial Controls & Commands ================");
        outPrintln("  [Keyboard Controls]");
        outPrintln("    Up / Left Arrow     : Rotate wheel UP / CCW (continuous repeat)");
        outPrintln("    Down / Right Arrow  : Rotate wheel DOWN / CW (continuous repeat)");
        outPrintln("    Enter (tap)         : Click encoder wheel");
        outPrintln("    Enter (hold >=600ms): Hold encoder wheel (opens main menu)");
        outPrintln("  [Text Fallbacks]");
        outPrintln("    'u' / 'up'          : Rotate UP");
        outPrintln("    'd' / 'down'        : Rotate DOWN");
        outPrintln("    'c' / 'click'       : Click");
        outPrintln("    'h' / 'hold'        : Long press / hold");
        outPrintln("  [System Commands]");
        outPrintln("    'brightness'        : Query current display brightness");
        outPrintln("    'brightness <10-100>': Set display brightness percentage");
        outPrintln("    'compute' / 'perf'  : Print real-time CPU compute & headroom audit");
        outPrintln("    'diagnostics'       : Run complete system & Wi-Fi scan diagnostics");
        outPrintln("    'status'            : Query real-time thermal and system status");
        outPrintln("============================================================\n");
    } else {
        outPrintf("[CMD] Unknown command '%s'. Type 'help' for available commands.\n", cmd.c_str());
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

    while (Serial.available() > 0 || bleAvailable() > 0) {
        char c = (Serial.available() > 0) ? (char)Serial.read() : (char)bleRead();

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

