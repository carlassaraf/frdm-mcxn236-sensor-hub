// Synthetic tilt producer used until a real accelerometer driver is wired
// into device_status_set_tilt(). Generates a slow per-axis drift (as if the
// board were being tilted by hand) plus periodic "shake" bursts, so the Tilt
// screen's centered/stretching slider can be exercised without hardware.
#include <math.h>
#include <stdbool.h>
#include <zephyr/kernel.h>

#include "device_status.h"
#include "tilt_sim.h"

#define TILT_SIM_PERIOD_MS         50
#define TILT_SIM_SHAKE_PERIOD_MS   4000
#define TILT_SIM_SHAKE_DURATION_MS 800
#define TILT_SIM_SHAKE_AMPLITUDE_G 0.3f
// Low-pass time constant used to turn the per-tick white noise into
// continuous wobble instead of teleporting between independent samples.
#define TILT_SIM_JITTER_TAU_MS     80.0f

static struct k_work_delayable s_tilt_sim_work;
static uint32_t s_rng_state;
static int64_t s_last_update_ms;
static float s_jitter_x, s_jitter_y, s_jitter_z;

// Tiny self-contained xorshift32 PRNG so this doesn't depend on the entropy
// subsystem (no CONFIG_ENTROPY_GENERATOR in this build).
static uint32_t tilt_sim_rand(void)
{
  s_rng_state ^= s_rng_state << 13;
  s_rng_state ^= s_rng_state >> 17;
  s_rng_state ^= s_rng_state << 5;
  return s_rng_state;
}

// Uniform jitter in [-amplitude, +amplitude].
static float tilt_sim_jitter(float amplitude)
{
  int32_t r = (int32_t)(tilt_sim_rand() % 2001) - 1000; // -1000..1000
  return (r / 1000.0f) * amplitude;
}

static void tilt_sim_update(struct k_work *work)
{
  int64_t now_ms = k_uptime_get();
  int32_t elapsed_ms = (int32_t)(now_ms - s_last_update_ms);
  s_last_update_ms = now_ms;
  float t_s = now_ms / 1000.0f;

  // Slow wandering drift, phase/frequency offset per axis so they don't move
  // in lockstep. Z rests near +1g, as if gravity were pulling on a flat board.
  float x = 0.5f * sinf(t_s * 0.3f);
  float y = 0.4f * sinf(t_s * 0.21f + 1.0f);
  float z = 1.0f + 0.05f * sinf(t_s * 0.17f + 2.0f);

  // Every TILT_SIM_SHAKE_PERIOD_MS, spend a short window shaking to exercise
  // the delta/stretch behavior on top of the slow drift. Each axis chases a
  // fresh random target through a low-pass filter instead of jumping straight
  // to it, so consecutive samples stay continuous instead of teleporting;
  // the same filter relaxes the jitter back to 0 once the window ends, so
  // there's no discontinuity at the edges of the burst either.
  bool shaking = (now_ms % TILT_SIM_SHAKE_PERIOD_MS) < TILT_SIM_SHAKE_DURATION_MS;
  float target_x = shaking ? tilt_sim_jitter(TILT_SIM_SHAKE_AMPLITUDE_G) : 0.0f;
  float target_y = shaking ? tilt_sim_jitter(TILT_SIM_SHAKE_AMPLITUDE_G) : 0.0f;
  float target_z = shaking ? tilt_sim_jitter(TILT_SIM_SHAKE_AMPLITUDE_G) : 0.0f;

  float alpha = 1.0f - expf(-(float)elapsed_ms / TILT_SIM_JITTER_TAU_MS);
  s_jitter_x += (target_x - s_jitter_x) * alpha;
  s_jitter_y += (target_y - s_jitter_y) * alpha;
  s_jitter_z += (target_z - s_jitter_z) * alpha;

  device_status_set_tilt(x + s_jitter_x, y + s_jitter_y, z + s_jitter_z, DEVICE_STATUS_OK);

  k_work_reschedule(&s_tilt_sim_work, K_MSEC(TILT_SIM_PERIOD_MS));
}

void tilt_sim_start(void)
{
  s_rng_state = (uint32_t)k_uptime_get() | 1u; // must be non-zero
  s_last_update_ms = k_uptime_get();
  k_work_init_delayable(&s_tilt_sim_work, tilt_sim_update);
  k_work_reschedule(&s_tilt_sim_work, K_NO_WAIT);
}
