#pragma once
#include <Arduino.h>
#include "config.hpp"
#include "thermal_types.hpp"
#include "ui.hpp"

// Precomputed 1D Catmull-Rom cubic interpolation weights (4 taps)


// Precomputed 1D Catmull-Rom cubic interpolation weights (4 taps)
struct CubicInterp {
    int idx0;
    int idx1;
    int idx2;
    int idx3;
    float w0;
    float w1;
    float w2;
    float w3;
};
// Precomputed 1D Lanczos-3 interpolation weights (6 taps)
struct LanczosInterp {
    int idx[6];
    float w[6];
};
// Precomputed horizontal history warp coordinates for TAAU
struct WarpColInterp {
    int hx0;
    int hx1;
    float hfx;
    float homfx;
};

class ThermalUpscaler {
public:
    ThermalUpscaler();
    ~ThermalUpscaler();

    // Allocate PSRAM history buffers and intermediate line caches
    bool init(int maxW = DISPLAY_WIDTH, int maxH = DISPLAY_HEIGHT);

    // Reset temporal history (e.g. on rotation or scaling mode switch)
    void reset();

    // Reconfigure viewport dimensions and precompute interpolation tables for all scaling modes
    void reconfigureViewport(int vpW, int vpH, int gridW, int gridH);

    // Unified render entry point for all scaling modes:
    // - SCALING_BILINEAR: Fast standard bilinear filtering
    // - SCALING_BICUBIC: Separable 4-tap Catmull-Rom cubic spline interpolation
    // - SCALING_LANCZOS: Separable 6-tap Lanczos-3 sinc-windowed reconstruction
    // - SCALING_TAAU: Temporal Anti-Aliasing Upsampling (motion estimation, reprojection, anti-ghosting, CAS)
    void render(
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
        float taauAlpha = 0.80f,
        float taauSharpness = 0.35f
    );

private:
    int m_maxW;
    int m_maxH;
    int m_vpW;
    int m_vpH;
    int m_gridW;
    int m_gridH;

    // Ping-pong history buffers in PSRAM for TAAU (320x240 floats each = ~307 KB)
    float* m_histA;
    float* m_histB;
    float* m_histRead;
    float* m_histWrite;
    bool m_historyValid;

    // Previous low-resolution frame for TAAU motion estimation
    float m_prevLow[SENSOR_PIXELS];

    // Intermediate horizontal row buffer for separable bicubic/lanczos (32 rows x 320 cols = 40 KB)
    float* m_intermediateRows;

    // Low-resolution 3x3 bounding box buffers for TAAU anti-ghosting
    float m_lowMin[SENSOR_PIXELS];
    float m_lowMax[SENSOR_PIXELS];

    // 1D Cubic precomputed tables (Catmull-Rom)
    CubicInterp m_colCubicTable[DISPLAY_WIDTH];
    CubicInterp m_rowCubicTable[DISPLAY_HEIGHT];

    // 1D Lanczos-3 precomputed tables (6 taps)
    LanczosInterp m_colLanczosTable[DISPLAY_WIDTH];
    LanczosInterp m_rowLanczosTable[DISPLAY_HEIGHT];

    // 1D Nearest Neighbor precomputed tables
    uint8_t m_colNNTable[DISPLAY_WIDTH];
    uint8_t m_rowNNTable[DISPLAY_HEIGHT];

    // 1D Bilinear precomputed tables
    ColInterp m_colBilinearTable[DISPLAY_WIDTH];
    RowInterp m_rowBilinearTable[DISPLAY_HEIGHT];

    // Fast line buffers for single-pass CAS sharpening (3 lines in internal SRAM)
    float m_lineBuf[3][DISPLAY_WIDTH];

    // Precomputed horizontal history warp table for TAAU
    WarpColInterp m_warpColTable[DISPLAY_WIDTH];

    // Intermediate horizontal row buffers for bounding box clamping
    float m_interMin0[DISPLAY_WIDTH];
    float m_interMin1[DISPLAY_WIDTH];
    float m_interMax0[DISPLAY_WIDTH];
    float m_interMax1[DISPLAY_WIDTH];
    int m_cachedMinY0;
    int m_cachedMinY1;


    // Rendering implementations
    void renderNearest(
        const float* lowGrid,
        uint16_t* canvasBuf,
        int vpX, int vpY, int vpW, int vpH, int canvasW,
        float smoothedMin, float invRange, const uint16_t* palette
    );
    void renderBilinear(
        const float* lowGrid,
        uint16_t* canvasBuf,
        int vpX, int vpY, int vpW, int vpH, int canvasW,
        float smoothedMin, float invRange, const uint16_t* palette
    );
    void renderBicubic(
        const float* lowGrid,
        uint16_t* canvasBuf,
        int vpX, int vpY, int vpW, int vpH, int canvasW,
        float smoothedMin, float invRange, const uint16_t* palette
    );
    void renderLanczos(
        const float* lowGrid,
        uint16_t* canvasBuf,
        int vpX, int vpY, int vpW, int vpH, int canvasW,
        float smoothedMin, float invRange, const uint16_t* palette
    );
    void renderTAAU(
        const float* lowGrid,
        uint16_t* canvasBuf,
        int vpX, int vpY, int vpW, int vpH, int canvasW,
        float smoothedMin, float invRange, const uint16_t* palette,
        float alpha, float sharpness
    );
    // TAAU helper functions
    void estimateMotion(
        const float* curLow,
        const float* prevLow,
        int w, int h,
        float& outDx, float& outDy
    );
    void computeNeighborhoodMinMax(
        const float* lowGrid,
        int w, int h,
        float* outMin, float* outMax
    );
};
