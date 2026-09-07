#include "display.hpp"
#include "scaling.hpp"
#include "power.hpp"
#include "diagnostics.hpp"
#include <esp_timer.h>
#include <esp_heap_caps.h>

// Hardware SPI TFT instance
Adafruit_ILI9341 tft = Adafruit_ILI9341(&SPI, TFT_DC, TFT_CS, TFT_RST);

// Multi-mode thermal scaling engine (Nearest, Bilinear, Bicubic, Lanczos, TAAU)
static ThermalScaler scaler;

// Viewport and rotated sensor dimensions
static int vpX = 0;
static int vpY = 0;
static int vpW = DISPLAY_WIDTH;
static int vpH = DISPLAY_HEIGHT;
static int gridW = SENSOR_WIDTH;
static int gridH = SENSOR_HEIGHT;

// Rotated thermal sensor temperature buffer
static float rotatedGrid[SENSOR_PIXELS];

// PSRAM Thermal RGB565 Frame Buffer (up to 320x240 pixels)
static uint16_t* thermalFrameBuffer = nullptr;

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

// Live telemetry variables for continuous serial reporting
volatile float displayFps = 0.0f;
volatile float displayCenterTemp = 0.0f;
volatile float displayMinTemp = 0.0f;
volatile float displayMaxTemp = 0.0f;

// LVGL Draw Buffer (allocated in internal DMA-capable SRAM: 120 lines = 2 passes, fits comfortably in DRAM)
#define LVGL_DRAW_BUF_LINES 120
static lv_color_t disp_buf[DISPLAY_WIDTH * LVGL_DRAW_BUF_LINES];
static lv_disp_draw_buf_t draw_buf;
static lv_disp_drv_t disp_drv;
static lv_indev_drv_t indev_drv;

// Rotate the raw 32x24 sensor array to the specified orientation and mirror mode
static void rotateSensorFrame(const float* src, float* dst, int rotation, bool mirror) {
    if (rotation == 0) {
        for (int r = 0; r < 24; ++r) {
            for (int c = 0; c < 32; ++c) {
                int screen_c = mirror ? (31 - c) : c;
                dst[r * 32 + c] = src[r * 32 + screen_c];
            }
        }
    } else if (rotation == 1) {
        for (int r = 0; r < 32; ++r) {
            for (int c = 0; c < 24; ++c) {
                int screen_c = mirror ? (23 - c) : c;
                int orig_r = 23 - screen_c;
                int orig_c = r;
                dst[r * 24 + c] = src[orig_r * 32 + orig_c];
            }
        }
    } else if (rotation == 2) {
        for (int r = 0; r < 24; ++r) {
            for (int c = 0; c < 32; ++c) {
                int screen_c = mirror ? (31 - c) : c;
                int orig_r = 23 - r;
                int orig_c = 31 - screen_c;
                dst[r * 32 + c] = src[orig_r * 32 + orig_c];
            }
        }
    } else if (rotation == 3) {
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

// Dynamically recompute viewport dimensions and interpolation coefficients
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
        vpH = DISPLAY_HEIGHT;                 // 240
        vpW = (vpH * gridW) / gridH;          // 180
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

    scaler.reconfigureViewport(vpW, vpH, gridW, gridH);
    Serial.printf("[TFT] Recomputed Viewport: %dx%d at (%d,%d), Grid: %dx%d, Rotation: %d\n",
                  vpW, vpH, vpX, vpY, gridW, gridH, currentCameraRotation);
}

// Generate Ironbow 256-color RGB565 palette
static void initIronbowPalette() {
    struct ColorPoint { float pos; uint8_t r, g, b; };
    const ColorPoint keyColors[] = {
        {0.00f, 0,   0,   32},
        {0.20f, 80,  0,   130},
        {0.45f, 210, 20,  20},
        {0.70f, 255, 140, 0},
        {0.88f, 255, 235, 30},
        {1.00f, 255, 255, 255}
    };
    const size_t numKeys = sizeof(keyColors) / sizeof(keyColors[0]);

    for (int i = 0; i < 256; ++i) {
        float pos = (float)i / 255.0f;
        uint8_t r = 0, g = 0, b = 0;
        for (size_t k = 0; k < numKeys - 1; ++k) {
            if (pos >= keyColors[k].pos && pos <= keyColors[k + 1].pos) {
                float segmentSpan = keyColors[k + 1].pos - keyColors[k].pos;
                float localRatio = (pos - keyColors[k].pos) / segmentSpan;
                r = (uint8_t)(keyColors[k].r + localRatio * (keyColors[k + 1].r - keyColors[k].r));
                g = (uint8_t)(keyColors[k].g + localRatio * (keyColors[k + 1].g - keyColors[k].g));
                b = (uint8_t)(keyColors[k].b + localRatio * (keyColors[k + 1].b - keyColors[k].b));
                break;
            }
        }
        ironbowPalette[i] = tft.color565(r, g, b);
    }
}

// Generate White-Hot monochrome 256-color palette
static void initWhiteHotPalette() {
    for (int i = 0; i < 256; ++i) {
        whiteHotPalette[i] = tft.color565((uint8_t)i, (uint8_t)i, (uint8_t)i);
    }
}

// Generate classic Rainbow palette
static void initRainbowPalette() {
    struct ColorPoint { float pos; uint8_t r, g, b; };
    const ColorPoint keyColors[] = {
        {0.00f, 0,   0,   255},
        {0.25f, 0,   255, 255},
        {0.50f, 0,   255, 0},
        {0.75f, 255, 255, 0},
        {1.00f, 255, 0,   0}
    };
    const size_t numKeys = sizeof(keyColors) / sizeof(keyColors[0]);

    for (int i = 0; i < 256; ++i) {
        float pos = (float)i / 255.0f;
        uint8_t r = 0, g = 0, b = 0;
        for (size_t k = 0; k < numKeys - 1; ++k) {
            if (pos >= keyColors[k].pos && pos <= keyColors[k + 1].pos) {
                float segmentSpan = keyColors[k + 1].pos - keyColors[k].pos;
                float localRatio = (pos - keyColors[k].pos) / segmentSpan;
                r = (uint8_t)(keyColors[k].r + localRatio * (keyColors[k + 1].r - keyColors[k].r));
                g = (uint8_t)(keyColors[k].g + localRatio * (keyColors[k + 1].g - keyColors[k].g));
                b = (uint8_t)(keyColors[k].b + localRatio * (keyColors[k + 1].b - keyColors[k].b));
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

// Accessors
uint16_t* getThermalFrameBuffer() { return thermalFrameBuffer; }
const uint16_t* getActivePalette() { return activePalette; }

// Settings mutators
void updateCameraRotation(int rotation) {
    recomputeBilinearTables(rotation);
}
void updateTempUnit(TempUnit unit) {
    currentTempUnit = unit;
}
void updatePalette(PaletteType palette) {
    currentPaletteType = palette;
    if (palette == PALETTE_WHITE_HOT) activePalette = whiteHotPalette;
    else if (palette == PALETTE_RAINBOW) activePalette = rainbowPalette;
    else activePalette = ironbowPalette;
}
void updateMirror(bool mirror) {
    currentMirrorFeed = mirror;
    scaler.reset();
}
void updateScalingMode(ScalingMode mode) {
    currentScalingMode = mode;
    scaler.reset();
}
ScalingMode getScalingMode() {
    return currentScalingMode;
}

static uint8_t currentDisplayBrightness = TFT_DEFAULT_BRIGHTNESS;

void setDisplayBrightness(uint8_t percent) {
    if (percent > 100) percent = 100;
    if (percent < 10) percent = 10;
    currentDisplayBrightness = percent;
    uint32_t duty = ((uint32_t)percent * 255) / 100;
    ledcWrite(TFT_LED_PWM_CHANNEL, duty);
}

uint8_t getDisplayBrightness() {
    return currentDisplayBrightness;
}

bool checkDisplayConnected() {
#if CHECK_TFT_MISO
    pinMode(TFT_MISO, INPUT_PULLDOWN);
    tft.setSPISpeed(2000000);

    uint8_t powerMode = tft.readcommand8(ILI9341_RDMODE);
    uint8_t madctl    = tft.readcommand8(ILI9341_RDMADCTL);
    uint8_t pixFmt    = tft.readcommand8(ILI9341_RDPIXFMT);

    tft.setSPISpeed(TFT_SPI_FREQ);

    bool connected = (powerMode != 0x00 && powerMode != 0xFF) ||
                     (madctl != 0x00 && madctl != 0xFF) ||
                     (pixFmt != 0x00 && pixFmt != 0xFF);
    return connected;
#else
    return true;
#endif
}

// LVGL Display Flush Callback
static void disp_flush_cb(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = (area->x2 - area->x1 + 1);
    uint32_t h = (area->y2 - area->y1 + 1);

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.writePixels((uint16_t*)&color_p->full, w * h);
    tft.endWrite();

    lv_disp_flush_ready(drv);
}

// LVGL Rotary Wheel Input Callback
static void wheel_read_cb(lv_indev_drv_t* drv, lv_indev_data_t* data) {
    static int32_t last_diff = 0;
    static bool button_pressed = false;

    WheelEventType evt;
    while (getWheelEvent(evt, 0)) {
        AppState state = getAppState();
        if (state == APP_STATE_THERMAL_VIEW) {
            if (evt == WHEEL_EVENT_CLICK) {
                // Click in thermal view transitions to Main Menu.
                // Consume the event here so it is not forwarded to the newly focused button!
                uiNavToMainMenu();
            }
        } else {
            // In Main Menu or Settings
            if (evt == WHEEL_EVENT_ROT_CW) {
                last_diff += 1;
            } else if (evt == WHEEL_EVENT_ROT_CCW) {
                last_diff -= 1;
            } else if (evt == WHEEL_EVENT_CLICK) {
                button_pressed = true;
            }
        }
    }

    data->enc_diff = last_diff;
    last_diff = 0;

    if (button_pressed) {
        data->state = LV_INDEV_STATE_PRESSED;
        button_pressed = false; // Next poll will read RELEASED
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void showBootScreen(const char* statusText, uint8_t progressPercent) {
    updateBootScreenUI(statusText, progressPercent);
    // Backlight on safely via PWM after initial frame
    setDisplayBrightness(currentDisplayBrightness);
}

bool initDisplay() {
    Serial.println("[TFT] Initializing display pins and SPI bus...");

    pinMode(TFT_LED, OUTPUT);
    digitalWrite(TFT_LED, LOW);
    ledcSetup(TFT_LED_PWM_CHANNEL, TFT_LED_PWM_FREQ, TFT_LED_PWM_RES);
    ledcAttachPin(TFT_LED, TFT_LED_PWM_CHANNEL);
    ledcWrite(TFT_LED_PWM_CHANNEL, 0);

    pinMode(TFT_RST, OUTPUT);
    digitalWrite(TFT_RST, HIGH);
    delay(50);
    digitalWrite(TFT_RST, LOW);
    delay(100);
    digitalWrite(TFT_RST, HIGH);
    delay(200);

    pinMode(TFT_MISO, INPUT_PULLDOWN);

    SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, -1);

    tft.begin(TFT_SPI_FREQ);
    tft.setRotation(DISPLAY_ROTATION);
    tft.fillScreen(ILI9341_BLACK);

#if CHECK_TFT_MISO
    tft.setSPISpeed(2000000);
    uint8_t powerMode = tft.readcommand8(ILI9341_RDMODE);
    uint8_t madctl    = tft.readcommand8(ILI9341_RDMADCTL);
    uint8_t pixFmt    = tft.readcommand8(ILI9341_RDPIXFMT);
    tft.setSPISpeed(TFT_SPI_FREQ);

    bool displayOk = (powerMode != 0x00 && powerMode != 0xFF) ||
                     (madctl != 0x00 && madctl != 0xFF) ||
                     (pixFmt != 0x00 && pixFmt != 0xFF);
    if (!displayOk) {
        globalDeviceStatus = STATUS_DISPLAY_ERROR;
        Serial.println("[TFT] ERROR: Display not responding via SPI MISO!");
    } else {
        globalDeviceStatus = STATUS_OK;
        Serial.println("[TFT] Display communication verified via MISO.");
    }
#else
    globalDeviceStatus = STATUS_OK;
#endif

    // Allocate Thermal Frame RGB565 Buffer in PSRAM (320x240 pixels = 153.6 KB)
    thermalFrameBuffer = (uint16_t*)heap_caps_malloc(
        DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    if (thermalFrameBuffer == nullptr) {
        Serial.println("[TFT] Falling back to standard malloc for thermalFrameBuffer...");
        thermalFrameBuffer = (uint16_t*)malloc(DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t));
    }
    if (thermalFrameBuffer == nullptr) {
        Serial.println("[TFT] FATAL: Failed to allocate thermalFrameBuffer!");
        return false;
    }
    memset(thermalFrameBuffer, 0, DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t));

    // Initialize LVGL Core
    lv_init();

    // Initialize LVGL Draw Buffer (full-frame buffer in internal SRAM eliminates strip overhead)
    lv_disp_draw_buf_init(&draw_buf, disp_buf, NULL, DISPLAY_WIDTH * LVGL_DRAW_BUF_LINES);

    // Register LVGL Display Driver
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISPLAY_WIDTH;
    disp_drv.ver_res = DISPLAY_HEIGHT;
    disp_drv.flush_cb = disp_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    // Initialize LVGL UI screens & styles
    initUI();

    // Register LVGL Input Device Driver (Rotary Wheel)
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_ENCODER;
    indev_drv.read_cb = wheel_read_cb;
    lv_indev_t* wheel_indev = lv_indev_drv_register(&indev_drv);
    lv_indev_set_group(wheel_indev, ui_wheel_group);

    // Initialize Multi-mode Scaler
    if (!scaler.init(DISPLAY_WIDTH, DISPLAY_HEIGHT)) {
        Serial.println("[TFT] Warning: Scaler PSRAM allocation partial.");
    }

    recomputeBilinearTables(currentCameraRotation);
    initAllPalettes();

    // Render initial boot progress
    showBootScreen("Starting System...", 15);
    Serial.println("[TFT] LVGL and Display initialized successfully!");
    return (globalDeviceStatus == STATUS_OK);
}

void displayTask(void *pvParameters) {
    Serial.println("[TASK] LVGL Display & Thermal Pipeline Task started on Core " 
        + String(xPortGetCoreID()));

    ThermalFrame currentFrame;
    uint32_t lastFpsCalc = millis();
    uint32_t frameCount = 0;
    float currentFps = 0.0f;
    uint32_t lastDisplayPoll = 0;
    int displayFailCount = 0;

    // Transition from Boot Screen to Thermal View
    vTaskDelay(pdMS_TO_TICKS(100));
    uiNavToThermal();

    while (true) {
        uint32_t now = millis();

        // 1. Connection Monitoring (every 1000 ms)
        #if CHECK_TFT_MISO
        if (now - lastDisplayPoll >= 1000) {
            lastDisplayPoll = now;
            bool connected = checkDisplayConnected();
            if (!connected) {
                displayFailCount++;
                if (displayFailCount >= 2) {
                    globalDeviceStatus = STATUS_DISPLAY_ERROR;
                }
            } else {
                if (globalDeviceStatus == STATUS_DISPLAY_ERROR) {
                    Serial.println("[TFT] SUCCESS: Display reconnected!");
                    tft.begin(TFT_SPI_FREQ);
                    tft.setRotation(DISPLAY_ROTATION);
                    lv_obj_invalidate(lv_scr_act());
                }
                displayFailCount = 0;
                globalDeviceStatus = STATUS_OK;
            }
        }
        #endif

        if (globalDeviceStatus != STATUS_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        // 2. Hardware Sensor Disconnect / Error handling
        if (globalSensorStatus != SENSOR_OK) {
            if (getAppState() != APP_STATE_SENSOR_ERROR) {
                uiNavToSensorError(sensorReconnectAttempts);
            } else {
                updateSensorErrorAttempt(sensorReconnectAttempts);
            }
        } else if (getAppState() == APP_STATE_SENSOR_ERROR) {
            uiDismissSensorError();
        }

        // 3. Thermal Frame Acquisition and Upscaling (when in live thermal view and fade transition completed)
        bool frameProcessed = false;
        if (getAppState() == APP_STATE_THERMAL_VIEW && lv_scr_act() == screen_thermal && globalSensorStatus == SENSOR_OK) {
            // Check if notified of new sensor frame (timeout 10 ms to keep UI responsive)
            uint32_t notified = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
            if (notified > 0) {
                if (getLatestThermalFrame(currentFrame)) {
                    frameProcessed = true;
                    static int64_t lastFrameStartUs = 0;
                    int64_t frameStartUs = esp_timer_get_time();
                    int64_t frameIntervalUs = (lastFrameStartUs > 0) ? (frameStartUs - lastFrameStartUs) : 66666;
                    lastFrameStartUs = frameStartUs;

                    // Rotate raw frame
                    rotateSensorFrame(currentFrame.temperatures, rotatedGrid, currentCameraRotation, currentMirrorFeed);

                    // Dynamic scene auto-ranging
                    static bool autoRangeInitialized = false;
                    if (!autoRangeInitialized) {
                        smoothedMin = currentFrame.minTemp;
                        smoothedMax = currentFrame.maxTemp;
                        autoRangeInitialized = true;
                    } else {
                        smoothedMin += 0.25f * (currentFrame.minTemp - smoothedMin);
                        smoothedMax += 0.25f * (currentFrame.maxTemp - smoothedMax);
                    }

                    // Technique C: Auto-range floor clamping (5.0 deg C)
                    // When scene thermal contrast is smaller than the floor, center the span symmetrically
                    // around the scene mean temperature so low-contrast scenes don't over-amplify noise.
                    float range = smoothedMax - smoothedMin;
                    float effectiveMin = smoothedMin;
                    if (range < THERMAL_MIN_RANGE_SPAN) {
                        float mid = 0.5f * (smoothedMin + smoothedMax);
                        range = THERMAL_MIN_RANGE_SPAN;
                        effectiveMin = mid - 0.5f * range;
                    }
                    float invRange = 255.0f / range;

                    // Execute thermal scaling directly into the thermalFrameBuffer
                    int64_t tScaleStart = esp_timer_get_time();
                    scaler.render(
                        currentScalingMode,
                        rotatedGrid,
                        gridW,
                        gridH,
                        thermalFrameBuffer,
                        0,
                        0,
                        vpW,
                        vpH,
                        vpW,
                        effectiveMin,
                        invRange,
                        activePalette
                    );
                    int64_t tScaleEnd = esp_timer_get_time();
                    int64_t scaleUs = tScaleEnd - tScaleStart;

                    // FPS calculation
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

                    // Update LVGL HUD telemetry and color bar
                    updateThermalHUD(
                        currentFrame.centerTemp,
                        currentFrame.minTemp,
                        currentFrame.maxTemp,
                        currentFps,
                        currentScalingMode,
                        currentTempUnit,
                        activePalette,
                        vpX, vpY, vpW, vpH
                    );

                    // Render & flush LVGL draw buffer directly via SPI DMA
                    int64_t tFlushStart = esp_timer_get_time();
                    lv_timer_handler();
                    int64_t tFlushEnd = esp_timer_get_time();
                    int64_t flushUs = tFlushEnd - tFlushStart;

                    int64_t frameTotalUs = tFlushEnd - frameStartUs;
                    logFrameProfile(scaleUs, flushUs, frameTotalUs, frameIntervalUs);
                }
            }
        }

        // 4. Handle LVGL timer and animation ticks when not flushed with thermal frame
        if (!frameProcessed) {
            lv_timer_handler();
        }

        // Yield to allow FreeRTOS scheduling without sleeping through the next frame
        if (getAppState() == APP_STATE_THERMAL_VIEW) {
            taskYIELD();
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
