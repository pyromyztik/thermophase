#include "scaling.hpp"
#include <esp_heap_caps.h>
#include <utility>
#include <cmath>

static inline float sinc(float x) {
    if (std::abs(x) < 1e-5f) return 1.0f;
    float px = 3.14159265358979323846f * x;
    return std::sin(px) / px;
}

static inline float lanczos3Weight(float x) {
    float ax = std::abs(x);
    if (ax >= 3.0f) return 0.0f;
    return sinc(x) * sinc(x / 3.0f);
}

ThermalScaler::ThermalScaler()
    : m_maxW(DISPLAY_WIDTH),
      m_maxH(DISPLAY_HEIGHT),
      m_vpW(DISPLAY_WIDTH),
      m_vpH(DISPLAY_HEIGHT),
      m_gridW(SENSOR_WIDTH),
      m_gridH(SENSOR_HEIGHT),
      m_histA(nullptr),
      m_histB(nullptr),
      m_histRead(nullptr),
      m_histWrite(nullptr),
      m_historyValid(false),
      m_intermediateRows(nullptr),
      m_cachedMinY0(-1),
      m_cachedMinY1(-1)
{
    memset(m_prevLow, 0, sizeof(m_prevLow));
    memset(m_lowMin, 0, sizeof(m_lowMin));
    memset(m_lowMax, 0, sizeof(m_lowMax));
    memset(m_lineBuf, 0, sizeof(m_lineBuf));
}

ThermalScaler::~ThermalScaler() {
    if (m_histA != nullptr) {
        free(m_histA);
        m_histA = nullptr;
    }
    if (m_histB != nullptr) {
        free(m_histB);
        m_histB = nullptr;
    }
    if (m_intermediateRows != nullptr) {
        free(m_intermediateRows);
        m_intermediateRows = nullptr;
    }
}

bool ThermalScaler::init(int maxW, int maxH) {
    m_maxW = maxW;
    m_maxH = maxH;

    size_t histBytes = m_maxW * m_maxH * sizeof(float);

    // Allocate ping-pong history buffers in Octal PSRAM
    m_histA = (float*)heap_caps_malloc(histBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (m_histA == nullptr) {
        Serial.println("[SCALER] Warning: PSRAM allocation failed for HistA, trying internal RAM...");
        m_histA = (float*)malloc(histBytes);
    }
    m_histB = (float*)heap_caps_malloc(histBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (m_histB == nullptr) {
        Serial.println("[SCALER] Warning: PSRAM allocation failed for HistB, trying internal RAM...");
        m_histB = (float*)malloc(histBytes);
    }
    // Allocate separable intermediate rows (32 rows x 320 cols = 40 KB)
    size_t interBytes = 32 * m_maxW * sizeof(float);
    m_intermediateRows = (float*)heap_caps_malloc(interBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (m_intermediateRows == nullptr) 
        m_intermediateRows = (float*)malloc(interBytes);

    if (m_histA == nullptr || m_histB == nullptr || m_intermediateRows == nullptr) {
        Serial.printf("[SCALER] ERROR: Failed to allocate scaling buffers! (A=%p, B=%p, Inter=%p)\n",
                      m_histA, m_histB, m_intermediateRows);
        return false;
    }
    m_histRead = m_histA;
    m_histWrite = m_histB;
    m_historyValid = false;

    Serial.printf("[SCALER] Initialized successfully. PSRAM Buffers: 2x %u KB, Row Cache: %u KB\n",
                  (unsigned int)(histBytes / 1024), (unsigned int)(interBytes / 1024));
    return true;
}

void ThermalScaler::reset() {
    m_historyValid = false;
    memset(m_prevLow, 0, sizeof(m_prevLow));
}

void ThermalScaler::reconfigureViewport(int vpW, int vpH, 
                                        int gridW, int gridH) {
    m_vpW = vpW;
    m_vpH = vpH;
    m_gridW = gridW;
    m_gridH = gridH;
    m_cachedMinY0 = -1;
    m_cachedMinY1 = -1;

    reset();

    // 0. Precompute Nearest Neighbor Tables
    for (int x = 0; x < vpW; ++x) {
        int gx = (int)((float)x * (float)gridW / (float)vpW);
        if (gx < 0) gx = 0;
        if (gx > gridW - 1) gx = gridW - 1;
        m_colNNTable[x] = (uint8_t)gx;
    }
    for (int y = 0; y < vpH; ++y) {
        int gy = (int)((float)y * (float)gridH / (float)vpH);
        if (gy < 0) gy = 0;
        if (gy > gridH - 1) gy = gridH - 1;
        m_rowNNTable[y] = (uint8_t)gy;
    }

    // 1. Precompute Bilinear Tables
    for (int x = 0; x < vpW; ++x) {
        float u = (float)x * (float)(gridW - 1) / (float)(vpW - 1);
        uint8_t x0 = (uint8_t)u;
        uint8_t x1 = (x0 < gridW - 1) ? (x0 + 1) : (gridW - 1);
        m_colBilinearTable[x].x0 = x0;
        m_colBilinearTable[x].x1 = x1;
        m_colBilinearTable[x].fx = u - (float)x0;
    }
    for (int y = 0; y < vpH; ++y) {
        float v = (float)y * (float)(gridH - 1) / (float)(vpH - 1);
        uint8_t y0 = (uint8_t)v;
        uint8_t y1 = (y0 < gridH - 1) ? (y0 + 1) : (gridH - 1);
        m_rowBilinearTable[y].y0 = y0;
        m_rowBilinearTable[y].y1 = y1;
        m_rowBilinearTable[y].fy = v - (float)y0;
    }

    // 2. Precompute 1D Catmull-Rom Cubic Tables (Columns)
    for (int x = 0; x < vpW; ++x) {
        float u = (float)x * (float)(gridW - 1) / (float)(vpW - 1);
        int x1 = (int)u;
        if (x1 > gridW - 1) x1 = gridW - 1;
        float t = u - (float)x1;

        int x0 = (x1 > 0) ? (x1 - 1) : 0;
        int x2 = (x1 < gridW - 1) ? (x1 + 1) : (gridW - 1);
        int x3 = (x2 < gridW - 1) ? (x2 + 1) : (gridW - 1);

        float t2 = t * t;
        float t3 = t2 * t;

        m_colCubicTable[x].idx0 = x0;
        m_colCubicTable[x].idx1 = x1;
        m_colCubicTable[x].idx2 = x2;
        m_colCubicTable[x].idx3 = x3;

        m_colCubicTable[x].w0 = -0.5f * t3 + 1.0f * t2 - 0.5f * t;
        m_colCubicTable[x].w1 =  1.5f * t3 - 2.5f * t2 + 1.0f;
        m_colCubicTable[x].w2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
        m_colCubicTable[x].w3 =  0.5f * t3 - 0.5f * t2;
    }

    // Precompute 1D Catmull-Rom Cubic Tables (Rows)
    for (int y = 0; y < vpH; ++y) {
        float v = (float)y * (float)(gridH - 1) / (float)(vpH - 1);
        int y1 = (int)v;
        if (y1 > gridH - 1) y1 = gridH - 1;
        float t = v - (float)y1;

        int y0 = (y1 > 0) ? (y1 - 1) : 0;
        int y2 = (y1 < gridH - 1) ? (y1 + 1) : (gridH - 1);
        int y3 = (y2 < gridH - 1) ? (y2 + 1) : (gridH - 1);

        float t2 = t * t;
        float t3 = t2 * t;

        m_rowCubicTable[y].idx0 = y0;
        m_rowCubicTable[y].idx1 = y1;
        m_rowCubicTable[y].idx2 = y2;
        m_rowCubicTable[y].idx3 = y3;

        m_rowCubicTable[y].w0 = -0.5f * t3 + 1.0f * t2 - 0.5f * t;
        m_rowCubicTable[y].w1 =  1.5f * t3 - 2.5f * t2 + 1.0f;
        m_rowCubicTable[y].w2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
        m_rowCubicTable[y].w3 =  0.5f * t3 - 0.5f * t2;
    }

    // 3. Precompute 1D Lanczos-3 Tables (Columns, 6 taps)
    for (int x = 0; x < vpW; ++x) {
        float u = (float)x * (float)(gridW - 1) / (float)(vpW - 1);
        int xc = (int)u;
        float sumW = 0.0f;

        for (int k = -2; k <= 3; ++k) {
            int tapIdx = k + 2;
            int srcIdx = xc + k;
            if (srcIdx < 0) srcIdx = 0;
            else if (srcIdx > gridW - 1) srcIdx = gridW - 1;

            float dist = u - (float)(xc + k);
            float weight = lanczos3Weight(dist);

            m_colLanczosTable[x].idx[tapIdx] = srcIdx;
            m_colLanczosTable[x].w[tapIdx] = weight;
            sumW += weight;
        }
        if (std::abs(sumW) > 1e-5f) {
            for (int k = 0; k < 6; ++k) {
                m_colLanczosTable[x].w[k] /= sumW;
            }
        }
    }

    // Precompute 1D Lanczos-3 Tables (Rows, 6 taps)
    for (int y = 0; y < vpH; ++y) {
        float v = (float)y * (float)(gridH - 1) / (float)(vpH - 1);
        int yc = (int)v;
        float sumW = 0.0f;

        for (int k = -2; k <= 3; ++k) {
            int tapIdx = k + 2;
            int srcIdx = yc + k;
            if (srcIdx < 0) srcIdx = 0;
            else if (srcIdx > gridH - 1) srcIdx = gridH - 1;

            float dist = v - (float)(yc + k);
            float weight = lanczos3Weight(dist);

            m_rowLanczosTable[y].idx[tapIdx] = srcIdx;
            m_rowLanczosTable[y].w[tapIdx] = weight;
            sumW += weight;
        }

        if (std::abs(sumW) > 1e-5f) {
            for (int k = 0; k < 6; ++k) 
                m_rowLanczosTable[y].w[k] /= sumW;
        }
    }
    Serial.printf("[SCALER] Reconfigured Viewport: %dx%d from Grid %dx%d\n",
                  m_vpW, m_vpH, m_gridW, m_gridH);
}

void ThermalScaler::render(
    ScalingMode mode,
    const float* lowGrid,
    int gridW,
    int gridH,
    uint16_t* canvasBuf,
    int vpX,
    int vpY,
    int vpW,
    int vpH,
    int canvasW,
    float smoothedMin,
    float invRange,
    const uint16_t* palette,
    float taauAlpha,
    float taauSharpness
) {
    switch (mode) {
        case SCALING_NEAREST:
            renderNearest(lowGrid, canvasBuf, vpX, vpY, vpW, vpH, canvasW,
                          smoothedMin, invRange, palette);
            break;

        case SCALING_BILINEAR:
            renderBilinear(lowGrid, canvasBuf, vpX, vpY, vpW, vpH, canvasW,
                           smoothedMin, invRange, palette);
            break;

        case SCALING_BICUBIC:
            renderBicubic(lowGrid, canvasBuf, vpX, vpY, vpW, vpH, canvasW,
                          smoothedMin, invRange, palette);
            break;

        case SCALING_LANCZOS:
            renderLanczos(lowGrid, canvasBuf, vpX, vpY, vpW, vpH, canvasW,
                          smoothedMin, invRange, palette);
            break;

        case SCALING_TAAU:
        default:
            renderTAAU(lowGrid, canvasBuf, vpX, vpY, vpW, vpH, canvasW,
                       smoothedMin, invRange, palette, taauAlpha, taauSharpness);
            break;
    }
}

