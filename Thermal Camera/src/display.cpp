#include "display.hpp"
#include "upscaler.hpp"

// Hardware SPI TFT instance
Adafruit_ILI9341 tft = Adafruit_ILI9341(&SPI, TFT_DC, TFT_CS, TFT_RST);

// Double-buffer canvas allocated in PSRAM/heap during initDisplay()
static GFXcanvas16* canvas = nullptr;

// Multi-mode thermal upscaling engine (Bilinear, Bicubic, Lanczos, TAAU)
static ThermalUpscaler upscaler;


// Bilinear interpolation precomputed tables
static ColInterp colTable[DISPLAY_WIDTH];
static RowInterp rowTable[DISPLAY_HEIGHT];

// Viewport and rotated sensor dimensions
static int vpX = 0;
static int vpY = 0;
static int vpW = DISPLAY_WIDTH;
static int vpH = DISPLAY_HEIGHT;
static int gridW = SENSOR_WIDTH;
static int gridH = SENSOR_HEIGHT;

// Rotated thermal sensor temperature buffer
static float rotatedGrid[SENSOR_PIXELS];

// Runtime configurable settings
static int currentCameraRotation = CAMERA_ROTATION;
static bool currentMirrorFeed = MIRROR_HORIZONTAL;
static TempUnit currentTempUnit = TEMP_UNIT_CELSIUS;
static PaletteType currentPaletteType = PALETTE_IRONBOW;
static ScalingMode currentScalingMode = SCALING_BICUBIC;


// 256-color thermal palettes
static uint16_t ironbowPalette[256];
static uint16_t whiteHotPalette[256];
static uint16_t rainbowPalette[256];
static const uint16_t* activePalette = ironbowPalette;

// Smoothing variables for auto-ranging
static float smoothedMin = 20.0f;
static float smoothedMax = 35.0f;

// Rotate the raw 32x24 sensor array to the specified orientation and mirror mode (always horizontal on-screen flip)
static void rotateSensorFrame(const float* src, float* dst, int rotation, bool mirror) {
    if (rotation == 0) {
        // 0 degrees (32 cols x 24 rows)
        for (int r = 0; r < 24; ++r) {
            for (int c = 0; c < 32; ++c) {
                int screen_c = mirror ? (31 - c) : c;
                dst[r * 32 + c] = src[r * 32 + screen_c];
            }
        }
    } else if (rotation == 1) {
        // 90 degrees clockwise (24 cols x 32 rows)
        for (int r = 0; r < 32; ++r) {
            for (int c = 0; c < 24; ++c) {
                int screen_c = mirror ? (23 - c) : c;
                int orig_r = 23 - screen_c;
                int orig_c = r;
                dst[r * 24 + c] = src[orig_r * 32 + orig_c];
            }
        }
    } else if (rotation == 2) {
        // 180 degrees (upside down, 32 cols x 24 rows)
        for (int r = 0; r < 24; ++r) {
            for (int c = 0; c < 32; ++c) {
                int screen_c = mirror ? (31 - c) : c;
                int orig_r = 23 - r;
                int orig_c = 31 - screen_c;
                dst[r * 32 + c] = src[orig_r * 32 + orig_c];
            }
        }
    } else if (rotation == 3) {
        // 270 degrees clockwise (24 cols x 32 rows)
        for (int r = 0; r < 32; ++r) {
            for (int c = 0; c < 24; ++c) {
                int screen_c = mirror ? (23 - c) : c;
                int orig_r = screen_c;
                int orig_c = 31 - r;
                dst[r * 24 + c] = src[orig_r * 32 + orig_c];
            }
        }
    }
}

// Dynamically compute bilinear interpolation mapping coefficients for any orientation
static void recomputeBilinearTables(int rotation) {
    currentCameraRotation = rotation;
    if (rotation == 1 || rotation == 3) {
        gridW = SENSOR_HEIGHT; // 24
        gridH = SENSOR_WIDTH;  // 32
        #if STRETCH_ROTATED_IMAGE
        vpW = DISPLAY_WIDTH;
        vpH = DISPLAY_HEIGHT;
        vpX = 0;
        vpY = 0;
        #else
        // Maintain true 3:4 aspect ratio (180x240 on 320x240 display)
        vpH = DISPLAY_HEIGHT;                 // 240
        vpW = (vpH * gridW) / gridH;          // (240 * 24) / 32 = 180
        vpX = (DISPLAY_WIDTH - vpW) / 2;      // 70 px left margin
        vpY = 0;
        #endif
    } else {
        gridW = SENSOR_WIDTH;  // 32
        gridH = SENSOR_HEIGHT; // 24
        vpW = DISPLAY_WIDTH;   // 320
        vpH = DISPLAY_HEIGHT;  // 240
        vpX = 0;
        vpY = 0;
    }

    for (int x = 0; x < vpW; ++x) {
        float u = (float)x * (float)(gridW - 1) / (float)(vpW - 1);
        uint8_t x0 = (uint8_t)u;
        uint8_t x1 = (x0 < gridW - 1) ? (x0 + 1) : (gridW - 1);
        colTable[x].x0 = x0;
        colTable[x].x1 = x1;
        colTable[x].fx = u - (float)x0;
    }

    for (int y = 0; y < vpH; ++y) {
        float v = (float)y * (float)(gridH - 1) / (float)(vpH - 1);
        uint8_t y0 = (uint8_t)v;
        uint8_t y1 = (y0 < gridH - 1) ? (y0 + 1) : (gridH - 1);
        rowTable[y].y0 = y0;
        rowTable[y].y1 = y1;
        rowTable[y].fy = v - (float)y0;
    }

    if (canvas != nullptr) {
        canvas->fillScreen(ILI9341_BLACK);
    }

    // Reconfigure multi-mode upscaler engine viewport
    upscaler.reconfigureViewport(vpW, vpH, gridW, gridH);

    Serial.printf("[TFT] Recomputed Viewport: %dx%d at (%d,%d), Grid: %dx%d, Rotation: %d\n",
                  vpW, vpH, vpX, vpY, gridW, gridH, currentCameraRotation);
}



// Generate Ironbow (FLIR-style) 256-color RGB565 palette
static void initIronbowPalette() {
    struct ColorPoint {
        float pos;
        uint8_t r, g, b;
    };

    const ColorPoint keyColors[] = {
        {0.00f, 0,   0,   32},   // Deep blue / dark purple
        {0.20f, 80,  0,   130},  // Purple
        {0.45f, 210, 20,  20},   // Deep red
        {0.70f, 255, 140, 0},    // Bright orange
        {0.88f, 255, 235, 30},   // Yellow
        {1.00f, 255, 255, 255}   // White
    };
    const size_t numKeys = sizeof(keyColors) / sizeof(keyColors[0]);

    for (int i = 0; i < 256; ++i) {
        float pos = (float)i / 255.0f;
        uint8_t r = 0, g = 0, b = 0;

        for (size_t k = 0; k < numKeys - 1; ++k) {
            if (pos >= keyColors[k].pos && pos <= keyColors[k + 1].pos) {
                float segmentSpan = keyColors[k + 1].pos - keyColors[k].pos;
                float localRatio = (pos - keyColors[k].pos) / segmentSpan;

                r = (uint8_t)(keyColors[k].r + localRatio 
                           * (keyColors[k + 1].r - keyColors[k].r));
                g = (uint8_t)(keyColors[k].g + localRatio 
                           * (keyColors[k + 1].g - keyColors[k].g));
                b = (uint8_t)(keyColors[k].b + localRatio 
                           * (keyColors[k + 1].b - keyColors[k].b));
                break;
            }
        }
        ironbowPalette[i] = tft.color565(r, g, b);
    }
}

// Generate White-Hot monochrome (grayscale) 256-color palette
static void initWhiteHotPalette() {
    for (int i = 0; i < 256; ++i) {
        whiteHotPalette[i] = tft.color565((uint8_t)i, (uint8_t)i, (uint8_t)i);
    }
}

// Generate classic 7-color spectral Rainbow palette
static void initRainbowPalette() {
    struct ColorPoint {
        float pos;
        uint8_t r, g, b;
    };

    const ColorPoint keyColors[] = {
        {0.00f, 0,   0,   255},  // Blue
        {0.25f, 0,   255, 255},  // Cyan
        {0.50f, 0,   255, 0},    // Green
        {0.75f, 255, 255, 0},    // Yellow
        {1.00f, 255, 0,   0}     // Red
    };
    const size_t numKeys = sizeof(keyColors) / sizeof(keyColors[0]);

    for (int i = 0; i < 256; ++i) {
        float pos = (float)i / 255.0f;
        uint8_t r = 0, g = 0, b = 0;

        for (size_t k = 0; k < numKeys - 1; ++k) {
            if (pos >= keyColors[k].pos && pos <= keyColors[k + 1].pos) {
                float segmentSpan = keyColors[k + 1].pos - keyColors[k].pos;
                float localRatio = (pos - keyColors[k].pos) / segmentSpan;

                r = (uint8_t)(keyColors[k].r + localRatio 
                           * (keyColors[k + 1].r - keyColors[k].r));
                g = (uint8_t)(keyColors[k].g + localRatio 
                           * (keyColors[k + 1].g - keyColors[k].g));
                b = (uint8_t)(keyColors[k].b + localRatio 
                           * (keyColors[k + 1].b - keyColors[k].b));
                break;
            }
        }
        rainbowPalette[i] = tft.color565(r, g, b);
    }
}

static void initAllPalettes() {
    initIronbowPalette();
    initWhiteHotPalette();
    initRainbowPalette();
    activePalette = ironbowPalette;
}

// Temperature unit conversion helpers
static float convertTemp(float tempC, TempUnit unit) {
    if (unit == TEMP_UNIT_KELVIN) {
        return tempC + 273.15f;
    }
    return tempC;
}
static const char* getTempUnitSymbol(TempUnit unit) {
    return (unit == TEMP_UNIT_KELVIN) ? "K" : "C";
}
static const char* getTempUnitName(TempUnit unit) {
    return (unit == TEMP_UNIT_KELVIN) ? "Kelvin" : "Celsius";
}

// Runtime display settings mutators
void updateCameraRotation(int rotation) {
    recomputeBilinearTables(rotation);
}
void updateTempUnit(TempUnit unit) {
    currentTempUnit = unit;
}
void updatePalette(PaletteType palette) {
    currentPaletteType = palette;
    if (palette == PALETTE_WHITE_HOT) {
        activePalette = whiteHotPalette;
    } else if (palette == PALETTE_RAINBOW) {
        activePalette = rainbowPalette;
    } else {
        activePalette = ironbowPalette;
    }
}
void updateMirror(bool mirror) {
    currentMirrorFeed = mirror;
    upscaler.reset();
}
void updateScalingMode(ScalingMode mode) {
    currentScalingMode = mode;
    upscaler.reset();
    const char* modeNames[4] = {"Bilinear", "Bicubic", "Lanczos", "TAAU"};
    Serial.printf("[TFT] Scaling mode updated to: %s\n", modeNames[mode % 4]);
}
ScalingMode getScalingMode() {
    return currentScalingMode;
}



// Live telemetry variables for continuous serial reporting
volatile float displayFps = 0.0f;
volatile float displayCenterTemp = 0.0f;
volatile float displayMinTemp = 0.0f;
volatile float displayMaxTemp = 0.0f;

bool checkDisplayConnected() {
#if CHECK_TFT_MISO
    // Ensure MISO pin has an active pulldown so a disconnected/floating wire reads 0x00
    pinMode(TFT_MISO, INPUT_PULLDOWN);

    // ILI9341 register read operations require SPI frequency <= 6.6 MHz (datasheet t_CONV=150ns).
    // Temporarily switch SPI bus to safe 2 MHz read speed:
    tft.setSPISpeed(2000000);

    uint8_t powerMode = tft.readcommand8(ILI9341_RDMODE);
    uint8_t madctl    = tft.readcommand8(ILI9341_RDMADCTL);
    uint8_t pixFmt    = tft.readcommand8(ILI9341_RDPIXFMT);

    // Restore full 40 MHz SPI clock for high-speed writes
    tft.setSPISpeed(TFT_SPI_FREQ);

    // A disconnected display (or floating/grounded pin) reads 0x00 (due to pulldown) or 0xFF.
    // A connected ILI9341 returns non-zero, non-0xFF values.
    bool connected = (powerMode != 0x00 && powerMode != 0xFF) ||
                     (madctl != 0x00 && madctl != 0xFF) ||
                     (pixFmt != 0x00 && pixFmt != 0xFF);
    return connected;
#else
    return true;
#endif
}

void showBootScreen(const char* statusText, uint8_t progressPercent) {
    if (canvas == nullptr) return;

    // 1. Pure black background
    canvas->fillScreen(ILI9341_BLACK);

    // 2. Stylized Thermal Aperture / Reticle Icon at (160, 60)
    int iconX = DISPLAY_WIDTH / 2;
    int iconY = 60;
    canvas->drawCircle(iconX, iconY, 18, 0x52AA);          // Outer ring (soft grey)
    canvas->drawCircle(iconX, iconY, 13, UI_ACCENT_COLOR); // Mid ring (accent red)
    canvas->drawCircle(iconX, iconY, 12, UI_ACCENT_COLOR);
    canvas->fillCircle(iconX, iconY, 5, ILI9341_YELLOW);   // Core (yellow spot)
    canvas->fillCircle(iconX, iconY, 2, ILI9341_WHITE);    // Center hot core
    // Crosshair ticks
    canvas->drawFastHLine(iconX - 25, iconY, 6, UI_ACCENT_COLOR);
    canvas->drawFastHLine(iconX + 20, iconY, 6, UI_ACCENT_COLOR);
    canvas->drawFastVLine(iconX, iconY - 25, 6, UI_ACCENT_COLOR);
    canvas->drawFastVLine(iconX, iconY + 20, 6, UI_ACCENT_COLOR);

    // 3. Main Title & Subtitle
    canvas->setTextColor(UI_TEXT_COLOR);
    canvas->setTextSize(2);
    canvas->setCursor(100, 90);
    canvas->print("THERMOPHASE");
    canvas->setTextSize(1);
    canvas->setTextColor(0xBDF7); // Sleek silver
    canvas->setCursor(88, 112);
    canvas->print("REAL-TIME THERMAL IMAGER");

    // 4. Progress Bar
    int pBarX = 50;
    int pBarY = 178;
    int pBarW = 220;
    int pBarH = 6;
    canvas->drawRect(pBarX - 1, pBarY - 1, pBarW + 2, pBarH + 2, 0x7BEF);
    if (progressPercent > 100) progressPercent = 100;
    int fillW = (progressPercent * pBarW) / 100;
    if (fillW > 0) {
        canvas->fillRect(pBarX, pBarY, fillW, pBarH, UI_ACCENT_COLOR);
    }

    // 5. Status message below progress bar
    canvas->setTextColor(ILI9341_LIGHTGREY);
    canvas->setTextSize(1);
    canvas->setCursor(pBarX, 194);
    canvas->print(statusText != nullptr ? statusText : "Initializing...");

    // Percentage on right side
    char pctStr[8];
    snprintf(pctStr, sizeof(pctStr), "%u%%", progressPercent);
    canvas->setTextColor(0xBDF7);
    canvas->setCursor(pBarX + pBarW - 24, 194);
    canvas->print(pctStr);

    // 6. Push composite boot frame to ILI9341 in ONE SPI burst
    tft.startWrite();
    tft.setAddrWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    tft.writePixels(canvas->getBuffer(), DISPLAY_WIDTH * DISPLAY_HEIGHT);
    tft.endWrite();

    // 7. Turn on backlight - no white flashes this way!
    digitalWrite(TFT_LED, HIGH);
}

bool initDisplay() {
    Serial.println("[TFT] Initializing display pins and SPI bus...");

    // Backlight control - KEEP OFF INITIALLY to avoid white screen!
    pinMode(TFT_LED, OUTPUT);
    digitalWrite(TFT_LED, LOW);

    // Hardware reset sequence
    pinMode(TFT_RST, OUTPUT);
    digitalWrite(TFT_RST, HIGH);
    delay(50);
    digitalWrite(TFT_RST, LOW);
    delay(100);
    digitalWrite(TFT_RST, HIGH);
    delay(200);

    // Ensure MISO pin has internal pulldown active
    pinMode(TFT_MISO, INPUT_PULLDOWN);

    // Initialize custom hardware SPI pins on ESP32-S3
    SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, -1);

    // Initialize ILI9341 display
    tft.begin(TFT_SPI_FREQ);
    tft.setRotation(DISPLAY_ROTATION);
    tft.fillScreen(ILI9341_BLACK);

#if CHECK_TFT_MISO
    // Temporarily switch to 2 MHz safe SPI clock for register diagnostics
    tft.setSPISpeed(2000000);
    uint8_t powerMode = tft.readcommand8(ILI9341_RDMODE);
    uint8_t madctl    = tft.readcommand8(ILI9341_RDMADCTL);
    uint8_t pixFmt    = tft.readcommand8(ILI9341_RDPIXFMT);
    uint8_t selfDiag  = tft.readcommand8(ILI9341_RDSELFDIAG);
    tft.setSPISpeed(TFT_SPI_FREQ);

    Serial.println("\n--------------------------------------------------");
    Serial.println("  ILI9341 TFT Display Diagnostic Register Check   ");
    Serial.println("--------------------------------------------------");
    Serial.printf("  Display Power Mode: 0x%02X\n", powerMode);
    Serial.printf("  MADCTL Mode:        0x%02X\n", madctl);
    Serial.printf("  Pixel Format:       0x%02X\n", pixFmt);
    Serial.printf("  Self Diagnostic:    0x%02X\n", selfDiag);
    Serial.println("--------------------------------------------------");

    bool displayOk = (powerMode != 0x00 && powerMode != 0xFF) ||
                     (madctl != 0x00 && madctl != 0xFF) ||
                     (pixFmt != 0x00 && pixFmt != 0xFF);

    if (!displayOk) {
        globalDeviceStatus = STATUS_DISPLAY_ERROR;
        Serial.println("\n========================================================");
        Serial.println(" [ERROR] TFT Display not responding via SPI MISO!");
        Serial.println(" All diagnostic registers returned 0x00 or 0xFF.");
        Serial.println(" Check hardware wiring:");
        Serial.printf("   - CS: GPIO %d | DC: GPIO %d | RST: GPIO %d\n", TFT_CS, TFT_DC, TFT_RST);
        Serial.printf("   - MOSI: GPIO %d (SDI) | SCK: GPIO %d | MISO: GPIO %d (SDO)\n", TFT_MOSI, TFT_SCK, TFT_MISO);
        Serial.printf("   - VCC: 5V/3.3V | GND: Common Ground\n");
        Serial.println("========================================================\n");
    } else {
        globalDeviceStatus = STATUS_OK;
        Serial.println("[TFT] Display communication successfully verified via MISO!");
    }
#else
    // Standard write-only mode: Mark OK unless memory allocation fails
    globalDeviceStatus = STATUS_OK;
    Serial.println("[TFT] Standard SPI write mode (CHECK_TFT_MISO=false).");
#endif

    // Allocate offscreen double-buffer canvas in PSRAM/heap
    canvas = new GFXcanvas16(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (canvas == nullptr || canvas->getBuffer() == nullptr) {
        Serial.println("[TFT] ERROR: Failed to allocate GFXcanvas16 frame buffer!");
        return false;
    }
    Serial.printf("[TFT] Double-buffer canvas allocated: %u bytes in RAM\n",
                  DISPLAY_WIDTH * DISPLAY_HEIGHT * 2);

    canvas->fillScreen(ILI9341_BLACK);

    // Initialize multi-mode upscaler engine (Bilinear, Bicubic, Lanczos, TAAU)
    if (!upscaler.init(DISPLAY_WIDTH, DISPLAY_HEIGHT)) {
        Serial.println("[TFT] Warning: Failed to allocate full Upscaler PSRAM buffers!");
    }

    // Precompute bilinear scaling tables and initialize all thermal palettes

    recomputeBilinearTables(currentCameraRotation);
    initAllPalettes();


    // Display initial Boot Screen and turn on backlight safely
    showBootScreen("Starting System...", 15);

    Serial.println("[TFT] ILI9341 display initialized successfully!");
    return (globalDeviceStatus == STATUS_OK);
}

static void renderSensorErrorScreen(uint32_t attempt) {
    if (canvas == nullptr) return;

    canvas->fillScreen(ILI9341_BLACK);

    // Red warning border
    canvas->drawRect(4, 4, DISPLAY_WIDTH - 8, DISPLAY_HEIGHT - 8, ILI9341_RED);
    canvas->drawRect(6, 6, DISPLAY_WIDTH - 12, DISPLAY_HEIGHT - 12, ILI9341_RED);

    // Title banner
    canvas->fillRect(12, 12, DISPLAY_WIDTH - 24, 28, ILI9341_RED);
    canvas->setTextColor(ILI9341_WHITE);
    canvas->setTextSize(2);
    canvas->setCursor(20, 18);
    canvas->print("THERMAL SENSOR ERROR");

    // Subtitle
    canvas->setTextSize(1);
    canvas->setTextColor(ILI9341_YELLOW);
    canvas->setCursor(18, 48);
    canvas->println("MLX90640 not responding on I2C bus (Addr 0x33)");

    // Pin checklist
    canvas->setTextColor(ILI9341_WHITE);
    canvas->setCursor(18, 68);
    canvas->println("CHECK HARDWARE CONNECTIONS:");
    canvas->setTextColor(ILI9341_LIGHTGREY);
    canvas->setCursor(24, 84);
    canvas->printf("  - SDA -> GPIO %d\n", PIN_I2C_SDA);
    canvas->setCursor(24, 98);
    canvas->printf("  - SCL -> GPIO %d\n", PIN_I2C_SCL);
    canvas->setCursor(24, 112);
    canvas->printf("  - VIN -> 3.3V (Do NOT connect to 5V!)");
    canvas->setCursor(24, 126);
    canvas->printf("  - GND -> GND  (Common ground with ESP32)");

    // Auto reconnect status
    canvas->setTextColor(ILI9341_GREENYELLOW);
    canvas->setCursor(18, 150);
    canvas->printf("Auto-reconnecting... (Attempt #%u)\n", attempt);

    // LED Status hint
    canvas->setTextColor(ILI9341_WHITE);
    canvas->setCursor(18, 172);
    canvas->println("STATUS INDICATOR (On-board RGB LED):");
    canvas->setTextColor(ILI9341_LIGHTGREY);
    canvas->setCursor(24, 187);
    canvas->println("  * Flashing Amber = Sensor disconnected");
    canvas->setCursor(24, 201);
    canvas->println("  * Blinking Red   = Display SPI disconnected");
    canvas->setCursor(24, 215);
    canvas->println("  * Rainbow Cycle  = All Systems Live");

    // Push canvas to screen
    tft.startWrite();
    tft.setAddrWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    tft.writePixels(canvas->getBuffer(), DISPLAY_WIDTH * DISPLAY_HEIGHT);
    tft.endWrite();
}

void displayTask(void *pvParameters) {
    Serial.println("[TASK] TFT Display & Scaling Task started on Core " 
        + String(xPortGetCoreID()));

    ThermalFrame currentFrame;
    uint32_t lastFpsCalc = millis();
    uint32_t frameCount = 0;
    float currentFps = 0.0f;
    uint32_t lastDisplayPoll = 0;
    int displayFailCount = 0;
    bool menuNeedsRedraw = true;
    AppState prevAppState = getAppState();

    while (true) {
        uint32_t now = millis();

        // 1. Dynamic Display Connection Monitoring (checks every 1000 ms)
        #if CHECK_TFT_MISO
        if (now - lastDisplayPoll >= 1000) {
            lastDisplayPoll = now;
            bool connected = checkDisplayConnected();

            if (!connected) {
                displayFailCount++;
                if (displayFailCount >= 2) {
                    if (globalDeviceStatus != STATUS_DISPLAY_ERROR) {
                        Serial.println("\n[TFT] ERROR: Display connection lost or disconnected!");
                    }
                    globalDeviceStatus = STATUS_DISPLAY_ERROR;
                }
            } else {
                if (globalDeviceStatus == STATUS_DISPLAY_ERROR) {
                    Serial.println("\n[TFT] SUCCESS: Display reconnected! Re-initializing ILI9341...");
                    tft.begin(TFT_SPI_FREQ);
                    tft.setRotation(DISPLAY_ROTATION);
                    tft.fillScreen(ILI9341_BLACK);
                    menuNeedsRedraw = true;
                }
                displayFailCount = 0;
                globalDeviceStatus = STATUS_OK;
            }
        }
        #endif

        // If display is disconnected, pause and wait for reconnect
        if (globalDeviceStatus != STATUS_OK) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // 2. Poll rotary encoder events and update UI state
        EncoderEventType evt;
        bool uiChanged = false;
        while (getEncoderEvent(evt, 0)) {
            if (processUIInput(evt)) {
                uiChanged = true;
            }
        }
        AppState currentAppState = getAppState();
        if (currentAppState != prevAppState) {
            menuNeedsRedraw = true;
            prevAppState = currentAppState;
        }

        // 3. Render according to active UI State
        if (currentAppState != APP_STATE_THERMAL_VIEW) {
            // Render Main Menu or Settings page
            if (uiChanged || menuNeedsRedraw) {
                renderCurrentUI(canvas);
                tft.startWrite();
                tft.setAddrWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
                tft.writePixels(canvas->getBuffer(), DISPLAY_WIDTH * DISPLAY_HEIGHT);
                tft.endWrite();
                menuNeedsRedraw = false;
            }
            // Wait for next encoder event or periodic check (timeout 50ms)
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
            continue;
        }

        // --- APP_STATE_THERMAL_VIEW (Live Sensor View) ---
        // If sensor is disconnected or failed, show on-screen error card
        if (globalSensorStatus != SENSOR_OK) {
            renderSensorErrorScreen(sensorReconnectAttempts);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        // Wait for frame notification from sensor task (or wake-up from encoder)
        uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50));
        // If no notif received within timeout, loop to check encoder / connection
        if (notified == 0) 
            continue;

        // Fetch latest frame from thread-safe double buffer
        if (!getLatestThermalFrame(currentFrame)) 
            continue;

        // Rotate thermal frame to configured orientation and mirror settings
        rotateSensorFrame(currentFrame.temperatures, rotatedGrid, currentCameraRotation, currentMirrorFeed);

        // Initialize auto-range immediately to actual scene min/max on first frame
        static bool autoRangeInitialized = false;
        if (!autoRangeInitialized) {
            smoothedMin = currentFrame.minTemp;
            smoothedMax = currentFrame.maxTemp;
            autoRangeInitialized = true;
        } else {
            smoothedMin += 0.25f * (currentFrame.minTemp - smoothedMin);
            smoothedMax += 0.25f * (currentFrame.maxTemp - smoothedMax);
        }

        float range = smoothedMax - smoothedMin;
        if (range < 1.0f) range = 1.0f;
        float invRange = 255.0f / range;

        // Clear canvas if using side panels or on first thermal frame transition
        static bool firstThermalFrameRendered = false;
        if (vpX > 0 || !firstThermalFrameRendered) {
            canvas->fillScreen(ILI9341_BLACK);
            firstThermalFrameRendered = true;
        }
        uint16_t* cBuf = canvas->getBuffer();

        // Multi-mode thermal upscaling (Bilinear, Bicubic, Lanczos, TAAU)
        upscaler.render(
            currentScalingMode,
            rotatedGrid,
            gridW,
            gridH,
            cBuf,
            vpX,
            vpY,
            vpW,
            vpH,
            DISPLAY_WIDTH,
            smoothedMin,
            invRange,
            activePalette
        );



        // Calculate real-time display FPS
        frameCount++;
        now = millis();
        if (now - lastFpsCalc >= 1000) {
            currentFps = (frameCount * 1000.0f) / (now - lastFpsCalc);
            frameCount = 0;
            lastFpsCalc = now;
            displayFps = currentFps;
        }
        displayCenterTemp = currentFrame.centerTemp;
        displayMinTemp = currentFrame.minTemp;
        displayMaxTemp = currentFrame.maxTemp;

        // Draw HUD and OSD Overlays directly into the offscreen canvas
        int cx = vpX + vpW / 2;
        int cy = vpY + vpH / 2;

        // Reticle / Center Crosshair
        canvas->drawFastHLine(cx - 7, cy, 15, ILI9341_WHITE);
        canvas->drawFastVLine(cx, cy - 7, 15, ILI9341_WHITE);
        canvas->drawPixel(cx, cy, ILI9341_RED);

        if (vpX > 0) {
            // --- SIDE PANELS LAYOUT ---
            canvas->drawRect(vpX - 1, vpY, vpW + 2, vpH, ILI9341_WHITE);

            // Left panel: Telemetry readouts
            canvas->setTextColor(ILI9341_YELLOW);
            canvas->setTextSize(1);
            canvas->setCursor(4, 10);
            canvas->print("THERMAL");

            canvas->setTextColor(ILI9341_WHITE);
            canvas->setCursor(4, 32);
            canvas->print("CENTER");
            canvas->setTextSize(2);
            canvas->setCursor(4, 46);
            canvas->printf("%.1f", convertTemp(currentFrame.centerTemp, currentTempUnit));
            canvas->setTextSize(1);
            canvas->setCursor(4, 66);
            canvas->print(getTempUnitName(currentTempUnit));

            canvas->setTextColor(ILI9341_CYAN);
            canvas->setCursor(4, 105);
            canvas->print("MIN TEMP");
            canvas->setTextSize(1);
            canvas->setCursor(4, 118);
            canvas->printf("%.1f %s", convertTemp(currentFrame.minTemp, currentTempUnit), getTempUnitSymbol(currentTempUnit));

            canvas->setTextColor(ILI9341_ORANGE);
            canvas->setCursor(4, 145);
            canvas->print("MAX TEMP");
            canvas->setTextSize(1);
            canvas->setCursor(4, 158);
            canvas->printf("%.1f %s", convertTemp(currentFrame.maxTemp, currentTempUnit), getTempUnitSymbol(currentTempUnit));

            // Right panel: FPS, Scaling Mode, and Vertical Color Bar
            canvas->setTextColor(ILI9341_GREEN);
            canvas->setTextSize(1);
            canvas->setCursor(256, 10);
            canvas->printf("%.1f FPS", currentFps);
            canvas->setTextColor(0xBDF7);
            canvas->setCursor(256, 21);
            const char* modeNames[4] = {"BILIN", "BICUB", "LANCZ", "TAAU"};
            canvas->print(modeNames[currentScalingMode % 4]);

            int barX = 265;
            int barY = 32;
            int barW = 16;
            int barH = 165;
            for (int h = 0; h < barH; ++h) {
                int palIdx = 255 - (h * 255) / (barH - 1);
                canvas->drawFastHLine(barX, barY + h, barW, activePalette[palIdx]);
            }
            canvas->drawRect(barX - 1, barY - 1, barW + 2, barH + 2, ILI9341_WHITE);
            canvas->setTextColor(ILI9341_WHITE);
            canvas->setCursor(287, barY + 2);
            canvas->printf("%.0f", convertTemp(smoothedMax, currentTempUnit));
            canvas->setCursor(287, barY + barH - 8);
            canvas->printf("%.0f", convertTemp(smoothedMin, currentTempUnit));

        } else {
            // --- FULLSCREEN BADGE OVERLAYS ---
            canvas->fillRect(cx - 55, 4, 110, 18, ILI9341_BLACK);
            canvas->drawRect(cx - 55, 4, 110, 18, ILI9341_WHITE);
            canvas->setTextColor(ILI9341_WHITE);
            canvas->setTextSize(1);
            canvas->setCursor(cx - 47, 9);
            canvas->printf("+ %.1f %s", convertTemp(currentFrame.centerTemp, currentTempUnit), getTempUnitSymbol(currentTempUnit));

            canvas->fillRect(4, DISPLAY_HEIGHT - 22, 160, 18, ILI9341_BLACK);
            canvas->drawRect(4, DISPLAY_HEIGHT - 22, 160, 18, ILI9341_WHITE);
            canvas->setTextColor(ILI9341_CYAN);
            canvas->setCursor(8, DISPLAY_HEIGHT - 17);
            canvas->printf("Min:%.1f", convertTemp(currentFrame.minTemp, currentTempUnit));
            canvas->setTextColor(ILI9341_ORANGE);
            canvas->setCursor(85, DISPLAY_HEIGHT - 17);
            canvas->printf("Max:%.1f %s", convertTemp(currentFrame.maxTemp, currentTempUnit), getTempUnitSymbol(currentTempUnit));

            const char* modeBadges[4] = {"BIL", "CUB", "LAN", "TAU"};
            canvas->fillRect(DISPLAY_WIDTH - 84, 4, 80, 18, ILI9341_BLACK);
            canvas->drawRect(DISPLAY_WIDTH - 84, 4, 80, 18, ILI9341_GREEN);
            canvas->setTextColor(ILI9341_GREEN);
            canvas->setCursor(DISPLAY_WIDTH - 79, 9);
            canvas->printf("%.1f %s", currentFps, modeBadges[currentScalingMode % 4]);
        }

        // Push the entire composite frame to the ILI9341 display in ONE continuous SPI burst
        tft.startWrite();
        tft.setAddrWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        tft.writePixels(canvas->getBuffer(), DISPLAY_WIDTH * DISPLAY_HEIGHT);
        tft.endWrite();
    }
}
