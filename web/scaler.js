// ==============================================================================
// THERMOPHASE | High-Performance Thermal Scaler & TAAU Super-Resolution Engine
// ==============================================================================

// --- Colormaps & Palette LUT Generator Matching Firmware 1:1 ---
function createPaletteLut(stops) {
  const lut = new Uint32Array(256);
  const numKeys = stops.length;
  for (let i = 0; i < 256; i++) {
    const pos = i / 255.0;
    let r = 0, g = 0, b = 0;
    for (let k = 0; k < numKeys - 1; k++) {
      if (pos >= stops[k][0] && pos <= stops[k + 1][0]) {
        const segmentSpan = stops[k + 1][0] - stops[k][0];
        const localRatio = segmentSpan > 0 ? (pos - stops[k][0]) / segmentSpan : 0;
        r = Math.floor(stops[k][1] + localRatio * (stops[k + 1][1] - stops[k][1]));
        g = Math.floor(stops[k][2] + localRatio * (stops[k + 1][2] - stops[k][2]));
        b = Math.floor(stops[k][3] + localRatio * (stops[k + 1][3] - stops[k][3]));
        break;
      }
    }
    lut[i] = (255 << 24) | (b << 16) | (g << 8) | r;
  }
  return lut;
}

function createWhiteHotLut() {
  const lut = new Uint32Array(256);
  for (let i = 0; i < 256; i++) {
    lut[i] = (255 << 24) | (i << 16) | (i << 8) | i;
  }
  return lut;
}

const PALETTES = {
  IRONBOW: createPaletteLut([
    [0.00,   0,   0,  32],
    [0.20,  80,   0, 130],
    [0.45, 210,  20,  20],
    [0.70, 255, 140,   0],
    [0.88, 255, 235,  30],
    [1.00, 255, 255, 255]
  ]),
  WHITE_HOT: createWhiteHotLut(),
  RAINBOW: createPaletteLut([
    [0.00,   0,   0, 255],
    [0.25,   0, 255, 255],
    [0.50,   0, 255,   0],
    [0.75, 255, 255,   0],
    [1.00, 255,   0,   0]
  ]),
  BONE: createWhiteHotLut(),
  INFERNO: createPaletteLut([
    [0.00,   0,   0,   4],
    [0.15,  30,  12,  60],
    [0.35,  87,  16, 110],
    [0.55, 155,  41, 100],
    [0.75, 221, 100,  44],
    [0.90, 252, 190,  60],
    [1.00, 252, 255, 164]
  ]),
  TURBO: createPaletteLut([
    [0.00,  48,  18,  59],
    [0.20,  70, 134, 251],
    [0.40,  27, 229, 181],
    [0.60, 164, 252,  60],
    [0.80, 251, 136,  36],
    [1.00, 122,   4,   3]
  ])
};

// --- 3x3 Peak-Preserving Defect-Rejecting Median Denoise ---
function filterMedian(src, dst, threshold = 2.5, w = 24, h = 32) {
  const win = new Float32Array(9);
  for (let r = 0; r < h; ++r) {
    const rOff = r * w;
    for (let c = 0; c < w; ++c) {
      let count = 0;
      const cVal = src[rOff + c];
      for (let dr = -1; dr <= 1; ++dr) {
        const nr = r + dr;
        if (nr < 0 || nr >= h) continue;
        const nOff = nr * w;
        for (let dc = -1; dc <= 1; ++dc) {
          const nc = c + dc;
          if (nc >= 0 && nc < w) win[count++] = src[nOff + nc];
        }
      }
      for (let i = 1; i < count; i++) {
        const k = win[i]; let j = i - 1;
        while (j >= 0 && win[j] > k) { win[j + 1] = win[j]; j--; }
        win[j + 1] = k;
      }
      const med = win[Math.floor(count / 2)];
      const diff = cVal - med;

      if (diff > threshold) {
        let isHotDefect = false;
        if (diff > 12.0) {
          let anyWarm = false;
          for (let dr = -1; dr <= 1 && !anyWarm; ++dr) {
            const nr = r + dr;
            if (nr < 0 || nr >= h) continue;
            for (let dc = -1; dc <= 1 && !anyWarm; ++dc) {
              if (dr === 0 && dc === 0) continue;
              const nc = c + dc;
              if (nc >= 0 && nc < w && src[nr * w + nc] > med + 0.6) anyWarm = true;
            }
          }
          if (!anyWarm) isHotDefect = true;
        }
        dst[rOff + c] = isHotDefect ? med : cVal;
      } else if (diff < -threshold) {
        let confirmedCold = false;
        for (let dr = -1; dr <= 1 && !confirmedCold; ++dr) {
          const nr = r + dr;
          if (nr < 0 || nr >= h) continue;
          for (let dc = -1; dc <= 1 && !confirmedCold; ++dc) {
            if (dr === 0 && dc === 0) continue;
            const nc = c + dc;
            if (nc >= 0 && nc < w && src[nr * w + nc] < med - 0.6) confirmedCold = true;
          }
        }
        dst[rOff + c] = confirmedCold ? cVal : med;
      } else {
        dst[rOff + c] = med;
      }
    }
  }
}

// --- High-Performance Thermal Scaler with True TAAU Super-Resolution ---
class ThermalScaler {
  constructor() {
    this.vpW = 0; this.vpH = 0; this.gridW = 0; this.gridH = 0;
    this.colNN = null; this.rowNN = null;
    this.colBilin = null; this.rowBilin = null;
    this.colCubic = null; this.rowCubic = null;
    this.interRows = null;

    // TAAU Temporal Buffers & State
    this.historyValid = false;
    this.prevLow = null;
    this.histA = null;
    this.histB = null;
    this.histRead = null;
    this.histWrite = null;
    this.lowMin = null;
    this.lowMax = null;
    this.interMin0 = null;
    this.interMin1 = null;
    this.interMax0 = null;
    this.interMax1 = null;
    this.cachedMinY0 = -1;
    this.cachedMinY1 = -1;

    // Line cache for unsharp mask streaming
    this.lineBuf0 = null;
    this.lineBuf1 = null;
    this.lineBuf2 = null;
  }

  resetHistory() {
    this.historyValid = false;
    if (this.histA) this.histA.fill(0);
    if (this.histB) this.histB.fill(0);
    if (this.prevLow) this.prevLow.fill(0);
    this.cachedMinY0 = -1;
    this.cachedMinY1 = -1;
  }

  reconfigure(vpW, vpH, gridW, gridH) {
    if (this.vpW === vpW && this.vpH === vpH && this.gridW === gridW && this.gridH === gridH) return;
    this.vpW = vpW; this.vpH = vpH; this.gridW = gridW; this.gridH = gridH;

    const totalHigh = vpW * vpH;
    const totalLow = gridW * gridH;
    this.interRows = new Float32Array(gridH * vpW);

    this.histA = new Float32Array(totalHigh);
    this.histB = new Float32Array(totalHigh);
    this.histRead = this.histA;
    this.histWrite = this.histB;
    this.prevLow = new Float32Array(totalLow);
    this.lowMin = new Float32Array(totalLow);
    this.lowMax = new Float32Array(totalLow);

    this.interMin0 = new Float32Array(vpW);
    this.interMin1 = new Float32Array(vpW);
    this.interMax0 = new Float32Array(vpW);
    this.interMax1 = new Float32Array(vpW);

    this.lineBuf0 = new Float32Array(vpW);
    this.lineBuf1 = new Float32Array(vpW);
    this.lineBuf2 = new Float32Array(vpW);

    this.historyValid = false;
    this.cachedMinY0 = -1;
    this.cachedMinY1 = -1;

    this.colNN = new Uint8Array(vpW);
    for (let x = 0; x < vpW; ++x) this.colNN[x] = Math.min(gridW - 1, Math.max(0, Math.floor(x * gridW / vpW)));
    this.rowNN = new Uint8Array(vpH);
    for (let y = 0; y < vpH; ++y) this.rowNN[y] = Math.min(gridH - 1, Math.max(0, Math.floor(y * gridH / vpH)));

    this.colBilin = new Array(vpW);
    for (let x = 0; x < vpW; ++x) {
      const u = x * (gridW - 1) / (vpW - 1);
      const x0 = Math.floor(u);
      this.colBilin[x] = { x0, x1: Math.min(gridW - 1, x0 + 1), fx: u - x0 };
    }
    this.rowBilin = new Array(vpH);
    for (let y = 0; y < vpH; ++y) {
      const v = y * (gridH - 1) / (vpH - 1);
      const y0 = Math.floor(v);
      this.rowBilin[y] = { y0, y1: Math.min(gridH - 1, y0 + 1), fy: v - y0 };
    }

    this.colCubic = new Array(vpW);
    for (let x = 0; x < vpW; ++x) {
      const u = x * (gridW - 1) / (vpW - 1);
      const x1 = Math.min(gridW - 1, Math.floor(u));
      const t = u - x1;
      const x0 = (x1 > 0) ? x1 - 1 : 0;
      const x2 = Math.min(gridW - 1, x1 + 1);
      const x3 = Math.min(gridW - 1, x2 + 1);
      const t2 = t * t, t3 = t2 * t;
      this.colCubic[x] = {
        i0: x0, i1: x1, i2: x2, i3: x3,
        w0: -0.5*t3 + t2 - 0.5*t, w1: 1.5*t3 - 2.5*t2 + 1,
        w2: -1.5*t3 + 2*t2 + 0.5*t, w3: 0.5*t3 - 0.5*t2
      };
    }
    this.rowCubic = new Array(vpH);
    for (let y = 0; y < vpH; ++y) {
      const v = y * (gridH - 1) / (vpH - 1);
      const y1 = Math.min(gridH - 1, Math.floor(v));
      const t = v - y1;
      const y0 = (y1 > 0) ? y1 - 1 : 0;
      const y2 = Math.min(gridH - 1, y1 + 1);
      const y3 = Math.min(gridH - 1, y2 + 1);
      const t2 = t * t, t3 = t2 * t;
      this.rowCubic[y] = {
        i0: y0, i1: y1, i2: y2, i3: y3,
        w0: -0.5*t3 + t2 - 0.5*t, w1: 1.5*t3 - 2.5*t2 + 1,
        w2: -1.5*t3 + 2*t2 + 0.5*t, w3: 0.5*t3 - 0.5*t2
      };
    }
  }

  estimateMotion(curLow, prevLow, w, h) {
    let minSSD = 1e30;
    let bestU = 2, bestV = 2;
    const ssd = new Float32Array(25);
    const margin = 2;

    for (let vIdx = 0; vIdx < 5; ++vIdx) {
      const v = vIdx - 2;
      for (let uIdx = 0; uIdx < 5; ++uIdx) {
        const u = uIdx - 2;
        let curSSD = 0;
        for (let y = margin; y < h - margin; ++y) {
          const curRow = y * w;
          const prevRow = (y + v) * w + u;
          for (let x = margin; x < w - margin; ++x) {
            const diff = curLow[curRow + x] - prevLow[prevRow + x];
            curSSD += diff * diff;
          }
        }
        ssd[vIdx * 5 + uIdx] = curSSD;
        if (curSSD < minSSD) {
          minSSD = curSSD;
          bestU = uIdx;
          bestV = vIdx;
        }
      }
    }

    let deltaU = 0;
    if (bestU > 0 && bestU < 4) {
      const eLeft  = ssd[bestV * 5 + (bestU - 1)];
      const eMid   = ssd[bestV * 5 + bestU];
      const eRight = ssd[bestV * 5 + (bestU + 1)];
      const denomX = 2 * (eLeft - 2 * eMid + eRight);
      if (denomX > 1e-4) {
        deltaU = Math.max(-0.5, Math.min(0.5, (eLeft - eRight) / denomX));
      }
    }

    let deltaV = 0;
    if (bestV > 0 && bestV < 4) {
      const eTop = ssd[(bestV - 1) * 5 + bestU];
      const eMid = ssd[bestV * 5 + bestU];
      const eBot = ssd[(bestV + 1) * 5 + bestU];
      const denomY = 2 * (eTop - 2 * eMid + eBot);
      if (denomY > 1e-4) {
        deltaV = Math.max(-0.5, Math.min(0.5, (eTop - eBot) / denomY));
      }
    }

    const dx = Math.max(-3.0, Math.min(3.0, (bestU - 2) + deltaU));
    const dy = Math.max(-3.0, Math.min(3.0, (bestV - 2) + deltaV));
    return { dx, dy };
  }

  computeNeighborhoodMinMax(grid, w, h) {
    for (let y = 0; y < h; ++y) {
      const yStart = (y > 0) ? (y - 1) : 0;
      const yEnd   = (y < h - 1) ? (y + 1) : (h - 1);
      for (let x = 0; x < w; ++x) {
        const xStart = (x > 0) ? (x - 1) : 0;
        const xEnd   = (x < w - 1) ? (x + 1) : (w - 1);
        let locMin = grid[y * w + x];
        let locMax = locMin;
        for (let ny = yStart; ny <= yEnd; ++ny) {
          const rowOff = ny * w;
          for (let nx = xStart; nx <= xEnd; ++nx) {
            const val = grid[rowOff + nx];
            if (val < locMin) locMin = val;
            if (val > locMax) locMax = val;
          }
        }
        const idx = y * w + x;
        this.lowMin[idx] = locMin;
        this.lowMax[idx] = locMax;
      }
    }
  }

  renderTAAU(grid, out32, minT, invRange, lut, alpha = 0.85, sharpness = 0.45) {
    const { vpW, vpH, gridW, gridH } = this;

    // 0. Input Sanitization
    const cleanGrid = new Float32Array(gridW * gridH);
    for (let i = 0; i < gridW * gridH; ++i) {
      const v = grid[i];
      cleanGrid[i] = (Number.isFinite(v) && v > -50 && v < 400) ? v : 25.0;
    }

    // 1. Sub-pixel Motion Registration
    let dx = 0, dy = 0;
    if (this.historyValid) {
      const motion = this.estimateMotion(cleanGrid, this.prevLow, gridW, gridH);
      dx = motion.dx;
      dy = motion.dy;
    }
    const scaleX = (vpW - 1) / (gridW - 1);
    const scaleY = (vpH - 1) / (gridH - 1);
    const dxHigh = dx * scaleX;
    const dyHigh = dy * scaleY;

    // Camera-pan attenuation (smoothly blend to bicubic during rapid sweeps)
    const motionDist = Math.hypot(dx, dy);
    const panFactor = (motionDist <= 0.50) ? 1.0 : Math.max(0.0, 1.0 - (motionDist - 0.50) * 0.67);
    const baseAlpha = alpha * panFactor;

    // 2. Anti-Ghosting Bounding Box on low-res grid
    this.computeNeighborhoodMinMax(cleanGrid, gridW, gridH);

    // 3. Horizontal Bicubic Pass into intermediate rows
    for (let r = 0; r < gridH; ++r) {
      const sOff = r * gridW;
      const dOff = r * vpW;
      for (let x = 0; x < vpW; ++x) {
        const c = this.colCubic[x];
        this.interRows[dOff + x] = c.w0 * cleanGrid[sOff + c.i0] + c.w1 * cleanGrid[sOff + c.i1] +
                                  c.w2 * cleanGrid[sOff + c.i2] + c.w3 * cleanGrid[sOff + c.i3];
      }
    }
    this.cachedMinY0 = -1;
    this.cachedMinY1 = -1;

    // Sliding line buffers for 3-line Laplacian unsharp masking
    const lines = [this.lineBuf0, this.lineBuf1, this.lineBuf2];

    const sharpenAndOutputRow = (rowIdx) => {
      if (rowIdx < 0 || rowIdx >= vpH) return;
      const rowPrev = (rowIdx > 0) ? (rowIdx - 1) : 0;
      const rowNext = (rowIdx < vpH - 1) ? (rowIdx + 1) : (vpH - 1);

      const rCurr  = lines[rowIdx % 3];
      const rNorth = lines[rowPrev % 3];
      const rSouth = lines[rowNext % 3];
      const dOff   = rowIdx * vpW;
      const sharpGain = sharpness * 1.5;

      for (let x = 0; x < vpW; ++x) {
        const xLeft  = (x > 0) ? (x - 1) : 0;
        const xRight = (x < vpW - 1) ? (x + 1) : (vpW - 1);

        const C = rCurr[x];
        const N = rNorth[x];
        const S = rSouth[x];
        const W = rCurr[xLeft];
        const E = rCurr[xRight];

        let detail = C - 0.25 * (N + S + W + E);
        if (detail > 2.0) detail = 2.0;
        else if (detail < -2.0) detail = -2.0;

        const tSharp = C + sharpGain * detail;
        let idx = Math.floor((tSharp - minT) * invRange);
        if (idx < 0) idx = 0;
        else if (idx > 255) idx = 255;
        out32[dOff + x] = lut[idx];
      }
    };

    // 4. Vertical Bicubic Pass + Continuous Subpixel History Reprojection + Clamping + Blending
    const histRead = this.histRead;
    const histWrite = this.histWrite;

    for (let y = 0; y < vpH; ++y) {
      const rc = this.rowCubic[y];
      const ir0 = rc.i0 * vpW, ir1 = rc.i1 * vpW, ir2 = rc.i2 * vpW, ir3 = rc.i3 * vpW;
      const rw0 = rc.w0, rw1 = rc.w1, rw2 = rc.w2, rw3 = rc.w3;

      // Update bounding box row caches
      const rb = this.rowBilin[y];
      if (rb.y0 !== this.cachedMinY0 || rb.y1 !== this.cachedMinY1) {
        this.cachedMinY0 = rb.y0;
        this.cachedMinY1 = rb.y1;
        const rMin0 = rb.y0 * gridW, rMin1 = rb.y1 * gridW;
        for (let x = 0; x < vpW; ++x) {
          const cb = this.colBilin[x];
          this.interMin0[x] = this.lowMin[rMin0 + cb.x0] + cb.fx * (this.lowMin[rMin0 + cb.x1] - this.lowMin[rMin0 + cb.x0]);
          this.interMin1[x] = this.lowMin[rMin1 + cb.x0] + cb.fx * (this.lowMin[rMin1 + cb.x1] - this.lowMin[rMin1 + cb.x0]);
          this.interMax0[x] = this.lowMax[rMin0 + cb.x0] + cb.fx * (this.lowMax[rMin0 + cb.x1] - this.lowMax[rMin0 + cb.x0]);
          this.interMax1[x] = this.lowMax[rMin1 + cb.x0] + cb.fx * (this.lowMax[rMin1 + cb.x1] - this.lowMax[rMin1 + cb.x0]);
        }
      }
      const omfy = 1.0 - rb.fy;
      const fy   = rb.fy;

      const dOff = y * vpW;
      const curLine = lines[y % 3];

      // Continuous sub-pixel vertical coordinate in history buffer
      const histY = Math.max(0, Math.min(vpH - 1, y - dyHigh));
      const hy0 = Math.floor(histY);
      const hy1 = Math.min(vpH - 1, hy0 + 1);
      const fhy = histY - hy0;
      const omfhy = 1.0 - fhy;
      const hy0Off = hy0 * vpW;
      const hy1Off = hy1 * vpW;

      for (let x = 0; x < vpW; ++x) {
        const curHigh = rw0 * this.interRows[ir0 + x] + rw1 * this.interRows[ir1 + x] +
                        rw2 * this.interRows[ir2 + x] + rw3 * this.interRows[ir3 + x];

        if (!this.historyValid) {
          histWrite[dOff + x] = curHigh;
          curLine[x] = curHigh;
          continue;
        }

        // Continuous sub-pixel horizontal coordinate in history buffer
        const histX = Math.max(0, Math.min(vpW - 1, x - dxHigh));
        const hx0 = Math.floor(histX);
        const hx1 = Math.min(vpW - 1, hx0 + 1);
        const fhx = histX - hx0;
        const omfhx = 1.0 - fhx;

        // Bilinear sample from history buffer
        const top = omfhx * histRead[hy0Off + hx0] + fhx * histRead[hy0Off + hx1];
        const bot = omfhx * histRead[hy1Off + hx0] + fhx * histRead[hy1Off + hx1];
        let hVal = omfhy * top + fhy * bot;

        // High-res bounding box clamping with ±0.15°C guard band
        const highMin = omfy * this.interMin0[x] + fy * this.interMin1[x] - 0.15;
        const highMax = omfy * this.interMax0[x] + fy * this.interMax1[x] + 0.15;
        if (hVal < highMin) hVal = highMin;
        else if (hVal > highMax) hVal = highMax;

        // Dynamic Disocclusion Rejection (anti-ghosting)
        const diff = Math.abs(curHigh - hVal);
        let disocclusionWeight = 1.0 - (diff - 1.00) * 0.50;
        if (disocclusionWeight < 0.0) disocclusionWeight = 0.0;
        else if (disocclusionWeight > 1.0) disocclusionWeight = 1.0;

        const effAlpha = baseAlpha * disocclusionWeight;
        const accum = (1.0 - effAlpha) * curHigh + effAlpha * hVal;

        histWrite[dOff + x] = accum;
        curLine[x] = accum;
      }

      if (y > 0) {
        sharpenAndOutputRow(y - 1);
      }
    }
    sharpenAndOutputRow(vpH - 1);

    // Update state for next frame
    this.prevLow.set(cleanGrid);
    this.histRead = histWrite;
    this.histWrite = histRead;
    this.historyValid = true;
  }

  // --- Standard Static Catmull-Rom Bicubic ---
  renderBicubic(grid, out32, minT, invRange, lut) {
    const { vpW, vpH, gridW, gridH } = this;
    for (let r = 0; r < gridH; ++r) {
      const sOff = r * gridW, dOff = r * vpW;
      for (let x = 0; x < vpW; ++x) {
        const c = this.colCubic[x];
        this.interRows[dOff + x] = c.w0 * grid[sOff + c.i0] + c.w1 * grid[sOff + c.i1] +
                                  c.w2 * grid[sOff + c.i2] + c.w3 * grid[sOff + c.i3];
      }
    }
    for (let y = 0; y < vpH; ++y) {
      const rc = this.rowCubic[y];
      const ir0 = rc.i0 * vpW, ir1 = rc.i1 * vpW, ir2 = rc.i2 * vpW, ir3 = rc.i3 * vpW;
      const w0 = rc.w0, w1 = rc.w1, w2 = rc.w2, w3 = rc.w3;
      const dOff = y * vpW;
      for (let x = 0; x < vpW; ++x) {
        const t = w0 * this.interRows[ir0 + x] + w1 * this.interRows[ir1 + x] +
                  w2 * this.interRows[ir2 + x] + w3 * this.interRows[ir3 + x];
        let idx = Math.floor((t - minT) * invRange);
        if (idx < 0) idx = 0; else if (idx > 255) idx = 255;
        out32[dOff + x] = lut[idx];
      }
    }
  }

  render(mode, grid, outImg, minT, invRange, lut, sharpness = 0.45) {
    const out32 = new Uint32Array(outImg.data.buffer);
    const { vpW, vpH, gridW } = this;

    if (mode === 'TAAU') {
      this.renderTAAU(grid, out32, minT, invRange, lut, 0.85, sharpness);
    } else {
      this.historyValid = false;
      if (mode === 'NEAREST') {
        for (let y = 0; y < vpH; ++y) {
          const row = this.rowNN[y] * gridW;
          const dOff = y * vpW;
          for (let x = 0; x < vpW; ++x) {
            const t = grid[row + this.colNN[x]];
            let idx = Math.floor((t - minT) * invRange);
            if (idx < 0) idx = 0; else if (idx > 255) idx = 255;
            out32[dOff + x] = lut[idx];
          }
        }
      } else if (mode === 'BILINEAR') {
        for (let y = 0; y < vpH; ++y) {
          const rb = this.rowBilin[y];
          const r0 = rb.y0 * gridW, r1 = rb.y1 * gridW;
          const fy = rb.fy, omfy = 1.0 - fy;
          const dOff = y * vpW;
          for (let x = 0; x < vpW; ++x) {
            const cb = this.colBilin[x];
            const v0 = grid[r0 + cb.x0] + cb.fx * (grid[r0 + cb.x1] - grid[r0 + cb.x0]);
            const v1 = grid[r1 + cb.x0] + cb.fx * (grid[r1 + cb.x1] - grid[r1 + cb.x0]);
            let idx = Math.floor(((omfy * v0 + fy * v1) - minT) * invRange);
            if (idx < 0) idx = 0; else if (idx > 255) idx = 255;
            out32[dOff + x] = lut[idx];
          }
        }
      } else {
        this.renderBicubic(grid, out32, minT, invRange, lut);
      }
    }
  }
}

