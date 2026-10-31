// ==============================================================================
// THERMOPHASE | Web Application, Streaming, Recording & Terminal Controller
// ==============================================================================

// --- DOM & Canvas Elements ---
const canvas = document.getElementById('thermalCanvas');
const ctx = canvas.getContext('2d');
const colorbar = document.getElementById('colorbarCanvas');
const cbCtx = colorbar.getContext('2d');
const reticle = document.getElementById('reticle');
const vpWrapper = document.getElementById('viewportWrapper');

// --- Scaler Instance & State ---
const scaler = new ThermalScaler();
const PALETTE_ID_MAP = ['IRONBOW', 'WHITE_HOT', 'RAINBOW'];
let currentPalette = 'IRONBOW';
let currentScaling = 'TAAU';
let currentSharpness = 0.45;
let currentRotation = 90;
let liveDenoise = true;
let flipVertical = true;
let currentRangeMode = 0;
let currentManualMin = 20.0;
let currentManualMax = 50.0;
let currentEmissivity = 0.95;

let displayImgData = null;
let smoothedCenter = null;
let lastProbeTemp = null;
let probeX = 12, probeY = 16, isHovering = false;
let lastEffMin = 20.0, lastEffMax = 35.0;

let frameCount = 0, fps = 0, lastFpsT = performance.now();
let lastFrameTime = performance.now();
let lastPacketInterval = 0;

// --- Connection Diagnostics & Troubleshooting State ---
let isConnected = false;
let isReconnecting = false;
let reconnectCount = 0;
let connectionTrouble = null; // null | { title: string, sub: string, level: 'warn'|'err' }

// --- Stream State & Elements ---
let isStreaming = false;
const btnRecord = document.getElementById('btnRecord');
const btnCapture = document.getElementById('btnCapture');
const btnStreamToggle = document.getElementById('btnStreamToggle');
const btnOverlayStartStream = document.getElementById('btnOverlayStartStream');
const streamInactiveAlert = document.getElementById('streamInactiveAlert');
const feedBadge = document.getElementById('feedBadge');

// --- Viewport & Canvas Rendering Helpers ---
function drawPlaceholder(msg = 'Live Wi-Fi Streaming is Inactive') {
  ctx.fillStyle = '#000000';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = '#ff1e27';
  ctx.font = 'bold 16px monospace, sans-serif';
  ctx.textAlign = 'center';
  ctx.fillText('■ THERMOPHASE WI-FI STREAM', canvas.width / 2, canvas.height / 2 - 20);
  ctx.fillStyle = '#a0a5b2';
  ctx.font = '12px monospace, sans-serif';
  ctx.fillText(msg, canvas.width / 2, canvas.height / 2 + 15);
  ctx.fillStyle = '#00e676';
  ctx.font = '11px monospace, sans-serif';
  ctx.fillText('[Click "Start Stream" to begin streaming]', canvas.width / 2, canvas.height / 2 + 45);
}

function updateDimensions() {
  if (currentRotation === 90 || currentRotation === 270) {
    canvas.width = 480; canvas.height = 640; colorbar.height = 640;
    scaler.reconfigure(480, 640, 24, 32);
  } else {
    canvas.width = 640; canvas.height = 480; colorbar.height = 480;
    scaler.reconfigure(640, 480, 32, 24);
  }
  scaler.resetHistory();
  displayImgData = ctx.createImageData(canvas.width, canvas.height);
  drawColorbar();
  if (frameCount === 0) {
    drawPlaceholder();
  }
}

function drawColorbar() {
  const lut = PALETTES[currentPalette] || PALETTES.IRONBOW;
  const w = colorbar.width, h = colorbar.height;
  const img = cbCtx.createImageData(w, h);
  const d32 = new Uint32Array(img.data.buffer);
  for (let y = 0; y < h; y++) {
    const norm = Math.max(0, Math.min(255, Math.floor(((h - 1 - y) / (h - 1)) * 255)));
    const color = lut[norm];
    const off = y * w;
    for (let x = 0; x < w; x++) d32[off + x] = color;
  }
  cbCtx.putImageData(img, 0, 0);
}

function rotateGrid(src, angle) {
  const rawW = 32, rawH = 24;
  if (angle === 90) {
    const dst = new Float32Array(768);
    for (let y = 0; y < rawH; y++) {
      const row = y * rawW;
      for (let x = 0; x < rawW; x++) dst[x * rawH + (rawH - 1 - y)] = src[row + x];
    }
    return dst;
  } else if (angle === 180) {
    const dst = new Float32Array(768);
    for (let y = 0; y < rawH; y++) {
      const row = y * rawW;
      for (let x = 0; x < rawW; x++) dst[(rawH - 1 - y) * rawW + (rawW - 1 - x)] = src[row + x];
    }
    return dst;
  } else if (angle === 270) {
    const dst = new Float32Array(768);
    for (let y = 0; y < rawH; y++) {
      const row = y * rawW;
      for (let x = 0; x < rawW; x++) dst[(rawW - 1 - x) * rawH + y] = src[row + x];
    }
    return dst;
  }
  return src;
}

// --- Connection Trouble Overlay Burned onto Canvas Video Stream ---
function drawCanvasConnectionTrouble(trouble) {
  if (!trouble) return;
  const cw = canvas.width, ch = canvas.height;
  const boxW = Math.min(cw - 40, 360);
  const boxH = 80;
  const boxX = (cw - boxW) / 2;
  const boxY = (ch - boxH) / 2;

  // Dark solid background with crisp warning border matching firmware
  ctx.save();
  ctx.fillStyle = '#000000';
  ctx.fillRect(boxX, boxY, boxW, boxH);
  ctx.strokeStyle = trouble.level === 'err' ? '#ff1e27' : '#ffd600';
  ctx.lineWidth = 2;
  ctx.strokeRect(boxX, boxY, boxW, boxH);

  // Warning icon & title
  ctx.fillStyle = trouble.level === 'err' ? '#ff1e27' : '#ffd600';
  ctx.font = 'bold 14px monospace, sans-serif';
  ctx.textAlign = 'center';
  ctx.fillText('WARNING: ' + trouble.title, cw / 2, boxY + 32);

  // Subtitle / troubleshooting detail
  ctx.fillStyle = '#ffffff';
  ctx.font = '11px monospace, sans-serif';
  ctx.fillText(trouble.sub, cw / 2, boxY + 56);
  ctx.restore();
}

// Update DOM Connection Alert Banner
function updateStreamAlertUI() {
  const alertEl = document.getElementById('streamAlert');
  const alertTitle = document.getElementById('alertTitle');
  const alertSub = document.getElementById('alertSub');
  if (!alertEl) return;

  if (connectionTrouble) {
    alertEl.style.display = 'flex';
    alertTitle.textContent = connectionTrouble.title;
    alertSub.textContent = connectionTrouble.sub;
  } else {
    alertEl.style.display = 'none';
  }
}

// --- Frame Processing & Pipeline ---
function handleIncomingFrame(temps, minTemp, maxTemp, rangeMode) {
  const now = performance.now();
  lastPacketInterval = now - lastFrameTime;
  lastFrameTime = now;
  frameCount++;

  if (now - lastFpsT >= 1000) {
    fps = (frameCount * 1000) / (now - lastFpsT);
    frameCount = 0; lastFpsT = now;
    document.getElementById('hudFps').textContent = fps.toFixed(1) + ' FPS';
    document.getElementById('statFps').textContent = fps.toFixed(1) + ' FPS';
  }

  // 1. Genuine scene temperature extremes
  let sceneMin = 1000.0, sceneMax = -1000.0;
  for (let i = 0; i < 768; i++) {
    const t = temps[i];
    if (t < sceneMin) sceneMin = t;
    if (t > sceneMax) sceneMax = t;
  }
  if (sceneMin > 500) sceneMin = 20.0;
  if (sceneMax < -500) sceneMax = 35.0;

  // 2. Color Range Synchronization
  const isManual = (rangeMode === 1);
  if (rangeMode !== currentRangeMode) {
    updateRangeModeUI(rangeMode);
  }
  let effMin = minTemp;
  let effMax = maxTemp;
  let span = effMax - effMin;
  if (span < 0.5) span = 0.5;
  const invRange = 255.0 / span;

  let oriented = temps;
  if (flipVertical) {
    const fl = new Float32Array(768);
    for (let y = 0; y < 24; y++) {
      const sRow = (23 - y) * 32, dRow = y * 32;
      for (let x = 0; x < 32; x++) fl[dRow + x] = oriented[sRow + x];
    }
    oriented = fl;
  }

  const rotated = rotateGrid(oriented, currentRotation);
  const rotW = (currentRotation === 90 || currentRotation === 270) ? 24 : 32;
  const rotH = (currentRotation === 90 || currentRotation === 270) ? 32 : 24;

  let processed = rotated;
  if (liveDenoise) {
    const den = new Float32Array(768);
    filterMedian(rotated, den, 2.5, rotW, rotH);
    processed = den;
  }

  // Center spot
  const cx = Math.floor(rotW / 2), cy = Math.floor(rotH / 2);
  const cTemp = processed[cy * rotW + cx];
  if (smoothedCenter === null) smoothedCenter = cTemp;
  else smoothedCenter += 0.25 * (cTemp - smoothedCenter);

  // Probe spot
  const pTemp = processed[Math.min(rotH - 1, probeY) * rotW + Math.min(rotW - 1, probeX)];
  lastProbeTemp = pTemp;

  // Render canvas
  const lut = PALETTES[currentPalette] || PALETTES.IRONBOW;
  scaler.render(currentScaling, processed, displayImgData, effMin, invRange, lut, currentSharpness);
  ctx.putImageData(displayImgData, 0, 0);

  // Center crosshair
  const cw = canvas.width, ch = canvas.height;
  ctx.strokeStyle = 'rgba(255, 255, 255, 0.5)';
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(cw/2 - 8, ch/2); ctx.lineTo(cw/2 + 8, ch/2);
  ctx.moveTo(cw/2, ch/2 - 8); ctx.lineTo(cw/2, ch/2 + 8);
  ctx.stroke();

  // If there are connection issues (e.g. packet delay), burn onto canvas
  if (connectionTrouble) {
    drawCanvasConnectionTrouble(connectionTrouble);
  }

  // If currently recording video, composite canvas frame + color range bar onto recordCanvas side panel
  lastEffMin = effMin;
  lastEffMax = effMax;
  if (isRecordingActive) {
    const targetW = canvas.width + COLORBAR_PANEL_WIDTH;
    const targetH = canvas.height;
    if (recordCanvas.width !== targetW || recordCanvas.height !== targetH) {
      recordCanvas.width = targetW;
      recordCanvas.height = targetH;
    }
    // Draw original video at 0, 0 (completely un-obscured)
    recordCtx.drawImage(canvas, 0, 0);
    // Draw dedicated color range scale panel on the right side
    drawThermalColorRange(recordCtx, canvas.width, COLORBAR_PANEL_WIDTH, targetH, effMin, effMax, rangeMode);
  }

  // Update HUD elements
  const hudMode = document.getElementById('hudMode');
  if (hudMode) {
    if (isManual) {
      hudMode.textContent = 'MANUAL RANGE';
      hudMode.className = 'hud-badge hud-mode manual';
    } else {
      hudMode.textContent = 'AUTO RANGE';
      hudMode.className = 'hud-badge hud-mode';
    }
  }

  document.getElementById('hudSpot').innerHTML = smoothedCenter.toFixed(1) + ' &deg;C';
  document.getElementById('hudMin').innerHTML = 'MIN: ' + sceneMin.toFixed(1) + ' &deg;C';
  document.getElementById('hudMax').innerHTML = 'MAX: ' + sceneMax.toFixed(1) + ' &deg;C';
  document.getElementById('statSpot').innerHTML = smoothedCenter.toFixed(1) + ' &deg;C';
  document.getElementById('statMin').innerHTML = sceneMin.toFixed(1) + ' &deg;C';
  document.getElementById('statMax').innerHTML = sceneMax.toFixed(1) + ' &deg;C';
  document.getElementById('statSpan').innerHTML = span.toFixed(1) + ' &deg;C';

  const statMode = document.getElementById('statRangeMode');
  if (statMode) {
    statMode.innerHTML = isManual ? '<span class="val-accent">MANUAL</span>' : '<span class="val-green">AUTO</span>';
  }
  const statPal = document.getElementById('statPaletteRange');
  if (statPal) {
    statPal.innerHTML = effMin.toFixed(1) + ' &deg;C &ndash; ' + effMax.toFixed(1) + ' &deg;C';
  }

  document.getElementById('cbMax').innerHTML = effMax.toFixed(1) + '&deg;';
  document.getElementById('cbMid').innerHTML = (effMin + span * 0.5).toFixed(1) + '&deg;';
  document.getElementById('cbMin').innerHTML = effMin.toFixed(1) + '&deg;';

  if (isHovering && lastProbeTemp !== null) {
    document.getElementById('hudProbe').style.display = 'block';
    document.getElementById('hudProbe').innerHTML = 'PROBE: ' + lastProbeTemp.toFixed(1) + ' &deg;C';
    document.getElementById('statProbe').innerHTML = lastProbeTemp.toFixed(1) + ' &deg;C';
  } else {
    document.getElementById('hudProbe').style.display = 'none';
    document.getElementById('statProbe').innerHTML = '--.-';
  }
}

// --- Periodic Watchdog: Connection Troubles & Frame Stalls ---
setInterval(() => {
  const now = performance.now();
  const timeSinceLastFrame = (now - lastFrameTime) / 1000;

  if (!isConnected) {
    connectionTrouble = {
      title: 'CONNECTION LOST',
      sub: isReconnecting ? `Reconnecting to camera (Attempt #${reconnectCount})...` : 'Connecting to camera WebSocket...',
      level: 'err'
    };
    if (frameCount > 0) {
      drawCanvasConnectionTrouble(connectionTrouble);
    }
  } else if (frameCount > 0 && timeSinceLastFrame > 2.5) {
    // WebSocket is open, but frames stopped arriving (packet starvation/sensor pause)
    const level = timeSinceLastFrame > 6.0 ? 'err' : 'warn';
    connectionTrouble = {
      title: timeSinceLastFrame > 6.0 ? 'STREAM TIMEOUT' : 'STREAM STALLED',
      sub: `No thermal frame data received for ${timeSinceLastFrame.toFixed(1)}s`,
      level: level
    };
    drawCanvasConnectionTrouble(connectionTrouble);
  } else if (lastPacketInterval > 400 && frameCount > 0) {
    connectionTrouble = {
      title: 'NETWORK JITTER',
      sub: `High packet gap (${Math.round(lastPacketInterval)}ms)`,
      level: 'warn'
    };
  } else {
    connectionTrouble = null;
  }

  updateStreamAlertUI();
}, 400);

// --- WebSocket Streaming Client ---
let ws = null;
function connectWebSocket() {
  const wsUrl = 'ws://' + (window.location.hostname || '192.168.4.1') + ':81/';
  const wsBadge = document.getElementById('wsBadge');
  if (wsBadge) {
    wsBadge.textContent = 'WS CONNECTING...';
    wsBadge.className = 'badge badge-ws';
  }

  ws = new WebSocket(wsUrl);
  ws.binaryType = 'arraybuffer';

  ws.onopen = () => {
    isConnected = true;
    isReconnecting = false;
    reconnectCount = 0;
    if (wsBadge) {
      wsBadge.textContent = 'WS STREAM ACTIVE';
      wsBadge.className = 'badge badge-live';
    }
    if (feedBadge && !isStreaming) {
      feedBadge.textContent = '[ READY ]';
      feedBadge.className = 'badge badge-dim';
    }
    appendTerminalOutput('[SYSTEM] WebSocket link established to camera');
    if (!isStreaming && frameCount === 0) {
      drawPlaceholder('Live Wi-Fi Streaming is Inactive');
      if (streamInactiveAlert && !isStandby) {
        streamInactiveAlert.style.display = 'flex';
      }
    }
  };

  ws.onclose = () => {
    isConnected = false;
    isReconnecting = true;
    reconnectCount++;
    if (wsBadge) {
      wsBadge.textContent = 'WS RECONNECTING...';
      wsBadge.className = 'badge badge-ws';
    }
    if (feedBadge) {
      feedBadge.textContent = '[ RECONNECTING... ]';
      feedBadge.className = 'badge badge-warn';
    }
    appendTerminalOutput(`[SYSTEM] Connection lost. Reconnecting in 1.5s (Retry #${reconnectCount})...`, 'err');
    if (frameCount === 0) {
      drawPlaceholder('WebSocket Disconnected - Reconnecting...');
    }
    setTimeout(connectWebSocket, 1500);
  };

  ws.onerror = () => {
    if (wsBadge) wsBadge.textContent = 'WS ERROR';
  };

  ws.onmessage = (event) => {
    if (event.data instanceof ArrayBuffer) {
      const chunk = new Uint8Array(event.data);
      if (chunk.length >= 1546 && chunk[0] === 0x5A && chunk[1] === 0xA5 && chunk[2] === 0x5A && chunk[3] === 0xA7) {
        if (!isStreaming) {
          updateStreamingUI(true);
        }
        const dv = new DataView(chunk.buffer, chunk.byteOffset, chunk.byteLength);
        const rangeMode = chunk[4];
        const palId = chunk[5];
        const minTemp = dv.getInt16(6, true) / 100.0;
        const maxTemp = dv.getInt16(8, true) / 100.0;
        const temps = new Float32Array(768);
        for (let i = 0; i < 768; i++) {
          temps[i] = dv.getInt16(10 + i * 2, true) / 100.0;
        }
        syncDevicePalette(palId);
        handleIncomingFrame(temps, minTemp, maxTemp, rangeMode);
      }
    } else if (typeof event.data === 'string') {
      // Live serial terminal line
      appendTerminalOutput(event.data);
    }
  };
}

// --- Live Video Recording Controller & Off-Screen Composite Canvas ---
let mediaRecorder = null;
let recordedChunks = [];
let recordStartTime = 0;
let recordTimerInterval = null;
let isRecordingActive = false;

// Dedicated recording canvas that places the thermal color range and telemetry to the side
const COLORBAR_PANEL_WIDTH = 64; // Side panel width appended to the right of the thermal frame
const recordCanvas = document.createElement('canvas');
const recordCtx = recordCanvas.getContext('2d');

// Shared / cached 1x256 LUT strip canvas
const offscreenLutCanvas = document.createElement('canvas');
offscreenLutCanvas.width = 1;
offscreenLutCanvas.height = 256;
const offscreenLutCtx = offscreenLutCanvas.getContext('2d');
let cachedLutPalette = null;

function updateOffscreenLut(paletteKey) {
  if (cachedLutPalette === paletteKey) return;
  const lut = PALETTES[paletteKey] || PALETTES.IRONBOW;
  const img = offscreenLutCtx.createImageData(1, 256);
  const d32 = new Uint32Array(img.data.buffer);
  for (let i = 0; i < 256; i++) {
    // 0 is top (max temp), 255 is bottom (min temp)
    d32[i] = lut[255 - i];
  }
  offscreenLutCtx.putImageData(img, 0, 0);
  cachedLutPalette = paletteKey;
}

/**
 * Draws the thermal color range bar, temperature extremes, and range mode
 * in a dedicated side panel next to the video/photo (panelX to totalW).
 * The original thermal feed (0 to thermalW) is untouched and 100% croppable.
 */
function drawThermalColorRange(targetCtx, panelX, panelW, totalH, effMin, effMax, rangeMode) {
  updateOffscreenLut(currentPalette);

  targetCtx.save();

  // Solid dark side-panel background matching camera UI
  targetCtx.fillStyle = '#0a0d14';
  targetCtx.fillRect(panelX, 0, panelW, totalH);

  // Clean vertical divider line between thermal feed and side panel
  targetCtx.strokeStyle = '#272c38';
  targetCtx.lineWidth = 1;
  targetCtx.beginPath();
  targetCtx.moveTo(panelX + 0.5, 0);
  targetCtx.lineTo(panelX + 0.5, totalH);
  targetCtx.stroke();

  // Colorbar dimensions inside the side panel
  const barW = 12;
  const barPadY = 32;
  const barX = panelX + 8;
  const barY = barPadY;
  const barH = totalH - (barPadY * 2);

  // Mode badge at top of side panel (AUTO / MANUAL)
  targetCtx.fillStyle = rangeMode === 1 ? '#ff9800' : '#00e676';
  targetCtx.font = 'bold 8.5px monospace, sans-serif';
  targetCtx.textAlign = 'center';
  targetCtx.textBaseline = 'middle';
  targetCtx.fillText(rangeMode === 1 ? 'MANUAL' : 'AUTO', panelX + (panelW / 2), 16);

  // Draw smooth vertical gradient using the 1x256 LUT canvas
  targetCtx.imageSmoothingEnabled = true;
  targetCtx.drawImage(offscreenLutCanvas, 0, 0, 1, 256, barX, barY, barW, barH);

  // Border around the color bar
  targetCtx.strokeStyle = '#3e4452';
  targetCtx.lineWidth = 1;
  targetCtx.strokeRect(barX, barY, barW, barH);

  // Tick marks
  targetCtx.strokeStyle = '#a0a5b2';
  targetCtx.lineWidth = 1;
  targetCtx.beginPath();
  // Max tick
  targetCtx.moveTo(barX + barW, barY);
  targetCtx.lineTo(barX + barW + 3, barY);
  // Mid tick
  targetCtx.moveTo(barX + barW, barY + barH / 2);
  targetCtx.lineTo(barX + barW + 3, barY + barH / 2);
  // Min tick
  targetCtx.moveTo(barX + barW, barY + barH);
  targetCtx.lineTo(barX + barW + 3, barY + barH);
  targetCtx.stroke();

  // Temperature labels (Max, Mid, Min)
  const textX = barX + barW + 5;
  targetCtx.font = 'bold 9.5px monospace, sans-serif';
  targetCtx.textAlign = 'left';

  // Max Temp (top)
  targetCtx.fillStyle = '#ff7b72'; // warm accent
  targetCtx.textBaseline = 'top';
  targetCtx.fillText(`${effMax.toFixed(1)}°`, textX, barY - 1);

  // Mid Temp (middle)
  const midTemp = effMin + (effMax - effMin) * 0.5;
  targetCtx.fillStyle = '#c9d1d9';
  targetCtx.textBaseline = 'middle';
  targetCtx.fillText(`${midTemp.toFixed(1)}°`, textX, barY + barH / 2);

  // Min Temp (bottom)
  targetCtx.fillStyle = '#00e5ff'; // cyan accent
  targetCtx.textBaseline = 'bottom';
  targetCtx.fillText(`${effMin.toFixed(1)}°`, textX, barY + barH + 1);

  targetCtx.restore();
}

function updateRecordTimer() {
  const elapsed = Math.floor((Date.now() - recordStartTime) / 1000);
  const m = String(Math.floor(elapsed / 60)).padStart(2, '0');
  const s = String(elapsed % 60).padStart(2, '0');
  const timerStr = `${m}:${s}`;
  const recTimeEl = document.getElementById('recTime');
  if (recTimeEl) recTimeEl.textContent = timerStr;
  const btnRec = document.getElementById('btnRecord');
  if (btnRec) btnRec.innerHTML = `<span class="rec-icon">&#x25A0;</span> Stop (${timerStr})`;
}

function startRecording() {
  recordCanvas.width = canvas.width + COLORBAR_PANEL_WIDTH;
  recordCanvas.height = canvas.height;

  if (typeof recordCanvas.captureStream !== 'function' && typeof canvas.captureStream !== 'function') {
    alert('Video recording is not supported in this browser.');
    return;
  }

  try {
    const stream = (typeof recordCanvas.captureStream === 'function')
      ? recordCanvas.captureStream(30)
      : canvas.captureStream(30);

    const mimeTypes = [
      'video/webm;codecs=vp9',
      'video/webm;codecs=vp8',
      'video/webm',
      'video/mp4;codecs=avc1',
      'video/mp4'
    ];
    let selectedMime = '';
    for (const type of mimeTypes) {
      if (typeof MediaRecorder.isTypeSupported === 'function' && MediaRecorder.isTypeSupported(type)) {
        selectedMime = type;
        break;
      }
    }

    recordedChunks = [];
    mediaRecorder = new MediaRecorder(stream, selectedMime ? { mimeType: selectedMime, videoBitsPerSecond: 2500000 } : undefined);

    mediaRecorder.ondataavailable = (e) => {
      if (e.data && e.data.size > 0) {
        recordedChunks.push(e.data);
      }
    };

    mediaRecorder.onstop = () => {
      isRecordingActive = false;
      const mime = mediaRecorder.mimeType || 'video/webm';
      const ext = mime.includes('mp4') ? 'mp4' : 'webm';
      const blob = new Blob(recordedChunks, { type: mime });
      const url = URL.createObjectURL(blob);
      const nowStr = new Date().toISOString().replace(/[:.]/g, '-');
      const filename = `thermal-stream-${nowStr}.${ext}`;

      const link = document.createElement('a');
      link.href = url;
      link.download = filename;
      document.body.appendChild(link);
      link.click();
      document.body.removeChild(link);
      setTimeout(() => URL.revokeObjectURL(url), 10000);

      appendTerminalOutput(`[REC] Saved video: ${filename} (${(blob.size / 1024 / 1024).toFixed(2)} MB)`, 'ok');
    };

    mediaRecorder.start(250);
    isRecordingActive = true;
    recordStartTime = Date.now();
    recordTimerInterval = setInterval(updateRecordTimer, 1000);
    updateRecordTimer();

    document.getElementById('hudRec').style.display = 'flex';
    const btnRec = document.getElementById('btnRecord');
    btnRec.classList.add('recording');
    appendTerminalOutput('[REC] Video recording started (with thermal color range scale)...', 'cmd');
  } catch (err) {
    console.error('Record error:', err);
    alert('Could not start recording: ' + err.message);
  }
}

function stopRecording() {
  isRecordingActive = false;
  if (mediaRecorder && mediaRecorder.state !== 'inactive') {
    mediaRecorder.stop();
  }
  if (recordTimerInterval) {
    clearInterval(recordTimerInterval);
    recordTimerInterval = null;
  }
  document.getElementById('hudRec').style.display = 'none';
  const btnRec = document.getElementById('btnRecord');
  btnRec.classList.remove('recording');
  btnRec.innerHTML = '<span class="rec-icon">&#x25CF;</span> Record Video';
}

document.getElementById('btnRecord').addEventListener('click', () => {
  if (!isStreaming) return;
  if (mediaRecorder && mediaRecorder.state === 'recording') {
    stopRecording();
  } else {
    startRecording();
  }
});

// --- Reticle & Cursor/Touch Temperature Probing ---
function updateProbePosition(clientX, clientY) {
  const rect = canvas.getBoundingClientRect();
  const scaleX = canvas.width / rect.width;
  const scaleY = canvas.height / rect.height;
  const cx = (clientX - rect.left) * scaleX;
  const cy = (clientY - rect.top) * scaleY;
  const rotW = (currentRotation === 90 || currentRotation === 270) ? 24 : 32;
  const rotH = (currentRotation === 90 || currentRotation === 270) ? 32 : 24;

  probeX = Math.max(0, Math.min(rotW - 1, Math.floor(cx / (canvas.width / rotW))));
  probeY = Math.max(0, Math.min(rotH - 1, Math.floor(cy / (canvas.height / rotH))));
  reticle.style.left = (clientX - rect.left) + 'px';
  reticle.style.top = (clientY - rect.top) + 'px';
  reticle.style.display = 'block';
  isHovering = true;
}

// Mouse events
vpWrapper.addEventListener('mousemove', (e) => updateProbePosition(e.clientX, e.clientY));
vpWrapper.addEventListener('mouseleave', () => {
  reticle.style.display = 'none';
  isHovering = false;
});

// Touch events for mobile screens
vpWrapper.addEventListener('touchstart', (e) => {
  if (e.touches.length > 0) {
    updateProbePosition(e.touches[0].clientX, e.touches[0].clientY);
  }
}, { passive: true });

vpWrapper.addEventListener('touchmove', (e) => {
  if (e.touches.length > 0) {
    updateProbePosition(e.touches[0].clientX, e.touches[0].clientY);
  }
}, { passive: true });

vpWrapper.addEventListener('touchend', () => {
  setTimeout(() => {
    reticle.style.display = 'none';
    isHovering = false;
  }, 1500);
});

// --- Settings Drawer Menu Controller ---
const settingsDrawer = document.getElementById('settingsDrawer');
const btnSettingsToggle = document.getElementById('btnSettingsToggle');
const btnSettingsClose = document.getElementById('btnSettingsClose');

if (btnSettingsToggle && settingsDrawer) {
  btnSettingsToggle.addEventListener('click', () => {
    const isShown = settingsDrawer.style.display !== 'none';
    settingsDrawer.style.display = isShown ? 'none' : 'block';
    btnSettingsToggle.classList.toggle('active', !isShown);
    btnSettingsToggle.innerHTML = !isShown
      ? '<span class="btn-icon">&#x2699;</span> Settings &#x25B4;'
      : '<span class="btn-icon">&#x2699;</span> Settings &#x25BE;';
  });
}

if (btnSettingsClose && settingsDrawer) {
  btnSettingsClose.addEventListener('click', () => {
    settingsDrawer.style.display = 'none';
    if (btnSettingsToggle) {
      btnSettingsToggle.classList.remove('active');
      btnSettingsToggle.innerHTML = '<span class="btn-icon">&#x2699;</span> Settings &#x25BE;';
    }
  });
}

// --- Controls Event Handlers ---
document.getElementById('paletteSelect').addEventListener('change', (e) => {
  currentPalette = e.target.value;
  document.getElementById('statPalette').textContent = e.target.options[e.target.selectedIndex].text;
  drawColorbar();
  const palIdx = PALETTE_ID_MAP.indexOf(currentPalette);
  if (palIdx !== -1) {
    fetch(`/api/control?palette=${palIdx}`).catch(() => {});
  }
});

document.getElementById('scalingSelect').addEventListener('change', (e) => {
  currentScaling = e.target.value;
  scaler.resetHistory();
  document.getElementById('statScaling').textContent = currentScaling;
  const sharpCtrl = document.getElementById('sharpnessCtrl');
  if (sharpCtrl) sharpCtrl.style.display = (currentScaling === 'TAAU') ? 'flex' : 'none';
});

document.getElementById('sharpnessSelect').addEventListener('change', (e) => {
  currentSharpness = parseFloat(e.target.value);
});

document.getElementById('rotationSelect').addEventListener('change', (e) => {
  currentRotation = parseInt(e.target.value, 10);
  scaler.resetHistory();
  updateDimensions();
});

document.getElementById('btnDenoise').addEventListener('click', function() {
  liveDenoise = !liveDenoise;
  this.classList.toggle('active', liveDenoise);
  this.textContent = 'Denoise: ' + (liveDenoise ? 'ON' : 'OFF');
});

document.getElementById('btnFlip').addEventListener('click', function() {
  flipVertical = !flipVertical;
  this.classList.toggle('active', flipVertical);
  this.textContent = 'Flip: ' + (flipVertical ? 'V' : 'OFF');
});

document.getElementById('btnCapture').addEventListener('click', () => {
  if (!isStreaming) return;
  // Create off-screen canvas with dedicated side panel for color range scale
  const photoCanvas = document.createElement('canvas');
  photoCanvas.width = canvas.width + COLORBAR_PANEL_WIDTH;
  photoCanvas.height = canvas.height;
  const photoCtx = photoCanvas.getContext('2d');

  // Copy current live frame at 0, 0 (completely un-obscured, 100% croppable)
  photoCtx.drawImage(canvas, 0, 0);

  // Draw color range scale and temps into dedicated side panel on the right
  drawThermalColorRange(photoCtx, canvas.width, COLORBAR_PANEL_WIDTH, canvas.height, lastEffMin, lastEffMax, currentRangeMode);

  const link = document.createElement('a');
  link.download = 'thermal-cam-' + Date.now() + '.png';
  link.href = photoCanvas.toDataURL('image/png');
  link.click();
  appendTerminalOutput(`[REC] Captured photo: ${link.download} (with side color scale)`, 'ok');
});

// --- Palette Synchronization Helper ---
function syncDevicePalette(palId) {
  if (palId !== undefined && palId >= 0 && palId < PALETTE_ID_MAP.length) {
    const palName = PALETTE_ID_MAP[palId];
    if (currentPalette !== palName) {
      currentPalette = palName;
      const sel = document.getElementById('paletteSelect');
      if (sel) sel.value = palName;
      const stat = document.getElementById('statPalette');
      if (stat) stat.textContent = (palName === 'IRONBOW') ? 'Ironbow (FLIR)' : (palName === 'WHITE_HOT' ? 'White-Hot (Monochrome)' : 'Rainbow (Classic)');
      drawColorbar();
    }
  }
}

// --- Thermal Range Calibration Controller ---
function updateRangeModeUI(mode, minVal, maxVal) {
  currentRangeMode = mode;
  const isMan = (mode === 1);
  const btnAuto = document.getElementById('btnRangeAuto');
  const btnMan = document.getElementById('btnRangeManual');
  const manControls = document.getElementById('manualRangeControls');
  const statMode = document.getElementById('statRangeMode');
  const hudMode = document.getElementById('hudMode');

  if (btnAuto && btnMan) {
    btnAuto.className = isMan ? 'range-mode-btn' : 'range-mode-btn active';
    btnAuto.innerHTML = isMan ? '&#x25CB; AUTO RANGE' : '&#x25CF; AUTO RANGE';
    btnMan.className = isMan ? 'range-mode-btn active' : 'range-mode-btn';
    btnMan.innerHTML = isMan ? '&#x25CF; MANUAL RANGE' : '&#x25CB; MANUAL RANGE';
  }

  if (manControls) {
    manControls.style.display = isMan ? 'flex' : 'none';
  }

  if (minVal !== undefined && !isNaN(minVal)) {
    currentManualMin = minVal;
    const minIn = document.getElementById('manualMinInput');
    if (minIn && document.activeElement !== minIn) minIn.value = minVal.toFixed(1);
  }
  if (maxVal !== undefined && !isNaN(maxVal)) {
    currentManualMax = maxVal;
    const maxIn = document.getElementById('manualMaxInput');
    if (maxIn && document.activeElement !== maxIn) maxIn.value = maxVal.toFixed(1);
  }

  if (statMode) {
    statMode.innerHTML = isMan ? '<span class="val-accent">MANUAL</span>' : '<span class="val-green">AUTO</span>';
  }
  if (hudMode) {
    hudMode.textContent = isMan ? 'MANUAL RANGE' : 'AUTO RANGE';
    hudMode.className = isMan ? 'hud-badge hud-mode manual' : 'hud-badge hud-mode';
  }
}

async function sendRangeControl(mode, minVal, maxVal) {
  try {
    let url = `/api/control?rangeMode=${mode}`;
    if (minVal !== undefined && maxVal !== undefined) {
      url += `&manualMin=${minVal.toFixed(1)}&manualMax=${maxVal.toFixed(1)}`;
    }
    const res = await fetch(url);
    if (res.ok) {
      const d = await res.json();
      updateRangeModeUI(d.rangeMode, d.manualMin, d.manualMax);
    }
  } catch(e) {}
}

const btnRangeAuto = document.getElementById('btnRangeAuto');
if (btnRangeAuto) {
  btnRangeAuto.addEventListener('click', () => {
    sendRangeControl(0);
  });
}

const btnRangeManual = document.getElementById('btnRangeManual');
if (btnRangeManual) {
  btnRangeManual.addEventListener('click', () => {
    const minVal = parseFloat(document.getElementById('manualMinInput').value) || 20.0;
    const maxVal = parseFloat(document.getElementById('manualMaxInput').value) || 50.0;
    sendRangeControl(1, minVal, maxVal);
  });
}

const btnApplyRange = document.getElementById('btnApplyRange');
if (btnApplyRange) {
  btnApplyRange.addEventListener('click', () => {
    let minVal = parseFloat(document.getElementById('manualMinInput').value);
    let maxVal = parseFloat(document.getElementById('manualMaxInput').value);
    if (isNaN(minVal)) minVal = 20.0;
    if (isNaN(maxVal)) maxVal = 50.0;
    if (maxVal <= minVal + 1.0) {
      maxVal = minVal + 1.0;
      document.getElementById('manualMaxInput').value = maxVal.toFixed(1);
    }
    sendRangeControl(1, minVal, maxVal);
  });
}

const btnMinMinus = document.getElementById('btnMinMinus');
if (btnMinMinus) {
  btnMinMinus.addEventListener('click', () => {
    const inEl = document.getElementById('manualMinInput');
    let v = (parseFloat(inEl.value) || 20.0) - 1.0;
    if (v < -20.0) v = -20.0;
    inEl.value = v.toFixed(1);
  });
}

const btnMinPlus = document.getElementById('btnMinPlus');
if (btnMinPlus) {
  btnMinPlus.addEventListener('click', () => {
    const inEl = document.getElementById('manualMinInput');
    const maxEl = document.getElementById('manualMaxInput');
    let maxV = parseFloat(maxEl.value) || 50.0;
    let v = (parseFloat(inEl.value) || 20.0) + 1.0;
    if (v > maxV - 1.0) v = maxV - 1.0;
    inEl.value = v.toFixed(1);
  });
}

const btnMaxMinus = document.getElementById('btnMaxMinus');
if (btnMaxMinus) {
  btnMaxMinus.addEventListener('click', () => {
    const minEl = document.getElementById('manualMinInput');
    const maxEl = document.getElementById('manualMaxInput');
    let minV = parseFloat(minEl.value) || 20.0;
    let v = (parseFloat(maxEl.value) || 50.0) - 1.0;
    if (v < minV + 1.0) v = minV + 1.0;
    maxEl.value = v.toFixed(1);
  });
}

const btnMaxPlus = document.getElementById('btnMaxPlus');
if (btnMaxPlus) {
  btnMaxPlus.addEventListener('click', () => {
    const inEl = document.getElementById('manualMaxInput');
    let v = (parseFloat(inEl.value) || 50.0) + 1.0;
    if (v > 300.0) v = 300.0;
    inEl.value = v.toFixed(1);
  });
}

// --- Surface Emissivity Controller ---
function getEmissivityPresetName(emi) {
  if (Math.abs(emi - 0.95) < 0.015) return 'Matte (0.95)';
  if (Math.abs(emi - 0.98) < 0.015) return 'Skin (0.98)';
  if (Math.abs(emi - 0.90) < 0.015) return 'Wood (0.90)';
  if (Math.abs(emi - 0.80) < 0.015) return 'Oxidized (0.80)';
  if (Math.abs(emi - 0.60) < 0.015) return 'Cast Iron (0.60)';
  if (Math.abs(emi - 0.20) < 0.015) return 'Polished (0.20)';
  return `Custom (${emi.toFixed(2)})`;
}

function updateEmissivityUI(emi, presetName) {
  currentEmissivity = emi;
  const slider = document.getElementById('emissivitySlider');
  if (slider && document.activeElement !== slider) {
    slider.value = emi.toFixed(2);
  }
  const name = presetName || getEmissivityPresetName(emi);
  const badge = document.getElementById('emiValBadge');
  if (badge) badge.textContent = `\u03B5 = ${emi.toFixed(2)} (${name})`;
  const stat = document.getElementById('statEmissivity');
  if (stat) stat.textContent = `${emi.toFixed(2)} (${name})`;

  document.querySelectorAll('.preset-chip').forEach(btn => {
    const bEmi = parseFloat(btn.getAttribute('data-emi'));
    if (Math.abs(bEmi - emi) < 0.015) {
      btn.classList.add('active');
    } else {
      btn.classList.remove('active');
    }
  });
}

async function sendEmissivity(val) {
  try {
    const res = await fetch(`/api/control?emissivity=${val.toFixed(2)}`);
    if (res.ok) {
      const d = await res.json();
      updateEmissivityUI(d.emissivity);
    }
  } catch(e) {}
}

const emiSlider = document.getElementById('emissivitySlider');
let emiDebounceTimer = null;
if (emiSlider) {
  emiSlider.addEventListener('input', (e) => {
    const val = parseFloat(e.target.value);
    updateEmissivityUI(val);
    clearTimeout(emiDebounceTimer);
    emiDebounceTimer = setTimeout(() => {
      sendEmissivity(val);
    }, 150);
  });
  emiSlider.addEventListener('change', (e) => {
    clearTimeout(emiDebounceTimer);
    sendEmissivity(parseFloat(e.target.value));
  });
}

document.querySelectorAll('.preset-chip').forEach(btn => {
  btn.addEventListener('click', () => {
    const val = parseFloat(btn.getAttribute('data-emi'));
    updateEmissivityUI(val);
    sendEmissivity(val);
  });
});

// --- Live Serial Terminal Controller ---
const termBody = document.getElementById('terminalBody');
const termOutput = document.getElementById('terminalOutput');
const termInput = document.getElementById('termInput');
const termForm = document.getElementById('termForm');

const cmdHistory = [];
let historyIndex = -1;

function appendTerminalOutput(text, type = '') {
  if (!text) return;
  const lines = text.split('\n');
  for (let rawLine of lines) {
    const line = rawLine.replace(/\r/g, '');
    if (!line && lines.length > 1) continue;

    const div = document.createElement('div');
    div.className = 'term-line';

    // Auto-detect classification
    if (type) {
      div.classList.add(type);
    } else if (line.startsWith('>')) {
      div.classList.add('cmd');
    } else if (line.includes('[WARN]') || line.includes('Warning')) {
      div.classList.add('warn');
    } else if (line.includes('[ERR]') || line.includes('Error') || line.includes('FAILED')) {
      div.classList.add('err');
    } else if (line.includes('[OK]') || line.includes('SUCCESS') || line.includes('Ready')) {
      div.classList.add('ok');
    } else if (line.startsWith('[')) {
      div.classList.add('sys');
    }

    div.textContent = line;
    termOutput.appendChild(div);
  }

  // Cap DOM history to 400 lines
  while (termOutput.children.length > 400) {
    termOutput.removeChild(termOutput.firstChild);
  }

  // Auto-scroll to bottom
  termBody.scrollTop = termBody.scrollHeight;
}

function sendTerminalCommand(cmd) {
  cmd = (cmd || '').trim();
  if (!cmd) return;

  if (cmd.toLowerCase() === 'clear') {
    termOutput.innerHTML = '';
    return;
  }

  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(cmd);
  } else {
    appendTerminalOutput('> ' + cmd, 'cmd');
    appendTerminalOutput('[ERR] Terminal not connected to device WebSocket', 'err');
  }

  cmdHistory.push(cmd);
  if (cmdHistory.length > 50) cmdHistory.shift();
  historyIndex = cmdHistory.length;
}

termForm.addEventListener('submit', (e) => {
  e.preventDefault();
  const cmd = termInput.value;
  termInput.value = '';
  sendTerminalCommand(cmd);
});

// Command history recall with Up/Down arrow keys
termInput.addEventListener('keydown', (e) => {
  if (e.key === 'ArrowUp') {
    if (historyIndex > 0) {
      historyIndex--;
      termInput.value = cmdHistory[historyIndex] || '';
    }
    e.preventDefault();
  } else if (e.key === 'ArrowDown') {
    if (historyIndex < cmdHistory.length - 1) {
      historyIndex++;
      termInput.value = cmdHistory[historyIndex] || '';
    } else {
      historyIndex = cmdHistory.length;
      termInput.value = '';
    }
    e.preventDefault();
  }
});

// Quick command chips
document.querySelectorAll('.term-chip[data-cmd]').forEach((chip) => {
  chip.addEventListener('click', () => {
    const cmd = chip.getAttribute('data-cmd');
    sendTerminalCommand(cmd);
  });
});

document.getElementById('btnTermClear').addEventListener('click', () => {
  termOutput.innerHTML = '';
});

// Fetch boot logs on load
async function loadInitialLogs() {
  try {
    const res = await fetch('/api/logs');
    if (!res.ok) return;
    const logs = await res.json();
    if (Array.isArray(logs) && logs.length > 0) {
      for (const line of logs) {
        appendTerminalOutput(line);
      }
    }
  } catch (e) {}
}

// --- Standby & Power Management ---
let isStandby = false;

function updateStandbyUI(standbyActive) {
  isStandby = !!standbyActive;
  const standbyAlert = document.getElementById('standbyAlert');
  const btnStandby = document.getElementById('btnStandby');
  const statPwr = document.getElementById('statPwr');

  if (isStandby) {
    if (standbyAlert) standbyAlert.style.display = 'flex';
    if (streamInactiveAlert) streamInactiveAlert.style.display = 'none';
    if (btnStandby) {
      btnStandby.innerHTML = '&#x2600; Wake Up';
      btnStandby.classList.add('active');
    }
    if (statPwr) {
      statPwr.innerText = 'STANDBY';
    }
  } else {
    if (standbyAlert) standbyAlert.style.display = 'none';
    if (streamInactiveAlert && !isStreaming) streamInactiveAlert.style.display = 'flex';
    if (btnStandby) {
      btnStandby.innerHTML = '&#x1F319; Standby';
      btnStandby.classList.remove('active');
    }
  }
}

// --- Wi-Fi Stream Control Functions ---
function updateStreamingUI(active) {
  isStreaming = !!active;

  if (btnStreamToggle) {
    if (isStreaming) {
      btnStreamToggle.innerHTML = '<span class="stream-icon">&#x25A0;</span> Stop Stream';
      btnStreamToggle.classList.add('streaming');
      btnStreamToggle.title = 'Stop live Wi-Fi stream';
    } else {
      btnStreamToggle.innerHTML = '<span class="stream-icon">&#x25B6;</span> Start Stream';
      btnStreamToggle.classList.remove('streaming');
      btnStreamToggle.title = 'Start live Wi-Fi stream';
    }
  }

  if (streamInactiveAlert) {
    streamInactiveAlert.style.display = (!isStreaming && !isStandby) ? 'flex' : 'none';
  }

  if (btnRecord) {
    btnRecord.disabled = !isStreaming;
    btnRecord.title = isStreaming ? 'Record video of live thermal stream' : 'Start live stream first to record video';
  }

  if (btnCapture) {
    btnCapture.disabled = !isStreaming;
    btnCapture.title = isStreaming ? 'Capture photo of current thermal frame' : 'Start live stream first to capture photo';
  }

  if (feedBadge) {
    if (isStreaming) {
      feedBadge.textContent = '[ LIVE STREAM ]';
      feedBadge.className = 'badge badge-live';
    } else {
      feedBadge.textContent = '[ STREAM PAUSED ]';
      feedBadge.className = 'badge badge-dim';
    }
  }
}

async function toggleWiFiStream(forceState) {
  const next = (forceState !== undefined) ? !!forceState : !isStreaming;
  updateStreamingUI(next);

  // If starting stream while in standby, wake up camera
  if (next && isStandby) {
    sendPowerAction('wake');
  }

  try {
    const res = await fetch(`/api/stream?action=${next ? 'start' : 'stop'}`);
    if (res.ok) {
      const d = await res.json();
      if (d && d.streaming !== undefined) {
        updateStreamingUI(d.streaming);
      }
    }
  } catch (err) {
    console.warn('[STREAM] HTTP stream toggle failed:', err);
  }

  // Also send duplex WebSocket command if connected
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(next ? 'stream start' : 'stream stop');
  }

  appendTerminalOutput(`[SYSTEM] Wi-Fi streaming ${next ? 'START requested' : 'STOP requested'}.`);
}

async function sendPowerAction(action) {
  try {
    const res = await fetch(`/api/power?action=${action}`, { method: 'POST' });
    if (!res.ok) return;
    const d = await res.json();
    if (d.standby !== undefined) {
      updateStandbyUI(d.standby);
      appendTerminalOutput(d.standby ? '[SYSTEM] Camera entered Standby mode (~35 mA).' : '[SYSTEM] Camera awakened from Standby mode.');
    }
    if (action === 'deepsleep' || action === 'shutdown') {
      appendTerminalOutput('[SYSTEM] Camera entering Deep Sleep (~10 uA). Wi-Fi turning off.');
      const powerModal = document.getElementById('powerModal');
      if (powerModal) powerModal.style.display = 'none';

      const standbyAlert = document.getElementById('standbyAlert');
      if (standbyAlert) {
        standbyAlert.style.display = 'flex';
        const title = standbyAlert.querySelector('.alert-title');
        const sub = standbyAlert.querySelector('.alert-sub');
        const wakeBtn = document.getElementById('btnOverlayWake');
        if (title) title.innerText = 'CAMERA SHUT DOWN';
        if (sub) sub.innerText = 'Camera is in Deep Sleep mode (~10 µA). Click the physical knob to power on.';
        if (wakeBtn) wakeBtn.style.display = 'none';
      }
    }
  } catch (err) {
    console.error('Power action failed:', err);
  }
}

// Wire up Stream buttons
if (btnStreamToggle) {
  btnStreamToggle.addEventListener('click', () => {
    toggleWiFiStream();
  });
}

if (btnOverlayStartStream) {
  btnOverlayStartStream.addEventListener('click', () => {
    toggleWiFiStream(true);
  });
}

// Wire up Power buttons
const btnStandby = document.getElementById('btnStandby');
if (btnStandby) {
  btnStandby.addEventListener('click', () => {
    sendPowerAction(isStandby ? 'wake' : 'standby');
  });
}

const btnOverlayWake = document.getElementById('btnOverlayWake');
if (btnOverlayWake) {
  btnOverlayWake.addEventListener('click', () => {
    sendPowerAction('wake');
  });
}

const btnPowerOff = document.getElementById('btnPowerOff');
const powerModal = document.getElementById('powerModal');
const btnPowerModalClose = document.getElementById('btnPowerModalClose');
const btnCancelPower = document.getElementById('btnCancelPower');
const btnConfirmPower = document.getElementById('btnConfirmPower');

if (btnPowerOff && powerModal) {
  btnPowerOff.addEventListener('click', () => {
    powerModal.style.display = 'flex';
  });
}
if (btnPowerModalClose && powerModal) {
  btnPowerModalClose.addEventListener('click', () => {
    powerModal.style.display = 'none';
  });
}
if (btnCancelPower && powerModal) {
  btnCancelPower.addEventListener('click', () => {
    powerModal.style.display = 'none';
  });
}
if (btnConfirmPower) {
  btnConfirmPower.addEventListener('click', () => {
    sendPowerAction('deepsleep');
  });
}

// --- Periodic Telemetry Polling ---
async function updateStatus() {
  try {
    const res = await fetch('/api/status');
    if (!res.ok) return;
    const d = await res.json();
    document.getElementById('statBatt').innerText = d.battery + '%';
    document.getElementById('statVolt').innerText = d.voltage.toFixed(3) + ' V';
    document.getElementById('statPwr').innerText = d.standby ? 'STANDBY' : d.pwrState;
    document.getElementById('statClients').innerText = d.clients;
    document.getElementById('statHeap').innerText = Math.round(d.freeHeap / 1024) + ' KB';
    document.getElementById('statPsram').innerText = Math.round(d.freePsram / 1024) + ' KB';
    document.getElementById('statUptime').innerText = d.uptime + ' s';

    if (d.streaming !== undefined) {
      updateStreamingUI(d.streaming);
    }
    if (d.standby !== undefined) {
      updateStandbyUI(d.standby);
    }
    if (d.rangeMode !== undefined) {
      updateRangeModeUI(d.rangeMode, d.manualMin, d.manualMax);
    }
    if (d.emissivity !== undefined) {
      updateEmissivityUI(d.emissivity, d.emissivityPreset);
    }
    if (d.paletteId !== undefined) {
      syncDevicePalette(d.paletteId);
    }
  } catch(e) {}
}

// --- Initialization ---
updateDimensions();
updateStreamingUI(false);
connectWebSocket();
loadInitialLogs();
setInterval(updateStatus, 1000);
updateStatus();
