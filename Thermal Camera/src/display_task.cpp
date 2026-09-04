#include "display_task.hpp"

// Hardware SPI TFT instance
Adafruit_ILI9341 tft = Adafruit_ILI9341(&SPI, TFT_DC, TFT_CS, TFT_RST);

// Double-buffer canvas allocated in PSRAM/heap during initDisplay()
static GFXcanvas16* canvas = nullptr;

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

// 256-color Ironbow thermal colormap
static uint16_t ironbowPalette[256];

// Smoothing variables for auto-ranging
static float smoothedMin = 20.0f;
static float smoothedMax = 35.0f;

// Rotate the raw 32x24 sensor array to the configured orientation
static void rotateSensorFrame(const float* src, float* dst, int rotation) {
    if (rotation == 0) {
        // 0 degrees (normal)
        for (int r = 0; r < 24; ++r) {
            for (int c = 0; c < 32; ++c) {
                #if MIRROR_HORIZONTAL
                dst[r * 32 + c] = src[r * 32 + (31 - c)];
                #else
                dst[r * 32 + c] = src[r * 32 + c];
                #endif
            }
        }
    } else if (rotation == 1) {
        // 90 degrees clockwise (24 cols x 32 rows)
        for (int r = 0; r < 32; ++r) {
            for (int c = 0; c < 24; ++c) {
                int orig_c = r;
                int orig_r = 23 - c;
                #if MIRROR_HORIZONTAL
                orig_c = 31 - orig_c;
                #endif
                dst[r * 24 + c] = src[orig_r * 32 + orig_c];
            }
        }
    } else if (rotation == 2) {
        // 180 degrees (upside down, 32 cols x 24 rows)
        for (int r = 0; r < 24; ++r) {
            for (int c = 0; c < 32; ++c) {
                int orig_c = 31 - c;
                int orig_r = 23 - r;
                #if MIRROR_HORIZONTAL
                orig_c = c;
                #endif
                dst[r * 32 + c] = src[orig_r * 32 + orig_c];
            }
        }
    } else if (rotation == 3) {
        // 270 degrees clockwise (24 cols x 32 rows)
        for (int r = 0; r < 32; ++r) {
            for (int c = 0; c < 24; ++c) {
                int orig_c = 31 - r;
                int orig_r = c;
                #if MIRROR_HORIZONTAL
                orig_c = r;
                #endif
                dst[r * 24 + c] = src[orig_r * 32 + orig_c];
            }
        }
    }
}

// Precompute bilinear interpolation mapping coefficients
static void initBilinearTables() {
#if (CAMERA_ROTATION == 1 || CAMERA_ROTATION == 3)
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
#else
    gridW = SENSOR_WIDTH;  // 32
    gridH = SENSOR_HEIGHT; // 24
    vpW = DISPLAY_WIDTH;   // 320
    vpH = DISPLAY_HEIGHT;  // 240
    vpX = 0;
    vpY = 0;
#endif

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

    Serial.printf("[TFT] Viewport: %dx%d at (%d,%d), Grid: %dx%d, Rotation: %d\n",
                  vpW, vpH, vpX, vpY, gridW, gridH, CAMERA_ROTATION);
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

                r = (uint8_t)(keyColors[k].r + localRatio * (keyColors[k + 1].r - keyColors[k].r));
                g = (uint8_t)(keyColors[k].g + localRatio * (keyColors[k + 1].g - keyColors[k].g));
                b = (uint8_t)(keyColors[k].b + localRatio * (keyColors[k + 1].b - keyColors[k].b));
                break;
            }
        }
        ironbowPalette[i] = tft.color565(r, g, b);
    }
}

bool initDisplay() {
    Serial.println("[TFT] Initializing display pins and SPI bus...");

    // Backlight control
    pinMode(TFT_LED, OUTPUT);
    digitalWrite(TFT_LED, HIGH);

    // Hardware reset sequence
    pinMode(TFT_RST, OUTPUT);
    digitalWrite(TFT_RST, HIGH);
    delay(50);
    digitalWrite(TFT_RST, LOW);
    delay(100);
    digitalWrite(TFT_RST, HIGH);
    delay(200);

    // Initialize custom hardware SPI pins on ESP32-S3
    SPI.begin(TFT_SCK, TFT_MISO, TFT_MOSI, -1);

    // Initialize ILI9341 display
    tft.begin(TFT_SPI_FREQ);
    tft.setRotation(DISPLAY_ROTATION);
    tft.fillScreen(ILI9341_BLACK);

    // Allocate offscreen double-buffer canvas in PSRAM/heap
    canvas = new GFXcanvas16(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (canvas == nullptr || canvas->getBuffer() == nullptr) {
        Serial.println("[TFT] ERROR: Failed to allocate GFXcanvas16 frame buffer!");
        return false;
    }
    Serial.printf("[TFT] Double-buffer canvas allocated: %u bytes in RAM\n",
                  DISPLAY_WIDTH * DISPLAY_HEIGHT * 2);

    canvas->fillScreen(ILI9341_BLACK);

    // Precompute bilinear scaling tables and color palette
    initBilinearTables();
    initIronbowPalette();

    Serial.println("[TFT] ILI9341 display initialized successfully!");
    return true;
}

void displayTask(void *pvParameters) {
    Serial.println("[TASK] TFT Display & Scaling Task started on Core " + String(xPortGetCoreID()));

    ThermalFrame currentFrame;
    uint32_t lastFpsCalc = millis();
    uint32_t frameCount = 0;
    float currentFps = 0.0f;

    while (true) {
        // Wait for frame notification from sensor task
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Fetch latest frame from thread-safe double buffer
        if (!getLatestThermalFrame(currentFrame)) {
            continue;
        }

        // Rotate thermal frame to configured orientation
        rotateSensorFrame(currentFrame.temperatures, rotatedGrid, CAMERA_ROTATION);

        // Smooth min/max to prevent flickering
        smoothedMin += 0.25f * (currentFrame.minTemp - smoothedMin);
        smoothedMax += 0.25f * (currentFrame.maxTemp - smoothedMax);

        float range = smoothedMax - smoothedMin;
        if (range < 1.0f) range = 1.0f;
        float invRange = 255.0f / range;

        // Clear canvas if using side panels
        if (vpX > 0) {
            canvas->fillScreen(ILI9341_BLACK);
        }

        uint16_t* cBuf = canvas->getBuffer();

        // 1. Bilinear interpolation directly into the offscreen canvas memory
        for (int y = 0; y < vpH; ++y) {
            uint8_t y0 = rowTable[y].y0;
            uint8_t y1 = rowTable[y].y1;
            float fy = rowTable[y].fy;
            float omfy = 1.0f - fy;

            const float* r0 = &rotatedGrid[y0 * gridW];
            const float* r1 = &rotatedGrid[y1 * gridW];

            uint16_t* destRow = &cBuf[(vpY + y) * DISPLAY_WIDTH + vpX];

            for (int x = 0; x < vpW; ++x) {
                uint8_t x0 = colTable[x].x0;
                uint8_t x1 = colTable[x].x1;
                float fx = colTable[x].fx;

                // Bilinear formula: interpolate horizontal then vertical
                float top = r0[x0] + fx * (r0[x1] - r0[x0]);
                float bot = r1[x0] + fx * (r1[x1] - r1[x0]);
                float temp = omfy * top + fy * bot;

                int idx = (int)((temp - smoothedMin) * invRange);
                if (idx < 0) idx = 0;
                else if (idx > 255) idx = 255;

                destRow[x] = ironbowPalette[idx];
            }
        }

        // Calculate real-time display FPS
        frameCount++;
        uint32_t now = millis();
        if (now - lastFpsCalc >= 1000) {
            currentFps = (frameCount * 1000.0f) / (now - lastFpsCalc);
            frameCount = 0;
            lastFpsCalc = now;
        }

        // 2. Draw HUD and OSD Overlays directly into the offscreen canvas
        int cx = vpX + vpW / 2;
        int cy = vpY + vpH / 2;

        // Reticle / Center Crosshair
        canvas->drawFastHLine(cx - 7, cy, 15, ILI9341_WHITE);
        canvas->drawFastVLine(cx, cy - 7, 15, ILI9341_WHITE);
        canvas->drawPixel(cx, cy, ILI9341_RED);

        if (vpX > 0) {
            // --- SIDE PANELS LAYOUT (Zero obstruction on thermal feed) ---
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
            canvas->printf("%.1f", currentFrame.centerTemp);
            canvas->setTextSize(1);
            canvas->setCursor(4, 66);
            canvas->print("deg C");

            canvas->setTextColor(ILI9341_CYAN);
            canvas->setCursor(4, 105);
            canvas->print("MIN TEMP");
            canvas->setTextSize(1);
            canvas->setCursor(4, 118);
            canvas->printf("%.1f C", currentFrame.minTemp);

            canvas->setTextColor(ILI9341_ORANGE);
            canvas->setCursor(4, 145);
            canvas->print("MAX TEMP");
            canvas->setTextSize(1);
            canvas->setCursor(4, 158);
            canvas->printf("%.1f C", currentFrame.maxTemp);

            // Right panel: FPS and Vertical Color Bar
            canvas->setTextColor(ILI9341_GREEN);
            canvas->setTextSize(1);
            canvas->setCursor(256, 10);
            canvas->printf("%.1f FPS", currentFps);

            int barX = 265;
            int barY = 32;
            int barW = 16;
            int barH = 165;
            for (int h = 0; h < barH; ++h) {
                int palIdx = 255 - (h * 255) / (barH - 1);
                canvas->drawFastHLine(barX, barY + h, barW, ironbowPalette[palIdx]);
            }
            canvas->drawRect(barX - 1, barY - 1, barW + 2, barH + 2, ILI9341_WHITE);
            canvas->setTextColor(ILI9341_WHITE);
            canvas->setCursor(287, barY + 2);
            canvas->printf("%.0f", smoothedMax);
            canvas->setCursor(287, barY + barH - 8);
            canvas->printf("%.0f", smoothedMin);

        } else {
            // --- FULLSCREEN BADGE OVERLAYS (Offscreen composited, zero flicker) ---
            canvas->fillRect(cx - 55, 4, 110, 18, ILI9341_BLACK);
            canvas->drawRect(cx - 55, 4, 110, 18, ILI9341_WHITE);
            canvas->setTextColor(ILI9341_WHITE);
            canvas->setTextSize(1);
            canvas->setCursor(cx - 47, 9);
            canvas->printf("+ %.1f C", currentFrame.centerTemp);

            canvas->fillRect(4, DISPLAY_HEIGHT - 22, 160, 18, ILI9341_BLACK);
            canvas->drawRect(4, DISPLAY_HEIGHT - 22, 160, 18, ILI9341_WHITE);
            canvas->setTextColor(ILI9341_CYAN);
            canvas->setCursor(8, DISPLAY_HEIGHT - 17);
            canvas->printf("Min:%.1f", currentFrame.minTemp);
            canvas->setTextColor(ILI9341_ORANGE);
            canvas->setCursor(85, DISPLAY_HEIGHT - 17);
            canvas->printf("Max:%.1f C", currentFrame.maxTemp);

            canvas->fillRect(DISPLAY_WIDTH - 80, 4, 76, 18, ILI9341_BLACK);
            canvas->drawRect(DISPLAY_WIDTH - 80, 4, 76, 18, ILI9341_GREEN);
            canvas->setTextColor(ILI9341_GREEN);
            canvas->setCursor(DISPLAY_WIDTH - 73, 9);
            canvas->printf("FPS: %.1f", currentFps);
        }

        // 3. Push the entire composite frame to the ILI9341 display in ONE continuous SPI burst
        tft.startWrite();
        tft.setAddrWindow(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
        tft.writePixels(canvas->getBuffer(), DISPLAY_WIDTH * DISPLAY_HEIGHT);
        tft.endWrite();
    }
}
