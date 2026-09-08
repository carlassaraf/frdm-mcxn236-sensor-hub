#include "device_status.h"
#include "ui.h"
#include "ui_adapters.h"
#include <math.h>

#define TAU_MS 100.0f
// Floor on the slider's half-width so it never visually collapses to nothing
// when the axis is calm. ~0.72px per raw unit on this 288px/-200..200 slider,
// so 13 raw units on each side is ~19px of total bar width.
#define TILT_BAR_MIN_DELTA_RAW 13
// Low-pass time constant for the bar's *position* (center). Shorter than
// TAU_MS so it still tracks real tilt promptly, but long enough to smooth
// out per-sample noise instead of snapping the whole bar sideways on it.
#define POS_TAU_MS 150.0f

// Per-axis smoothing state, carried across calls.
typedef struct {
  float prev;         // last raw sample, for delta detection
  float disp_delta;   // decaying peak-hold delta, drives the bar's width
  float disp_x;       // low-passed position, drives the bar's center
} tilt_axis_state_t;

static void tilt_axis_update(tilt_axis_state_t *st, float raw, int32_t elapsed_ms,
                              lv_obj_t *bar, lv_obj_t *label)
{
  float raw_delta = fabsf(raw - st->prev);
  st->prev = raw;

  float factor = expf(-elapsed_ms / TAU_MS);
  st->disp_delta = fmaxf(raw_delta, st->disp_delta * factor);
  st->disp_delta = fminf(st->disp_delta, 2.0f);

  // Low-pass the *position* separately from the width: the width should
  // still react instantly to a real delta (that's the point), but the
  // center jumping to every noisy raw sample is what reads as jittery, not
  // "shaking". Blend toward the new sample instead of snapping to it.
  float pos_alpha = 1.0f - expf(-elapsed_ms / POS_TAU_MS);
  st->disp_x += (raw - st->disp_x) * pos_alpha;

  int32_t x_raw     = (int32_t)(st->disp_x * 100.0f);
  int32_t delta_raw = (int32_t)(st->disp_delta * 100.0f);
  if (delta_raw < TILT_BAR_MIN_DELTA_RAW) {
    delta_raw = TILT_BAR_MIN_DELTA_RAW;
  }

  int32_t start = CLAMP(x_raw - delta_raw, -200, 200);
  int32_t end   = CLAMP(x_raw + delta_raw, -200, 200);
  lv_slider_set_start_value(bar, start, LV_ANIM_OFF);
  lv_slider_set_value(bar, end, LV_ANIM_OFF);

  // Read off the same smoothed disp_x the bar's center uses, so the number
  // doesn't flicker independently of what the bar is showing. Split sign out
  // once, then work with the magnitude for both digits -- truncating a float
  // in (-1, 1) to int drops the sign (e.g. (int)-0.35 == 0), so the sign has
  // to be captured explicitly rather than inferred per-digit.
  float mag = fabsf(st->disp_x);
  lv_label_set_text_fmt(label, "%s%d.%02d g", st->disp_x < 0 ? "-" : "",
                         (int32_t)mag, (int32_t)(mag * 100.0f) % 100);
}

static void scrTilt_step(void)
{
  static int64_t last_update_ms = 0;
  static tilt_axis_state_t x_state, y_state, z_state;

  int64_t now_ms = k_uptime_get();
  int32_t elapsed_ms = (int32_t)(now_ms - last_update_ms);
  last_update_ms = now_ms;

  struct device_status status;
  device_status_get(&status);

  if(status.tilt_status != DEVICE_STATUS_OK) { return; }

  tilt_axis_update(&x_state, status.tilt_x, elapsed_ms, ui_axisXbar, ui_axisXv);
  tilt_axis_update(&y_state, status.tilt_y, elapsed_ms, ui_axisYbar, ui_axisYv);
  tilt_axis_update(&z_state, status.tilt_z, elapsed_ms, ui_axisZbar, ui_axisZv);
}

static void scrTilt_load(void)
{
  _ui_screen_change(&ui_scrTilt, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_scrTilt_screen_init);
}

static void scrTilt_unload(void)
{
  _ui_screen_delete(ui_scrTilt_screen_destroy);
}

const screen_ops_t scrTilt_ops = { "Tilt", scrTilt_load, scrTilt_unload, NULL, scrTilt_step };
