import time
import serial
import numpy as np
import cv2

# ==============================================================================
# Configuration
# ==============================================================================
PORT = 'COM5'          # Adjust to match your ESP32-S3 serial port
BAUD_RATE = 921600

RAW_WIDTH, RAW_HEIGHT = 32, 24
UPSCALE = 20           # 20x upscaling -> 640x480
TARGET_WIDTH, TARGET_HEIGHT = RAW_WIDTH * UPSCALE, RAW_HEIGHT * UPSCALE

# Thermal bounds for colormap scaling (in degrees Celsius)
T_MIN = 28.0
T_MAX = 36.0

# Orientation: True to invert vertically (fixes upside-down sensor mounting)
FLIP_VERTICAL = True

# ==============================================================================
# De-checkerboard & Filtering Pipeline (Step 43)
# ==============================================================================
CHECKER_MASK = (np.indices((RAW_HEIGHT, RAW_WIDTH)).sum(axis=0) % 2).astype(np.uint8)
CHECKER_INTERIOR = CHECKER_MASK[1:-1, 1:-1]
LAPLACIAN_KERNEL = np.array([[0, 0.25, 0],
                             [0.25, 0, 0.25],
                             [0, 0.25, 0]], dtype=np.float32)

def remove_checkerboard_artifact(frame: np.ndarray) -> np.ndarray:
    """
    Cancels the subpage inter-calibration offset in MLX90640 chess mode.
    Uses nanmedian and finite guards so transient sensor NaNs can never poison the frame.
    """
    # Guard against NaNs at the input
    if not np.isfinite(frame).all():
        frame = np.nan_to_num(frame, copy=True, nan=25.0)

    neighbor_avg = cv2.filter2D(frame, -1, LAPLACIAN_KERNEL)
    diff = frame - neighbor_avg
    diff_int = diff[1:-1, 1:-1]

    # Use nanmedian to protect against any residual bad pixels
    m0 = np.nanmedian(diff_int[CHECKER_INTERIOR == 0])
    m1 = np.nanmedian(diff_int[CHECKER_INTERIOR == 1])
    delta = (m0 - m1) / 4.0

    if not np.isfinite(delta):
        return frame

    corrected = frame.copy()
    corrected[CHECKER_MASK == 0] -= delta
    corrected[CHECKER_MASK == 1] += delta
    return corrected

# Binary Packet Protocol: 4-byte Magic (0x5A, 0xA5, 0x5A, 0xA5) + 768 float32 values (3072 bytes)
MAGIC_HEADER = b'\x5a\xa5\x5a\xa5'
PAYLOAD_BYTES = RAW_WIDTH * RAW_HEIGHT * 4  # 3072 bytes
PACKET_TOTAL_BYTES = len(MAGIC_HEADER) + PAYLOAD_BYTES  # 3076 bytes

class SerialThermalReader:
    def __init__(self, port: str, baud: int):
        self.ser = serial.Serial(port, baud, timeout=0.05)
        time.sleep(0.4)
        self.ser.reset_input_buffer()
        self.byte_buffer = bytearray()

    def get_latest_frame(self) -> np.ndarray | None:
        """
        Drains the serial buffer and decodes the freshest binary packet (or ASCII fallback).
        Zero-copy float32 unpacking eliminating all string serialization lag and drops.
        """
        waiting = self.ser.in_waiting
        if waiting > 0:
            self.byte_buffer.extend(self.ser.read(waiting))

        # --- Fast Path: Binary Packet ---
        idx = self.byte_buffer.rfind(MAGIC_HEADER)
        if idx != -1:
            if len(self.byte_buffer) >= idx + PACKET_TOTAL_BYTES:
                payload = self.byte_buffer[idx + len(MAGIC_HEADER) : idx + PACKET_TOTAL_BYTES]
                self.byte_buffer = self.byte_buffer[idx + PACKET_TOTAL_BYTES :]
                frame = np.frombuffer(payload, dtype=np.float32).reshape((RAW_HEIGHT, RAW_WIDTH))
                if not np.isfinite(frame).all():
                    frame = np.nan_to_num(frame, copy=True, nan=25.0, posinf=100.0, neginf=-40.0)
                return np.clip(frame, -40.0, 300.0)
            else:
                # Check if an earlier complete packet exists before the latest incomplete one
                prev_idx = self.byte_buffer.rfind(MAGIC_HEADER, 0, idx)
                if prev_idx != -1 and len(self.byte_buffer) >= prev_idx + PACKET_TOTAL_BYTES:
                    payload = self.byte_buffer[prev_idx + len(MAGIC_HEADER) : prev_idx + PACKET_TOTAL_BYTES]
                    self.byte_buffer = self.byte_buffer[prev_idx + PACKET_TOTAL_BYTES :]
                    frame = np.frombuffer(payload, dtype=np.float32).reshape((RAW_HEIGHT, RAW_WIDTH))
                    if not np.isfinite(frame).all():
                        frame = np.nan_to_num(frame, copy=True, nan=25.0, posinf=100.0, neginf=-40.0)
                    return np.clip(frame, -40.0, 300.0)

        # Prevent runaway buffer
        if len(self.byte_buffer) > 16384:
            self.byte_buffer = self.byte_buffer[-8192:]

        return None

    def close(self):
        self.ser.close()

# ==============================================================================
# TAAU (Temporal Anti-Aliasing Upsampling) Super-Resolution Engine
# ==============================================================================
class TAAUUpscaler:
    """
    Temporal Anti-Aliasing Upsampling (TAAU / FSR 2.0 style):
    1. Sub-pixel motion estimation (phase correlation across frames)
    2. High-resolution history reprojection / warping
    3. Neighborhood bounding-box clamping (anti-ghosting)
    4. Sub-pixel sample accumulation across natural sensor micro-jitter
    5. Contrast Adaptive Sharpening (CAS) & edge-preserving bilateral definition
    """
    def __init__(self, target_w: int, target_h: int, alpha: float = 0.80):
        self.target_w = target_w
        self.target_h = target_h
        self.alpha = alpha
        self.history = None
        self.prev_low = None

    def reset(self):
        self.history = None
        self.prev_low = None

    def process(self, frame_low: np.ndarray) -> np.ndarray:
        h, w = frame_low.shape
        cur_low = np.float32(frame_low)

        # Base high-order reconstruction
        cur_high = cv2.resize(cur_low, (self.target_w, self.target_h), interpolation=cv2.INTER_CUBIC)

        if self.history is None or self.prev_low is None:
            self.history = cur_high.copy()
            self.prev_low = cur_low.copy()
            return cur_high

        # 1. Sub-pixel Motion Registration
        # NOTE: cv2.phaseCorrelate mutates its inputs in-place when a Hanning window is passed!
        # We MUST pass .copy() so the window does NOT zero out the image borders.
        window = cv2.createHanningWindow((w, h), cv2.CV_32F)
        shift, response = cv2.phaseCorrelate(self.prev_low.copy(), cur_low.copy(), window)
        dx = np.clip(shift[0], -3.0, 3.0)
        dy = np.clip(shift[1], -3.0, 3.0)

        # 2. History Reprojection (Warping by motion vector)
        scale_x = self.target_w / w
        scale_y = self.target_h / h
        M = np.float32([[1, 0, dx * scale_x], [0, 1, dy * scale_y]])
        warped_history = cv2.warpAffine(self.history, M, (self.target_w, self.target_h),
                                        borderMode=cv2.BORDER_REPLICATE)

        # 3. Neighborhood Clamping (Anti-Ghosting Bounding Box)
        kernel = cv2.getStructuringElement(cv2.MORPH_RECT, (3, 3))
        low_min = cv2.erode(cur_low, kernel)
        low_max = cv2.dilate(cur_low, kernel)
        high_min = cv2.resize(low_min, (self.target_w, self.target_h), interpolation=cv2.INTER_LINEAR)
        high_max = cv2.resize(low_max, (self.target_w, self.target_h), interpolation=cv2.INTER_LINEAR)
        clamped_history = np.clip(warped_history, high_min - 0.2, high_max + 0.2)

        # 4. Temporal Sample Accumulation
        accumulated = (1.0 - self.alpha) * cur_high + self.alpha * clamped_history

        # 5. Contrast Adaptive Edge-Preserving Sharpening Pass
        smooth = cv2.bilateralFilter(accumulated, d=5, sigmaColor=1.5, sigmaSpace=5.0)
        detail = accumulated - cv2.GaussianBlur(accumulated, (0, 0), 2.0)
        sharpened = smooth + 1.4 * detail

        self.history = accumulated.copy()
        self.prev_low = cur_low.copy()

        return sharpened

UPSCALER_MODES = [
    "TAAU (Temporal Super-Res)",
    "Edge-Preserving (Bilateral)",
    "Lanczos-4 (Sharp)",
    "Bicubic (Smooth)",
    "Bilinear (Linear)",
    "None (Nearest Neighbor / Raw Pixels)"
]

# ==============================================================================
# Text Rendering with Drop Shadow
# ==============================================================================
def draw_text_with_shadow(
    img: np.ndarray,
    text: str,
    pos: tuple[int, int],
    font: int = cv2.FONT_HERSHEY_SIMPLEX,
    scale: float = 0.5,
    color: tuple[int, int, int] = (255, 255, 255),
    thickness: int = 1,
    shadow_color: tuple[int, int, int] = (0, 0, 0),
    shadow_offset: tuple[int, int] = (1, 1),
) -> None:
    """Renders text with a subtle 1px drop shadow for crisp readability."""
    x, y = pos
    dx, dy = shadow_offset
    cv2.putText(img, text, (x + dx, y + dy), font, scale, shadow_color, thickness, cv2.LINE_AA)
    cv2.putText(img, text, (x, y), font, scale, color, thickness, cv2.LINE_AA)

# ==============================================================================
# Color Bar Component
# ==============================================================================
def create_color_bar(width: int, t_min: float, t_max: float, height: int = 50) -> np.ndarray:
    """Renders a horizontal colormap gradient bar with temperature labels."""
    bar_img = np.zeros((height, width, 3), dtype=np.uint8)

    pad_x = 24
    bar_w = width - 2 * pad_x
    bar_h = 14
    bar_y = 8

    # Create 0-255 gradient matching colormap
    grad = np.linspace(0, 255, bar_w, dtype=np.uint8).reshape(1, bar_w)
    grad = np.repeat(grad, bar_h, axis=0)
    grad_color = cv2.applyColorMap(grad, cv2.COLORMAP_INFERNO)
    bar_img[bar_y:bar_y + bar_h, pad_x:pad_x + bar_w] = grad_color

    # Outline
    cv2.rectangle(bar_img, (pad_x, bar_y), (pad_x + bar_w, bar_y + bar_h), (100, 100, 100), 1)

    # Temperature labels: Min, Mid, Max
    font = cv2.FONT_HERSHEY_SIMPLEX
    scale = 0.42
    color = (255, 255, 255)
    text_y = bar_y + bar_h + 18

    draw_text_with_shadow(bar_img, f"{t_min:.1f}C", (pad_x, text_y), font=font, scale=scale, color=color)
    mid_t = (t_min + t_max) / 2.0
    draw_text_with_shadow(bar_img, f"{mid_t:.1f}C", (width // 2 - 20, text_y), font=font, scale=scale, color=color)
    draw_text_with_shadow(bar_img, f"{t_max:.1f}C", (width - pad_x - 48, text_y), font=font, scale=scale, color=color)

    return bar_img

# ==============================================================================
# Main Application
# ==============================================================================
def main():
    print("=" * 60)
    print("MLX90640 Thermal Viewer (TAAU Super-Resolution)")
    print(f"Port: {PORT} @ {BAUD_RATE} baud")
    print(f"Scale: {RAW_WIDTH}x{RAW_HEIGHT} -> {TARGET_WIDTH}x{TARGET_HEIGHT} (20x)")
    print(f"Fixed Range: {T_MIN:.1f}C to {T_MAX:.1f}C")
    print("Controls:")
    print("  [u] Cycle Scaling Mode (TAAU, Bilateral, Lanczos, Bicubic, Bilinear, None)")
    print("  [v] Toggle Vertical Flip")
    print("  [d] Toggle De-checkerboard Filter")
    print("  [s] Toggle Spatial Denoise (Gaussian)")
    print("  [q / ESC] Quit")
    print("=" * 60)

    try:
        reader = SerialThermalReader(PORT, BAUD_RATE)
    except serial.SerialException as err:
        print(f"Error opening port {PORT}: {err}")
        return

    flip_v = FLIP_VERTICAL
    dechecker_enabled = True
    spatial_blur_enabled = True
    temporal_alpha = 0.85

    # Upscaler state
    upscaler_idx = 0  # Default: TAAU (Temporal Anti-Aliasing Upsampling)
    taau = TAAUUpscaler(TARGET_WIDTH, TARGET_HEIGHT, alpha=0.80)

    color_bar = create_color_bar(TARGET_WIDTH, T_MIN, T_MAX)
    window_name = "MLX90640 Thermal Stream"
    cv2.namedWindow(window_name, cv2.WINDOW_AUTOSIZE)

    hover_pos = None
    mouse_moved = False

    def on_mouse(event, x, y, flags, param):
        nonlocal hover_pos, mouse_moved
        if event == cv2.EVENT_MOUSEMOVE:
            hover_pos = (x, y)
            mouse_moved = True

    cv2.setMouseCallback(window_name, on_mouse)

    prev_frame = None
    fps_ema = 0.0
    last_frame_time = time.perf_counter()

    base_thermal_view = None
    latest_upscaled = None
    hotspot_pos = (0, 0)
    hotspot_temp = 0.0

    try:
        while True:
            raw_frame = reader.get_latest_frame()
            new_data = (raw_frame is not None)

            if new_data:
                # Real-time smoothed FPS
                now = time.perf_counter()
                dt = now - last_frame_time
                last_frame_time = now
                if dt > 0:
                    inst_fps = 1.0 / dt
                    fps_ema = inst_fps if fps_ema == 0.0 else (0.85 * fps_ema + 0.15 * inst_fps)

                # --- Denoising & De-checkerboarding ---
                proc_frame = raw_frame
                if dechecker_enabled:
                    proc_frame = remove_checkerboard_artifact(proc_frame)

                if spatial_blur_enabled:
                    proc_frame = cv2.GaussianBlur(proc_frame, (3, 3), 0.5)

                # Flip if needed (upside-down fix)
                if flip_v:
                    proc_frame = cv2.flip(proc_frame, 0)

                # --- Advanced Super-Resolution & Upscaling ---
                if upscaler_idx == 0:
                    # Mode 0: TAAU (Temporal Anti-Aliasing Upsampling + Anti-Ghosting + CAS)
                    upscaled = taau.process(proc_frame)
                elif upscaler_idx == 1:
                    # Mode 1: Edge-Preserving Bilateral Super-Resolution
                    base = cv2.resize(proc_frame, (TARGET_WIDTH, TARGET_HEIGHT), interpolation=cv2.INTER_CUBIC)
                    smooth = cv2.bilateralFilter(base, d=5, sigmaColor=1.5, sigmaSpace=5.0)
                    detail = base - cv2.GaussianBlur(base, (0, 0), 2.0)
                    upscaled = smooth + 1.4 * detail
                elif upscaler_idx == 2:
                    # Mode 2: Lanczos-4 Sharp
                    upscaled = cv2.resize(proc_frame, (TARGET_WIDTH, TARGET_HEIGHT), interpolation=cv2.INTER_LANCZOS4)
                elif upscaler_idx == 3:
                    # Mode 3: Bicubic Smooth
                    upscaled = cv2.resize(proc_frame, (TARGET_WIDTH, TARGET_HEIGHT), interpolation=cv2.INTER_CUBIC)
                elif upscaler_idx == 4:
                    # Mode 4: Bilinear Standard
                    upscaled = cv2.resize(proc_frame, (TARGET_WIDTH, TARGET_HEIGHT), interpolation=cv2.INTER_LINEAR)
                else:
                    # Mode 5: None / Nearest Neighbor (unfiltered raw sensor grid)
                    upscaled = cv2.resize(proc_frame, (TARGET_WIDTH, TARGET_HEIGHT), interpolation=cv2.INTER_NEAREST)

                # Safe normalization (immune to NaNs or zero-division)
                diff = max(T_MAX - T_MIN, 1.0)
                norm = np.clip((upscaled - T_MIN) / diff * 255.0, 0.0, 255.0)
                normalized = np.nan_to_num(norm, nan=0.0).astype(np.uint8)

                # False-color thermal mapping
                color_mapped = cv2.applyColorMap(normalized, cv2.COLORMAP_INFERNO)

                # --- Hotspot Tracking ---
                max_idx = np.argmax(upscaled)
                max_y, max_x = np.unravel_index(max_idx, upscaled.shape)
                hotspot_pos = (max_x, max_y)
                hotspot_temp = float(upscaled[max_y, max_x])

                # Draw hotspot marker
                cv2.drawMarker(color_mapped, (max_x, max_y), (0, 0, 255), cv2.MARKER_CROSS, 16, 2, cv2.LINE_AA)
                cv2.circle(color_mapped, (max_x, max_y), 6, (0, 0, 255), 1, cv2.LINE_AA)

                # Hotspot label with boundary clamp & drop shadow
                label = f"HOT: {hotspot_temp:.1f}C"
                lbl_x = min(max_x + 10, TARGET_WIDTH - 110)
                lbl_y = max(max_y - 10, 24)
                draw_text_with_shadow(color_mapped, label, (lbl_x, lbl_y), scale=0.5, color=(0, 255, 255))

                base_thermal_view = color_mapped
                latest_upscaled = upscaled

            # Render display if new thermal frame arrived or mouse moved
            if (new_data or mouse_moved) and base_thermal_view is not None:
                mouse_moved = False
                view = base_thermal_view.copy()

                # HUD Overlays (FPS & Upscaler Mode) with drop shadow
                mode_str = UPSCALER_MODES[upscaler_idx]
                draw_text_with_shadow(view, f"FPS: {fps_ema:.1f}", (12, 28), scale=0.6, color=(255, 255, 255))
                draw_text_with_shadow(view, f"[U] {mode_str}", (12, 52), scale=0.5, color=(0, 255, 255))

                # Hover Inspector: show temperature of hovered pixel
                if hover_pos is not None and latest_upscaled is not None:
                    hx, hy = hover_pos
                    if 0 <= hx < TARGET_WIDTH and 0 <= hy < TARGET_HEIGHT:
                        hover_temp = float(latest_upscaled[hy, hx])

                        # Precision reticle with drop shadow
                        cv2.drawMarker(view, (hx + 1, hy + 1), (0, 0, 0), cv2.MARKER_CROSS, 12, 1, cv2.LINE_AA)
                        cv2.drawMarker(view, (hx, hy), (255, 255, 255), cv2.MARKER_CROSS, 12, 1, cv2.LINE_AA)
                        cv2.circle(view, (hx + 1, hy + 1), 3, (0, 0, 0), 1, cv2.LINE_AA)
                        cv2.circle(view, (hx, hy), 3, (255, 255, 255), 1, cv2.LINE_AA)

                        # Floating tooltip label
                        tip_text = f"{hover_temp:.1f}C"
                        (tw, th), _ = cv2.getTextSize(tip_text, cv2.FONT_HERSHEY_SIMPLEX, 0.48, 1)
                        tip_x = hx + 10
                        tip_y = hy - 6
                        if tip_x + tw > TARGET_WIDTH - 6:
                            tip_x = hx - tw - 10
                        if tip_y - th < 6:
                            tip_y = hy + th + 12
                        if abs(hx - hotspot_pos[0]) < 30 and abs(hy - hotspot_pos[1]) < 25:
                            tip_y = hy + th + 14

                        draw_text_with_shadow(view, tip_text, (tip_x, tip_y), scale=0.48, color=(255, 255, 255))
                        draw_text_with_shadow(view, f"Cursor: {hover_temp:.1f}C", (12, 74), scale=0.5, color=(255, 255, 255))

                    elif 0 <= hx < TARGET_WIDTH and TARGET_HEIGHT <= hy < TARGET_HEIGHT + color_bar.shape[0]:
                        pad_x = 24
                        bar_w = TARGET_WIDTH - 2 * pad_x
                        if pad_x <= hx <= pad_x + bar_w:
                            bar_temp = T_MIN + ((hx - pad_x) / bar_w) * (T_MAX - T_MIN)
                            draw_text_with_shadow(view, f"Bar: {bar_temp:.1f}C", (12, 74), scale=0.5, color=(255, 255, 255))

                display_frame = np.vstack([view, color_bar])
                cv2.imshow(window_name, display_frame)

            key = cv2.waitKey(1) & 0xFF
            if key in (ord('q'), 27):
                break
            elif key == ord('u'):
                upscaler_idx = (upscaler_idx + 1) % len(UPSCALER_MODES)
                taau.reset()
            elif key == ord('v'):
                flip_v = not flip_v
                taau.reset()
            elif key == ord('d'):
                dechecker_enabled = not dechecker_enabled
                taau.reset()
            elif key == ord('s'):
                spatial_blur_enabled = not spatial_blur_enabled
                taau.reset()

    except KeyboardInterrupt:
        print("\nViewer closed.")
    finally:
        reader.close()
        cv2.destroyAllWindows()

if __name__ == '__main__':
    main()
