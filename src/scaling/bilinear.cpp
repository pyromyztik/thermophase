#include "scaling.hpp"

// --------------------------------------------------------------------------
// Bilinear Interpolation (Precomputed 1D Coordinate Tables)
// --------------------------------------------------------------------------
void ThermalScaler::renderBilinear(
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

