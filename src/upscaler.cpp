#include "upscaler.hpp"
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

ThermalUpscaler::ThermalUpscaler()
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


ThermalUpscaler::~ThermalUpscaler() {
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

bool ThermalUpscaler::init(int maxW, int maxH) {
    m_maxW = maxW;
    m_maxH = maxH;

    size_t histBytes = m_maxW * m_maxH * sizeof(float);

    // Allocate ping-pong history buffers in Octal PSRAM
    m_histA = (float*)heap_caps_malloc(histBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (m_histA == nullptr) {
        Serial.println("[UPSCALER] Warning: PSRAM allocation failed for HistA, trying internal RAM...");
        m_histA = (float*)malloc(histBytes);
    }
    m_histB = (float*)heap_caps_malloc(histBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (m_histB == nullptr) {
        Serial.println("[UPSCALER] Warning: PSRAM allocation failed for HistB, trying internal RAM...");
        m_histB = (float*)malloc(histBytes);
    }
    // Allocate separable intermediate rows (32 rows x 320 cols = 40 KB)
    // Internal RAM preferred for single-cycle row staging
    size_t interBytes = 32 * m_maxW * sizeof(float);
    m_intermediateRows = (float*)heap_caps_malloc(interBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (m_intermediateRows == nullptr) 
        m_intermediateRows = (float*)malloc(interBytes);

    if (m_histA == nullptr || m_histB == nullptr || m_intermediateRows == nullptr) {
        Serial.printf("[UPSCALER] ERROR: Failed to allocate scaling buffers! (A=%p, B=%p, Inter=%p)\n",
                      m_histA, m_histB, m_intermediateRows);
        return false;
    }
    m_histRead = m_histA;
    m_histWrite = m_histB;
    m_historyValid = false;

    Serial.printf("[UPSCALER] Initialized successfully. PSRAM Buffers: 2x %u KB, Row Cache: %u KB\n",
                  (unsigned int)(histBytes / 1024), (unsigned int)(interBytes / 1024));
    return true;
}

void ThermalUpscaler::reset() {
    m_historyValid = false;
    memset(m_prevLow, 0, sizeof(m_prevLow));
}

void ThermalUpscaler::reconfigureViewport(int vpW, int vpH, int gridW, int gridH) {
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
        // Normalize weights to 1.0 for unit gain
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
    Serial.printf("[UPSCALER] Reconfigured Viewport: %dx%d from Grid %dx%d\n",
                  m_vpW, m_vpH, m_gridW, m_gridH);
}

void ThermalUpscaler::render(
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

// -----------------------------------------------------------------------------
// 0. Nearest Neighbor Interpolation
// -----------------------------------------------------------------------------
void ThermalUpscaler::renderNearest(
    const float* lowGrid,
    uint16_t* canvasBuf,
    int vpX, int vpY, int vpW, int vpH, int canvasW,
    float smoothedMin, float invRange, const uint16_t* palette
) {
    for (int y = 0; y < vpH; ++y) {
        uint8_t gy = m_rowNNTable[y];
        const float* row = &lowGrid[gy * m_gridW];
        uint16_t* destRow = &canvasBuf[(vpY + y) * canvasW + vpX];

        for (int x = 0; x < vpW; ++x) {
            uint8_t gx = m_colNNTable[x];
            float temp = row[gx];

            int idx = (int)((temp - smoothedMin) * invRange);
            if (idx < 0) idx = 0;
            else if (idx > 255) idx = 255;

            destRow[x] = palette[idx];
        }
    }
}

// -----------------------------------------------------------------------------
// 1. Bilinear Interpolation
// -----------------------------------------------------------------------------
void ThermalUpscaler::renderBilinear(
    const float* lowGrid,
    uint16_t* canvasBuf,
    int vpX, int vpY, int vpW, int vpH, int canvasW,
    float smoothedMin, float invRange, const uint16_t* palette
) {
    for (int y = 0; y < vpH; ++y) {
        uint8_t y0 = m_rowBilinearTable[y].y0;
        uint8_t y1 = m_rowBilinearTable[y].y1;
        float fy = m_rowBilinearTable[y].fy;
        float omfy = 1.0f - fy;

        const float* r0 = &lowGrid[y0 * m_gridW];
        const float* r1 = &lowGrid[y1 * m_gridW];

        uint16_t* destRow = &canvasBuf[(vpY + y) * canvasW + vpX];

        for (int x = 0; x < vpW; ++x) {
            uint8_t x0 = m_colBilinearTable[x].x0;
            uint8_t x1 = m_colBilinearTable[x].x1;
            float fx = m_colBilinearTable[x].fx;

            float top = r0[x0] + fx * (r0[x1] - r0[x0]);
            float bot = r1[x0] + fx * (r1[x1] - r1[x0]);
            float temp = omfy * top + fy * bot;

            int idx = (int)((temp - smoothedMin) * invRange);
            if (idx < 0) idx = 0;
            else if (idx > 255) idx = 255;

            destRow[x] = palette[idx];
        }
    }
}

// -----------------------------------------------------------------------------
// 2. Separable Catmull-Rom Bicubic Interpolation
// -----------------------------------------------------------------------------
void ThermalUpscaler::renderBicubic(
    const float* lowGrid,
    uint16_t* canvasBuf,
    int vpX, int vpY, int vpW, int vpH, int canvasW,
    float smoothedMin, float invRange, const uint16_t* palette
) {
    if (m_intermediateRows == nullptr) return;

    // Horizontal pass across grid rows
    for (int r = 0; r < m_gridH; ++r) {
        const float* srcRow = &lowGrid[r * m_gridW];
        float* dstRow = &m_intermediateRows[r * vpW];

        for (int x = 0; x < vpW; ++x) {
            const CubicInterp& c = m_colCubicTable[x];
            dstRow[x] = c.w0 * srcRow[c.idx0] + c.w1 * srcRow[c.idx1] +
                        c.w2 * srcRow[c.idx2] + c.w3 * srcRow[c.idx3];
        }
    }
    // Vertical evaluation & direct palette mapping
    for (int y = 0; y < vpH; ++y) {
        const CubicInterp& rCubic = m_rowCubicTable[y];
        const float* ir0 = &m_intermediateRows[rCubic.idx0 * vpW];
        const float* ir1 = &m_intermediateRows[rCubic.idx1 * vpW];
        const float* ir2 = &m_intermediateRows[rCubic.idx2 * vpW];
        const float* ir3 = &m_intermediateRows[rCubic.idx3 * vpW];
        float rw0 = rCubic.w0, rw1 = rCubic.w1, rw2 = rCubic.w2, rw3 = rCubic.w3;

        uint16_t* destRow = &canvasBuf[(vpY + y) * canvasW + vpX];

        for (int x = 0; x < vpW; ++x) {
            float temp = rw0 * ir0[x] + rw1 * ir1[x] + rw2 * ir2[x] + rw3 * ir3[x];

            int idx = (int)((temp - smoothedMin) * invRange);
            if (idx < 0) idx = 0;
            else if (idx > 255) idx = 255;

            destRow[x] = palette[idx];
        }
    }
}

// -----------------------------------------------------------------------------
// 3. Separable Lanczos-3 Interpolation (6 taps)
// -----------------------------------------------------------------------------
void ThermalUpscaler::renderLanczos(
    const float* lowGrid,
    uint16_t* canvasBuf,
    int vpX, int vpY, int vpW, int vpH, int canvasW,
    float smoothedMin, float invRange, const uint16_t* palette
) {
    if (m_intermediateRows == nullptr) return;

    // Horizontal pass across grid rows
    for (int r = 0; r < m_gridH; ++r) {
        const float* srcRow = &lowGrid[r * m_gridW];
        float* dstRow = &m_intermediateRows[r * vpW];

        for (int x = 0; x < vpW; ++x) {
            const LanczosInterp& c = m_colLanczosTable[x];
            dstRow[x] = c.w[0] * srcRow[c.idx[0]] + c.w[1] * srcRow[c.idx[1]] +
                        c.w[2] * srcRow[c.idx[2]] + c.w[3] * srcRow[c.idx[3]] +
                        c.w[4] * srcRow[c.idx[4]] + c.w[5] * srcRow[c.idx[5]];
        }
    }
    // Vertical evaluation & direct palette mapping
    for (int y = 0; y < vpH; ++y) {
        const LanczosInterp& r = m_rowLanczosTable[y];
        const float* ir0 = &m_intermediateRows[r.idx[0] * vpW];
        const float* ir1 = &m_intermediateRows[r.idx[1] * vpW];
        const float* ir2 = &m_intermediateRows[r.idx[2] * vpW];
        const float* ir3 = &m_intermediateRows[r.idx[3] * vpW];
        const float* ir4 = &m_intermediateRows[r.idx[4] * vpW];
        const float* ir5 = &m_intermediateRows[r.idx[5] * vpW];

        uint16_t* destRow = &canvasBuf[(vpY + y) * canvasW + vpX];

        for (int x = 0; x < vpW; ++x) {
            float temp = r.w[0] * ir0[x] + r.w[1] * ir1[x] +
                         r.w[2] * ir2[x] + r.w[3] * ir3[x] +
                         r.w[4] * ir4[x] + r.w[5] * ir5[x];

            int idx = (int)((temp - smoothedMin) * invRange);
            if (idx < 0) idx = 0;
            else if (idx > 255) idx = 255;

            destRow[x] = palette[idx];
        }
    }
}

// -----------------------------------------------------------------------------
// 4. TAAU (Temporal Anti-Aliasing Upsampling)
// -----------------------------------------------------------------------------
void ThermalUpscaler::computeNeighborhoodMinMax(
    const float* lowGrid,
    int w,
    int h,
    float* outMin,
    float* outMax
) {
    for (int y = 0; y < h; ++y) {
        int yStart = (y > 0) ? (y - 1) : 0;
        int yEnd   = (y < h - 1) ? (y + 1) : (h - 1);

        for (int x = 0; x < w; ++x) {
            int xStart = (x > 0) ? (x - 1) : 0;
            int xEnd   = (x < w - 1) ? (x + 1) : (w - 1);

            float localMin = lowGrid[y * w + x];
            float localMax = localMin;

            for (int ny = yStart; ny <= yEnd; ++ny) {
                const float* row = &lowGrid[ny * w];
                for (int nx = xStart; nx <= xEnd; ++nx) {
                    float val = row[nx];
                    if (val < localMin) localMin = val;
                    if (val > localMax) localMax = val;
                }
            }
            int idx = y * w + x;
            outMin[idx] = localMin;
            outMax[idx] = localMax;
        }
    }
}

void ThermalUpscaler::estimateMotion(
    const float* curLow,
    const float* prevLow,
    int w,
    int h,
    float& outDx,
    float& outDy
) {
    float ssd[5][5];
    float minSSD = 1e30f;
    int bestU = 2;
    int bestV = 2;

    const int margin = 2;
    for (int vIdx = 0; vIdx < 5; ++vIdx) {
        int v = vIdx - 2;
        for (int uIdx = 0; uIdx < 5; ++uIdx) {
            int u = uIdx - 2;

            float curSSD = 0.0f;
            for (int y = margin; y < h - margin; ++y) {
                const float* curRow = &curLow[y * w];
                const float* prevRow = &prevLow[(y + v) * w + u];

                for (int x = margin; x < w - margin; ++x) {
                    float diff = curRow[x] - prevRow[x];
                    curSSD += diff * diff;
                }
            }

            ssd[vIdx][uIdx] = curSSD;
            if (curSSD < minSSD) {
                minSSD = curSSD;
                bestU = uIdx;
                bestV = vIdx;
            }
        }
    }
    float deltaU = 0.0f;
    if (bestU > 0 && bestU < 4) {
        float eLeft  = ssd[bestV][bestU - 1];
        float eMid   = ssd[bestV][bestU];
        float eRight = ssd[bestV][bestU + 1];
        float denomX = 2.0f * (eLeft - 2.0f * eMid + eRight);
        if (denomX > 1e-4f) {
            deltaU = (eLeft - eRight) / denomX;
            if (deltaU < -0.5f) deltaU = -0.5f;
            else if (deltaU > 0.5f) deltaU = 0.5f;
        }
    }
    float deltaV = 0.0f;
    if (bestV > 0 && bestV < 4) {
        float eTop = ssd[bestV - 1][bestU];
        float eMid = ssd[bestV][bestU];
        float eBot = ssd[bestV + 1][bestU];
        float denomY = 2.0f * (eTop - 2.0f * eMid + eBot);
        if (denomY > 1e-4f) {
            deltaV = (eTop - eBot) / denomY;
            if (deltaV < -0.5f) deltaV = -0.5f;
            else if (deltaV > 0.5f) deltaV = 0.5f;
        }
    }
    float dx = (float)(bestU - 2) + deltaU;
    float dy = (float)(bestV - 2) + deltaV;

    if (dx < -3.0f) dx = -3.0f;
    else if (dx > 3.0f) dx = 3.0f;

    if (dy < -3.0f) dy = -3.0f;
    else if (dy > 3.0f) dy = 3.0f;

    outDx = dx;
    outDy = dy;
}

void ThermalUpscaler::renderTAAU(
    const float* lowGrid,
    uint16_t* canvasBuf,
    int vpX, int vpY, int vpW, int vpH, int canvasW,
    float smoothedMin, float invRange, const uint16_t* palette,
    float alpha, float sharpness
) {
    if (m_histA == nullptr || m_histB == nullptr || m_intermediateRows == nullptr) {
        return;
    }

    // 0. Input Sanitization: Ensure transient sensor NaNs or infs never enter the pipeline
    float cleanGrid[SENSOR_PIXELS];
    for (int i = 0; i < m_gridW * m_gridH; ++i) {
        float v = lowGrid[i];
        cleanGrid[i] = (std::isfinite(v) && v > -50.0f && v < 400.0f) ? v : 25.0f;
    }

    // 1. Sub-pixel Motion Registration
    float dx = 0.0f;
    float dy = 0.0f;
    if (m_historyValid) {
        estimateMotion(cleanGrid, m_prevLow, m_gridW, m_gridH, dx, dy);
    }
    float scaleX = (float)(vpW - 1) / (float)(m_gridW - 1);
    float scaleY = (float)(vpH - 1) / (float)(m_gridH - 1);
    float dxHigh = dx * scaleX;
    float dyHigh = dy * scaleY;

    // High-res integer displacement: 1 display pixel is ~0.10 sensor pixels, which preserves sub-pixel accuracy
    int shiftX = (int)roundf(dxHigh);
    int shiftY = (int)roundf(dyHigh);

    // 2. Anti-Ghosting Bounding Box on low-res grid
    computeNeighborhoodMinMax(cleanGrid, m_gridW, m_gridH, m_lowMin, m_lowMax);

    // 3. Separable Bicubic Horizontal Pass into intermediate rows (internal SRAM)
    for (int r = 0; r < m_gridH; ++r) {
        const float* srcRow = &cleanGrid[r * m_gridW];
        float* dstRow = &m_intermediateRows[r * vpW];

        for (int x = 0; x < vpW; ++x) {
            const CubicInterp& c = m_colCubicTable[x];
            dstRow[x] = c.w0 * srcRow[c.idx0] + c.w1 * srcRow[c.idx1] +
                        c.w2 * srcRow[c.idx2] + c.w3 * srcRow[c.idx3];
        }
    }
    // Reset bounding box row caches
    m_cachedMinY0 = -1;
    m_cachedMinY1 = -1;

    // 4. Single-Pass Streaming: Accumulate Line Y and immediately sharpen Line Y-1 via 3-line SRAM buffer
    auto sharpenAndOutputRow = [&](int rowIdx) {
        if (rowIdx < 0 || rowIdx >= vpH) return;

        int rowPrev = (rowIdx > 0) ? (rowIdx - 1) : 0;
        int rowNext = (rowIdx < vpH - 1) ? (rowIdx + 1) : (vpH - 1);

        const float* rCurr  = m_lineBuf[rowIdx % 3];
        const float* rNorth = m_lineBuf[rowPrev % 3];
        const float* rSouth = m_lineBuf[rowNext % 3];

        uint16_t* destRow = &canvasBuf[(vpY + rowIdx) * canvasW + vpX];
        float sharpGain = sharpness * 1.5f;

        for (int x = 0; x < vpW; ++x) {
            int xLeft  = (x > 0) ? (x - 1) : 0;
            int xRight = (x < vpW - 1) ? (x + 1) : (vpW - 1);

            float C = rCurr[x];
            float N = rNorth[x];
            float S = rSouth[x];
            float W = rCurr[xLeft];
            float E = rCurr[xRight];

            // Fast, unconditionally stable unsharp mask (detail = C - 4-neighbor blur)
            float detail = C - 0.25f * (N + S + W + E);
            if (detail > 2.0f) detail = 2.0f;
            else if (detail < -2.0f) detail = -2.0f;

            float tSharp = C + sharpGain * detail;

            int idx = (int)((tSharp - smoothedMin) * invRange);
            if (idx < 0) idx = 0;
            else if (idx > 255) idx = 255;

            destRow[x] = palette[idx];
        }
    };

    for (int y = 0; y < vpH; ++y) {
        // Vertical bicubic inputs from SRAM
        const CubicInterp& rCubic = m_rowCubicTable[y];
        const float* ir0 = &m_intermediateRows[rCubic.idx0 * vpW];
        const float* ir1 = &m_intermediateRows[rCubic.idx1 * vpW];
        const float* ir2 = &m_intermediateRows[rCubic.idx2 * vpW];
        const float* ir3 = &m_intermediateRows[rCubic.idx3 * vpW];
        float rw0 = rCubic.w0, rw1 = rCubic.w1, rw2 = rCubic.w2, rw3 = rCubic.w3;

        // Bounding box row cache update (only recomputed when row indices change)
        const RowInterp& rBilin = m_rowBilinearTable[y];
        if (rBilin.y0 != m_cachedMinY0 || rBilin.y1 != m_cachedMinY1) {
            m_cachedMinY0 = rBilin.y0;
            m_cachedMinY1 = rBilin.y1;
            const float* rawMin0 = &m_lowMin[rBilin.y0 * m_gridW];
            const float* rawMin1 = &m_lowMin[rBilin.y1 * m_gridW];
            const float* rawMax0 = &m_lowMax[rBilin.y0 * m_gridW];
            const float* rawMax1 = &m_lowMax[rBilin.y1 * m_gridW];

            for (int x = 0; x < vpW; ++x) {
                const ColInterp& cb = m_colBilinearTable[x];
                m_interMin0[x] = rawMin0[cb.x0] + cb.fx * (rawMin0[cb.x1] - rawMin0[cb.x0]);
                m_interMin1[x] = rawMin1[cb.x0] + cb.fx * (rawMin1[cb.x1] - rawMin1[cb.x0]);
                m_interMax0[x] = rawMax0[cb.x0] + cb.fx * (rawMax0[cb.x1] - rawMax0[cb.x0]);
                m_interMax1[x] = rawMax1[cb.x0] + cb.fx * (rawMax1[cb.x1] - rawMax1[cb.x0]);
            }
        }
        float omfy = 1.0f - rBilin.fy;
        float fy = rBilin.fy;

        // Vertical history reprojection (single sequential row from PSRAM)
        int srcY = y - shiftY;
        if (srcY < 0) srcY = 0;
        else if (srcY >= vpH) srcY = vpH - 1;
        const float* histRow = &m_histRead[srcY * vpW];

        float* outHistRow = &m_histWrite[y * vpW];
        float* curLine = m_lineBuf[y % 3];

        for (int x = 0; x < vpW; ++x) {
            float curHigh = rw0 * ir0[x] + rw1 * ir1[x] + rw2 * ir2[x] + rw3 * ir3[x];

            if (!m_historyValid) {
                outHistRow[x] = curHigh;
                curLine[x] = curHigh;
                continue;
            }

            int srcX = x - shiftX;
            if (srcX < 0) srcX = 0;
            else if (srcX >= vpW) srcX = vpW - 1;

            float hVal = histRow[srcX];

            float highMin = omfy * m_interMin0[x] + fy * m_interMin1[x] - 0.3f;
            float highMax = omfy * m_interMax0[x] + fy * m_interMax1[x] + 0.3f;

            if (hVal < highMin) hVal = highMin;
            else if (hVal > highMax) hVal = highMax;

            float accum = (1.0f - alpha) * curHigh + alpha * hVal;

            outHistRow[x] = accum;
            curLine[x] = accum;
        }

        // Sharpen and output row y - 1 as soon as line y is in the cache
        if (y > 0) {
            sharpenAndOutputRow(y - 1);
        }
    }
    // Output final row
    sharpenAndOutputRow(vpH - 1);

    // Update state for next frame
    memcpy(m_prevLow, cleanGrid, m_gridW * m_gridH * sizeof(float));
    std::swap(m_histRead, m_histWrite);
    m_historyValid = true;
}


