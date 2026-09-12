#include "scaling.hpp"

// --------------------------------------------------------------------------
// Separable Lanczos-3 Interpolation (6 taps)
// --------------------------------------------------------------------------
void ThermalScaler::renderLanczos(
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
    float offset = smoothedMin * invRange;
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

            int idx = (int)(temp * invRange - offset);
            if (idx < 0) idx = 0;
            else if (idx > 255) idx = 255;

            destRow[x] = palette[idx];
        }
    }
}

