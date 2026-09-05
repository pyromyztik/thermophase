#include "ui.hpp"
#include "config.hpp"
#include "display.hpp"
#include "power.hpp"

// Active application state - starts immediately in Live Thermal View on boot
static AppState currentAppState = APP_STATE_THERMAL_VIEW;

// Runtime setting variables
static int currentRotationSetting = CAMERA_ROTATION;
static TempUnit currentTempUnitSetting = TEMP_UNIT_CELSIUS;
static PaletteType currentPaletteSetting = PALETTE_IRONBOW;
static bool currentMirrorSetting = MIRROR_HORIZONTAL;
static ScalingMode currentScalingSetting = SCALING_BICUBIC;

// Main Menu items
static const char* mainMenuLabels[] = {
    "Live Thermal View",
    "Settings",
    "Power Off"
};
static const int MAIN_MENU_COUNT = sizeof(mainMenuLabels) / sizeof(mainMenuLabels[0]);
static int mainMenuIndex = 0;

// Settings items
static const char* settingsLabels[] = {
    "Rotation",
    "Temp Unit",
    "Color Palette",
    "Mirror Feed",
    "Upscaling",
    "< Back to Main Menu"
};
static const int SETTINGS_COUNT = sizeof(settingsLabels) / sizeof(settingsLabels[0]);
static int settingsIndex = 0;

void initUI() {
    currentAppState = APP_STATE_THERMAL_VIEW;
    mainMenuIndex = 0;
    settingsIndex = 0;
    currentRotationSetting = CAMERA_ROTATION;
    currentTempUnitSetting = TEMP_UNIT_CELSIUS;
    currentPaletteSetting = PALETTE_IRONBOW;
    currentMirrorSetting = MIRROR_HORIZONTAL;
    currentScalingSetting = SCALING_BICUBIC;

    Serial.println("[UI] UI State Machine initialized to APP_STATE_THERMAL_VIEW.");
}

AppState getAppState() {
    return currentAppState;
}
void setAppState(AppState newState) {
    if (currentAppState != newState) {
        currentAppState = newState;
        Serial.printf("[UI] State changed to: %d\n", (int)currentAppState);
    }
}
int getCameraRotationSetting() {
    return currentRotationSetting;
}
TempUnit getTempUnitSetting() {
    return currentTempUnitSetting;
}
PaletteType getPaletteSetting() {
    return currentPaletteSetting;
}
bool getMirrorSetting() {
    return currentMirrorSetting;
}
ScalingMode getScalingModeSetting() {
    return currentScalingSetting;
}


bool processUIInput(EncoderEventType event) {
    if (event == ENCODER_EVENT_NONE) {
        return false;
    }

    switch (currentAppState) {
        case APP_STATE_THERMAL_VIEW:
            // Clicking the HW-040 knob opens the Main Menu
            if (event == ENCODER_EVENT_CLICK) {
                currentAppState = APP_STATE_MAIN_MENU;
                mainMenuIndex = 0;
                Serial.println("[UI] Click detected: Transitioning to APP_STATE_MAIN_MENU");
                return true;
            }
            break;

        case APP_STATE_MAIN_MENU:
            if (event == ENCODER_EVENT_ROT_CW) {
                mainMenuIndex = (mainMenuIndex + 1) % MAIN_MENU_COUNT;
                return true;
            } else if (event == ENCODER_EVENT_ROT_CCW) {
                mainMenuIndex = (mainMenuIndex - 1 + MAIN_MENU_COUNT) % MAIN_MENU_COUNT;
                return true;
            } else if (event == ENCODER_EVENT_CLICK) {
                if (mainMenuIndex == 0) {
                    // Return to Live Thermal View
                    currentAppState = APP_STATE_THERMAL_VIEW;
                    Serial.println("[UI] Menu selected: Returning to APP_STATE_THERMAL_VIEW");
                } else if (mainMenuIndex == 1) {
                    // Navigate to Settings
                    currentAppState = APP_STATE_SETTINGS;
                    settingsIndex = 0;
                    Serial.println("[UI] Menu selected: Transitioning to APP_STATE_SETTINGS");
                } else if (mainMenuIndex == 2) {
                    // Enter Deep Sleep / Power Off
                    Serial.println("[UI] Menu selected: Powering Off Device (Deep Sleep)...");
                    enterDeepSleep();
                }
                return true;
            }
            break;

        case APP_STATE_SETTINGS:
            if (event == ENCODER_EVENT_ROT_CW) {
                settingsIndex = (settingsIndex + 1) % SETTINGS_COUNT;
                return true;
            } else if (event == ENCODER_EVENT_ROT_CCW) {
                settingsIndex = (settingsIndex - 1 + SETTINGS_COUNT) % SETTINGS_COUNT;
                return true;
            } else if (event == ENCODER_EVENT_CLICK) {
                switch (settingsIndex) {
                    case 0: // Rotation: 0 -> 90 -> 180 -> 270 -> 0
                        currentRotationSetting = (currentRotationSetting + 1) % 4;
                        updateCameraRotation(currentRotationSetting);
                        Serial.printf("[UI] Rotation updated to: %d deg\n", currentRotationSetting * 90);
                        break;

                    case 1: // Temp Unit: Celsius <-> Kelvin
                        currentTempUnitSetting = (currentTempUnitSetting == TEMP_UNIT_CELSIUS) 
                                               ? TEMP_UNIT_KELVIN : TEMP_UNIT_CELSIUS;
                        updateTempUnit(currentTempUnitSetting);
                        Serial.printf("[UI] Temp unit updated to: %s\n", 
                                      (currentTempUnitSetting == TEMP_UNIT_CELSIUS) ? "Celsius" : "Kelvin");
                        break;

                    case 2: // Color Palette: Ironbow -> White-Hot -> Rainbow -> Ironbow
                        currentPaletteSetting = (PaletteType)((currentPaletteSetting + 1) % 3);
                        updatePalette(currentPaletteSetting);
                        Serial.printf("[UI] Palette updated to: %d\n", (int)currentPaletteSetting);
                        break;

                    case 3: // Mirror Feed: OFF <-> ON
                        currentMirrorSetting = !currentMirrorSetting;
                        updateMirror(currentMirrorSetting);
                        Serial.printf("[UI] Mirror updated to: %s\n", currentMirrorSetting ? "ON" : "OFF");
                        break;

                    case 4: // Upscaling: Bilinear -> Bicubic -> Lanczos -> TAAU
                        currentScalingSetting = (ScalingMode)((currentScalingSetting + 1) % 4);
                        updateScalingMode(currentScalingSetting);
                        Serial.printf("[UI] Scaling updated to mode %d\n", (int)currentScalingSetting);
                        break;


                    case 5: // < Back to Main Menu
                        currentAppState = APP_STATE_MAIN_MENU;
                        Serial.println("[UI] Settings: Returning to APP_STATE_MAIN_MENU");
                        break;
                }
                return true;
            }
            break;
    }
    return false;
}

static void drawHeader(GFXcanvas16* canvas, const char* title) {
    // Title text at top
    canvas->setTextColor(UI_TEXT_COLOR);
    canvas->setTextSize(2);
    canvas->setCursor(14, 9);
    canvas->print(title);

    // Grey separator line underneath
    canvas->drawFastHLine(0, 32, DISPLAY_WIDTH, UI_SEPARATOR_COLOR);
}
static void drawFooter(GFXcanvas16* canvas, const char* hints) {
    // Grey separator line above hints
    canvas->drawFastHLine(0, 216, DISPLAY_WIDTH, UI_SEPARATOR_COLOR);

    // Centered hint text
    canvas->setTextColor(UI_TEXT_COLOR);
    canvas->setTextSize(1);
    canvas->setCursor(14, 224);
    canvas->print(hints);
}

void renderCurrentUI(GFXcanvas16* canvas) {
    if (canvas == nullptr) return;

    // Pure black background
    canvas->fillScreen(UI_BG_COLOR);

    if (currentAppState == APP_STATE_MAIN_MENU) {
        drawHeader(canvas, "MAIN MENU");

        int itemWidth = 260;
        int itemHeight = 40;
        int itemX = 30;

        int yOffsets[3] = {48, 100, 152};

        for (int i = 0; i < MAIN_MENU_COUNT; ++i) {
            int y = yOffsets[i];
            bool isSelected = (i == mainMenuIndex);

            if (isSelected) {
                // Unfilled double-line Red accent border
                canvas->drawRect(itemX, y, itemWidth, itemHeight, UI_ACCENT_COLOR);
                canvas->drawRect(itemX + 1, y + 1, itemWidth - 2, itemHeight - 2, UI_ACCENT_COLOR);

                // Red selection arrow
                canvas->setTextColor(UI_ACCENT_COLOR);
                canvas->setTextSize(2);
                canvas->setCursor(itemX + 12, y + 12);
                canvas->print(">");

                // White text
                canvas->setTextColor(UI_TEXT_COLOR);
                canvas->setCursor(itemX + 32, y + 12);
                canvas->print(mainMenuLabels[i]);
            } else {
                // Unfilled single-line White border
                canvas->drawRect(itemX, y, itemWidth, itemHeight, UI_BORDER_COLOR);

                // White text
                canvas->setTextColor(UI_TEXT_COLOR);
                canvas->setTextSize(2);
                canvas->setCursor(itemX + 32, y + 12);
                canvas->print(mainMenuLabels[i]);
            }
        }
        drawFooter(canvas, "[Rotate] Navigate   |   [Click] Select");

    } else if (currentAppState == APP_STATE_SETTINGS) {
        drawHeader(canvas, "SETTINGS");

        const int ITEMS_PER_PAGE = 5;
        int totalPages = (SETTINGS_COUNT + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
        int currentPage = settingsIndex / ITEMS_PER_PAGE;
        int pageStartIndex = currentPage * ITEMS_PER_PAGE;
        int pageEndIndex = (pageStartIndex + ITEMS_PER_PAGE < SETTINGS_COUNT) 
                         ? (pageStartIndex + ITEMS_PER_PAGE) : SETTINGS_COUNT;
        int itemsOnPage = pageEndIndex - pageStartIndex;

        int startY = 42;
        int itemHeight = 28;
        int itemSpacing = 6;
        int itemWidth = 268;
        int itemX = 18;

        // Formatted value strings for settings
        const char* rotStrs[4] = {"0 deg", "90 deg", "180 deg", "270 deg"};
        const char* unitStrs[2] = {"Celsius (C)", "Kelvin (K)"};
        const char* palStrs[3] = {"Ironbow", "White-Hot", "Rainbow"};
        const char* mirStrs[2] = {"OFF", "ON"};
        const char* scaleStrs[4] = {"Bilinear", "Bicubic", "Lanczos", "TAAU"};

        const char* valStrings[SETTINGS_COUNT] = {
            rotStrs[currentRotationSetting % 4],
            unitStrs[currentTempUnitSetting % 2],
            palStrs[currentPaletteSetting % 3],
            mirStrs[currentMirrorSetting ? 1 : 0],
            scaleStrs[currentScalingSetting % 4],
            nullptr // Back option has no value column
        };

        for (int slot = 0; slot < itemsOnPage; ++slot) {
            int i = pageStartIndex + slot;
            int y = startY + slot * (itemHeight + itemSpacing);
            bool isSelected = (i == settingsIndex);
            int textY = y + 10;

            if (isSelected) {
                // Unfilled double-line Red accent border
                canvas->drawRect(itemX, y, itemWidth, itemHeight, UI_ACCENT_COLOR);
                canvas->drawRect(itemX + 1, y + 1, itemWidth - 2, itemHeight - 2, UI_ACCENT_COLOR);

                // Red selection arrow
                canvas->setTextColor(UI_ACCENT_COLOR);
                canvas->setTextSize(1);
                canvas->setCursor(itemX + 8, textY);
                canvas->print(">");

                // White label
                canvas->setTextColor(UI_TEXT_COLOR);
                canvas->setCursor(itemX + 22, textY);
                canvas->print(settingsLabels[i]);

                // Red value text
                if (valStrings[i] != nullptr) {
                    canvas->setTextColor(UI_ACCENT_COLOR);
                    canvas->setCursor(itemX + 155, textY);
                    canvas->print(valStrings[i]);
                }
            } else {
                // Unfilled single-line White border
                canvas->drawRect(itemX, y, itemWidth, itemHeight, UI_BORDER_COLOR);

                // White label
                canvas->setTextColor(UI_TEXT_COLOR);
                canvas->setTextSize(1);
                canvas->setCursor(itemX + 22, textY);
                canvas->print(settingsLabels[i]);

                // Dim white value text
                if (valStrings[i] != nullptr) {
                    canvas->setTextColor(0xBDF7); // Soft light-grey
                    canvas->setCursor(itemX + 155, textY);
                    canvas->print(valStrings[i]);
                }
            }
        }

        // Draw side page indicator squares on the right (filled for current page, hollow for others)
        if (totalPages > 1) {
            int sqX = 298;
            int sqSize = 8;
            int sqGap = 6;
            int totalIndHeight = totalPages * sqSize + (totalPages - 1) * sqGap;
            int indStartY = 124 - (totalIndHeight / 2);

            for (int p = 0; p < totalPages; ++p) {
                int sqY = indStartY + p * (sqSize + sqGap);
                if (p == currentPage) {
                    // Active page: filled square with accent color
                    canvas->fillRect(sqX, sqY, sqSize, sqSize, UI_ACCENT_COLOR);
                    canvas->drawRect(sqX, sqY, sqSize, sqSize, UI_TEXT_COLOR);
                } else {
                    // Inactive page: unfilled square
                    canvas->drawRect(sqX, sqY, sqSize, sqSize, UI_SEPARATOR_COLOR);
                }
            }
        }

        drawFooter(canvas, "[Rotate] Navigate   |   [Click] Change / Back");
    }
}

