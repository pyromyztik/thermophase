#pragma once
#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include "encoder.hpp"

// Application UI States
enum AppState {
    APP_STATE_THERMAL_VIEW = 0, // Live sensor view with HUD/OSD overlays (boot default)
    APP_STATE_MAIN_MENU,        // Main menu navigation screen
    APP_STATE_SETTINGS          // Settings & configuration page
};

// Temperature Unit Options
enum TempUnit {
    TEMP_UNIT_CELSIUS = 0,
    TEMP_UNIT_KELVIN
};

// Color Palette Options
enum PaletteType {
    PALETTE_IRONBOW = 0,
    PALETTE_WHITE_HOT,
    PALETTE_RAINBOW
};

// Scaling Mode Options
enum ScalingMode {
    SCALING_BILINEAR = 0,
    SCALING_BICUBIC,
    SCALING_LANCZOS,
    SCALING_TAAU
};


// Minimalist Theme Colors
#define UI_BG_COLOR         ILI9341_BLACK
#define UI_BORDER_COLOR     ILI9341_WHITE
#define UI_TEXT_COLOR       ILI9341_WHITE
#define UI_ACCENT_COLOR     ILI9341_RED      // User-selected Red accent
#define UI_SEPARATOR_COLOR  0x7BEF           // Crisp medium-grey horizontal divider

// Initializes UI state machine variables
void initUI();

// Returns the current UI state
AppState getAppState();

// Transition to a new UI state
void setAppState(AppState newState);

// Handle an encoder input event (rotation or click)
// Returns true if the state or menu selection changed and screen needs redrawing
bool processUIInput(EncoderEventType event);

// Render the active menu/settings screen into the offscreen canvas
void renderCurrentUI(GFXcanvas16* canvas);

// Runtime Settings Accessors
int getCameraRotationSetting();
TempUnit getTempUnitSetting();
PaletteType getPaletteSetting();
bool getMirrorSetting();
ScalingMode getScalingModeSetting();

