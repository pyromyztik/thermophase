#include "scaling.hpp"

// --------------------------------------------------------------------------
// Separable Catmull-Rom Bicubic Interpolation
// --------------------------------------------------------------------------
void ThermalScaler::renderBicubic(
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
        float rw0 = rCubic.w0, rw1 = rCubic.w1, 
              rw2 = rCubic.w2, rw3 = rCubic.w3;

        uint16_t* destRow = &canvasBuf[(vpY + y) * canvasW + vpX];

        for (int x = 0; x < vpW; ++x) {
            float temp = rw0 * ir0[x] + rw1 * ir1[x] 
                       + rw2 * ir2[x] + rw3 * ir3[x];

            int idx = (int)((temp - smoothedMin) * invRange);
            if (idx < 0) idx = 0;
            else if (idx > 255) idx = 255;

            destRow[x] = palette[idx];
        }
    }
}

