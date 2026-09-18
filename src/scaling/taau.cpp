#include "scaling.hpp"
#include <cmath>
#include <cstring>
#include <utility>

// -----------------------------------------------------------------------------
// TAAU (Temporal Anti-Aliasing Upsampling)
// -----------------------------------------------------------------------------
void ThermalScaler::computeNeighborhoodMinMax(
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

void ThermalScaler::estimateMotion(
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

void ThermalScaler::renderTAAU(
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

    // Camera-pan attenuation:
    // Deadband: Sensor micro-jitter and hand tremor (<= 0.50 sensor pixels) retains 100% alpha.
    // Rapid camera sweeps (> 0.50 sensor pixels) smoothly fade alpha to 0 so fast pans fall back cleanly to Bicubic.
    float motionDist = sqrtf(dx * dx + dy * dy);
    float panFactor = (motionDist <= 0.50f) ? 1.0f : (1.0f - (motionDist - 0.50f) * 0.67f);
    if (panFactor < 0.0f) panFactor = 0.0f;
    float baseAlpha = alpha * panFactor;

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

            float highMin = omfy * m_interMin0[x] + fy * m_interMin1[x] - 0.15f;
            float highMax = omfy * m_interMax0[x] + fy * m_interMax1[x] + 0.15f;

            if (hVal < highMin) hVal = highMin;
            else if (hVal > highMax) hVal = highMax;

            // Dynamic Disocclusion / Temporal Difference Rejection (Anti-Ghosting Engine)
            // Sensor noise floor stays well under 1.00°C -> retains 100% full temporal accumulation.
            // Rapid motion / moving edges (delta >= 1.00°C to 3.00°C) smoothly drop history weight to 0,
            // purging ghost trails in 1 frame without degrading static super-resolution clarity.
            float diff = fabsf(curHigh - hVal);
            float disocclusionWeight = 1.0f - (diff - 1.00f) * 0.50f;
            if (disocclusionWeight < 0.0f) disocclusionWeight = 0.0f;
            else if (disocclusionWeight > 1.0f) disocclusionWeight = 1.0f;

            float effAlpha = baseAlpha * disocclusionWeight;
            float accum = (1.0f - effAlpha) * curHigh + effAlpha * hVal;

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

