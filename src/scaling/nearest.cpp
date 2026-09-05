#include "scaling.hpp"

// -----------------------------------------------------------------------------
// Nearest Neighbor Interpolation (Integer LUT Pixel Replication)
// -----------------------------------------------------------------------------
void ThermalScaler::renderNearest(
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

