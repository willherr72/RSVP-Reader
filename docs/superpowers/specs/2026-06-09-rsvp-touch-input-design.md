# RSVP Reader — Touch Input Reliability & Gesture Controls — Design Spec

**Date:** 2026-06-09
**Status:** Approved (brainstorm + root-cause confirmed) — ready for implementation planning
**Target hardware:** Waveshare **ESP32-S3-Touch-LCD-3.49** (AXS15231B panel + touch, 640×172 landscape)
**Firmware stack:** ESP-IDF 5.5.2 + LVGL 9

---

## 1. Summary

The live RSVP reader runs on hardware, but **touch input is unreliable**: quick taps
often register nothing, and swipes produce nonsense (impossible deltas, merged
touches). This work makes touch reliable and wires the real reading controls:
**tap = play/pause, swipe ↑/↓ = WPM, swipe ←/→ = sentence**.

**Definition of done:** on-device, ~20 consecutive taps all toggle play/pause;
vertical swipes step WPM and the status line reflects it; horizontal swipes jump
sentence; measured LVGL touch-event processing rate ≥ 30 Hz (target ~60 Hz) with
clean 1:1 PRESSED/RELEASED pairs and in-bounds deltas; no debug scaffolding left.

---

## 2. Root cause (confirmed by on-device measurement)

The hand-rolled I2C read was **wrongly suspected**. The same read ran in the
Waveshare LVGL demo and worked great; instrumentation proved it detects fingers
fine (constant `touch PRESS edge` logs).

The defect is **poll starvation on LVGL's single render thread.** LVGL polls and
processes the touch indev from `lv_timer_handler`, the same loop that renders and
flushes. In `LV_DISPLAY_RENDER_MODE_FULL`, *any* dirty pixel reflushes the entire
640×172×2 ≈ 220 KB framebuffer over QSPI (plus a software rotate), blocking the
thread. Continuous redraws therefore strangle the touch poll.

Measured `reads/s` vs. the active redraw phase (auto-toggled every 5 s in one run):

| Redraw phase | reads/s | Effective touch poll rate |
|---|---|---|
| 30 fps debug label redraw **on** | 8, 8, 8, 8, 8, 8 … | **~8 Hz** |
| debug redraw **off** | 14, 15, 15, 16, 17, 18 … | **~15 Hz** |

Two corollaries the experiment surfaced:

1. **Even ~15 Hz corrupts the event stream**, it isn't merely sparse. The read
   logged ~30 press edges but LVGL emitted ~5 `PRESSED` events, with deltas like
   `dx=45 dy=483` (impossible on a 172 px-tall screen) — at this rate a press and
   the *next* tap merge into one LVGL gesture. So removing the debug redraw alone
   is **not sufficient**.
2. **Second redraw culprit + a coordinate bug.** `CONFIG_LV_USE_PERF_MONITOR` /
   `LV_USE_SYSMON` are enabled — LVGL's FPS overlay forces its own periodic
   full-frame flushes, capping the idle rate at ~15 Hz instead of LVGL's ~33 Hz
   ceiling. Separately, the LVGL event point is a 90° rotation of the read
   coordinate (read `(133,108)` → event `(531,133)` ≈ `(640−y, x)`): we feed
   raw-landscape coords but `lv_display_set_rotation(90)` rotates the indev point
   on top, so presses land mis-mapped / out of bounds.

**Root cause statement:** Touch is polled on the render thread; continuous
full-frame redraws (debug label + perf-monitor overlay + FULL-mode QSPI flush)
starve the poll to 8–15 Hz, where LVGL's pointer state machine corrupts presses
into merged/garbage gestures — compounded by an indev coordinate/rotation
mismatch. The I2C read protocol is innocent.

---

## 3. Decisions log (and rationale)

| Decision | Choice | Why |
|---|---|---|
| Primary fix | **Cut render load so the LVGL thread can process input** | Load-bearing. Restores indev processing to ≥30 Hz; without it nothing else helps because LVGL processes touch on the render thread. |
| Continuous-redraw sources | **Remove all of them**: delete the 30 fps debug redraw, turn off `LV_USE_PERF_MONITOR` + `LV_USE_SYSMON`, invalidate labels only on actual change | Each forces a full-frame QSPI flush that blocks the thread. |
| Touch sampling | **Dedicated FreeRTOS task** polling the `esp_lcd_touch` driver at ~100 Hz into a lock-guarded snapshot; LVGL `read_cb` just copies the snapshot | Decouples I2C sampling from render; the indev callback never blocks on I2C and always has a ≤~10 ms-old sample. |
| Touch driver | **`esp_lcd_touch_new_i2c_axs15231b`** from the already-pinned `esp_lcd_axs15231b ^1.0.1` | Maintained, correct parse/transform, supports config-flag mirror/swap and optional INT. No version bump (2.x targets IDF 6.0; we're on 5.5.2). Touch API confirmed present in 1.0.1. |
| Indev/refresh rate | **Lower LVGL indev read + display refresh periods toward ~16 ms (~60 Hz)** once load is cut | "All for increasing the speed." Headroom exists after the redraw cut; tune empirically. |
| Coordinate transform | **Set `esp_lcd_touch_config_t` x_max/y_max + mirror/swap flags to match LVGL's logical (pre-rotation) space**; verify by tapping known corners | Replaces the ad-hoc manual swap/clamp with the driver's transform, aligned to the 90° display rotation. Exact flags dialed in on hardware. |
| Gesture classification | **Pure `classifyGesture()` in `core/`, host-tested** | Threshold/axis logic is testable without hardware; keeps `ui_reader.cpp` thin. Matches the project's 66-case host-test pattern. |
| Gesture map | tap → `togglePlay`; swipe ↑ → WPM +25; swipe ↓ → WPM −25; swipe ← → `prevSentence`; swipe → → `nextSentence` | Confirmed in brainstorming. WPM clamps 100–800 via existing `set_wpm()`. |
| Instrumentation | **Keep during bring-up, remove as the final step** | We re-measure poll/processing rate after each change to confirm we cleared the target before declaring done. |

---

## 4. Components

### 4.1 `core/` — gesture classification (host-tested)
New `core/include/rsvp/gesture.hpp` (+ `core/src/gesture.cpp` if non-trivial):

```cpp
namespace rsvp {
enum class Gesture { None, Tap, SwipeUp, SwipeDown, SwipeLeft, SwipeRight };

// dx,dy are release-minus-press in LVGL screen pixels (y grows downward).
// |delta| ≤ tapRadius → Tap; otherwise the dominant axis sets the swipe
// direction. Pure and deterministic. Always returns Tap or a Swipe* —
// `None` is reserved for the caller's "no gesture pending" state.
Gesture classifyGesture(int dx, int dy, int tapRadius);
}
```
Registered in `core/CMakeLists.txt` and `test/host/CMakeLists.txt`; new
`test/host/test_gesture.cpp` covering: tap within radius, each of the four swipe
directions, axis-dominance ties, and exact-boundary values.

### 4.2 `firmware/main/main.c` — touch driver + dedicated read task
- Replace `TouchInputReadCallback`'s hand-rolled I2C poll. Create the touch handle
  once at bring-up: `ESP_LCD_TOUCH_IO_I2C_AXS15231B_CONFIG()` → `esp_lcd_panel_io_i2c`
  on the existing `user_i2c_port1_handle` (shared bus; the i2c_master driver
  serializes device access) → `esp_lcd_touch_config_t{ x_max, y_max, rst=-1,
  int=-1, flags=… }` → `esp_lcd_touch_new_i2c_axs15231b()`.
- A dedicated FreeRTOS task polls `esp_lcd_touch_read_data` + `…_get_coordinates`
  at ~100 Hz and stores `{pressed, x, y}` in a snapshot guarded by a spinlock.
- The LVGL `read_cb` copies the snapshot into `lv_indev_data_t` — no I2C, no block.
- Drop `read_touchpad_cmd` and the touch use of `disp_touch_dev_handle`.

### 4.3 `firmware/sdkconfig.defaults` — disable the FPS overlay
Add `CONFIG_LV_USE_PERF_MONITOR=n` and `CONFIG_LV_USE_SYSMON=n` (and `STRESS`/demo
flags if they pull in monitor draw). Removes the overlay's periodic full-frame flush.

### 4.4 `firmware/main/ui_reader.cpp` — render-on-change + gesture wiring
- `tick_cb`: remove the unconditional per-tick label write; redraw only when the
  word index changes; restore `update_status()` for the `wpm · %` line.
- `touch_event_cb`: on `RELEASED`, compute press→release delta, call
  `classifyGesture`, dispatch: Tap → `g_player->togglePlay()` + status refresh;
  SwipeUp/Down → `set_wpm(g_wpm ± 25)`; SwipeLeft → `do_prev_sentence()`;
  SwipeRight → `do_next_sentence()`. (`set_wpm` / `do_*_sentence` already exist.)
- Remove `g_pc/g_rc/g_ldx/g_ldy` and all debug logging once the rate target is met.

---

## 5. Data flow

```
touch task (~100 Hz): esp_lcd_touch_read_data → get_coordinates → snapshot{pressed,x,y}
                                                                        │ (spinlock)
LVGL thread (load now light): indev read_cb copies snapshot → pointer state
   → PRESSED/RELEASED on the full-screen clickable layer
   → touch_event_cb: classifyGesture(press→release Δ) → Player control
   → refresh_word() / update_status()
```

## 6. Error handling
- `esp_lcd_touch_read_data` returns `esp_err_t`; on non-OK the task keeps the prior
  snapshot as RELEASED and logs at debug level — never aborts.
- WPM clamped 100–800 (existing `set_wpm`).
- Snapshot access is brief and lock-guarded; the read_cb tolerates a stale sample.
- Null-guard `g_player` in the event path.

## 7. Verification
- **Host:** `cmake --build build/host && build/host/Debug/rsvp_tests.exe` green,
  including the new `test_gesture` cases (no firmware regressions in core).
- **On-device (board connected), measured via the temporary instrumentation:**
  1. After the render-load cut: idle `reads/s` / event-processing rate ≥ 30 Hz
     (target ~60 Hz after lowering indev/refresh periods).
  2. ~20 consecutive taps → ~20 clean `PRESSED`/`RELEASED` pairs, deltas in-bounds.
  3. Corner-tap check: LVGL point matches the physical touch location (coordinate
     transform correct).
  4. Each gesture performs its mapped action; status line tracks WPM/%.
- **Final:** strip instrumentation, rebuild, confirm controls still reliable.

## 8. Out of scope (non-goals for this work)
- INT-pin (IRQ-driven) touch — optional future enhancement; polling task suffices.
- Partial/direct LVGL render mode — revisit only if the render-load cut doesn't
  clear the rate target.
- SD/EPUB loading, library/settings screens, IMU auto-rotate, real battery, Wi-Fi
  (separate roadmap items).
```
