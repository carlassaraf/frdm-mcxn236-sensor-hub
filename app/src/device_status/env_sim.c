// Synthetic environment producer used until a real gas/environment sensor
// driver is wired into device_status_set_environment(). Generates a slowly
// wandering ppm reading around a baseline (as if CO2 were building up and
// venting in a room) with a plausible ADC voltage to match, so the
// Environment screen can be exercised without hardware. Unit is always ppm,
// so unlike device_status_set_environment() there's no unit to thread through
// here.
#include <math.h>
#include <zephyr/kernel.h>

#include "device_status.h"
#include "env_sim.h"

#define ENV_SIM_PERIOD_MS       1000
#define ENV_SIM_SENSOR_NAME     "env-sim"
#define ENV_SIM_CHANNEL         0

#define ENV_SIM_BASELINE_PPM    420.0f
#define ENV_SIM_DRIFT_PPM       60.0f
#define ENV_SIM_NOISE_PPM       5.0f
#define ENV_SIM_PPM_MAX         2000.0f
// Simulated ADC voltage range the ppm reading is mapped onto, as if driven
// by an MQ135-style analog gas sensor.
#define ENV_SIM_VOLTAGE_MIN     0.4f
#define ENV_SIM_VOLTAGE_MAX     2.8f

static struct k_work_delayable s_env_sim_work;
static uint32_t s_rng_state;

// Tiny self-contained xorshift32 PRNG so this doesn't depend on the entropy
// subsystem (no CONFIG_ENTROPY_GENERATOR in this build).
static uint32_t env_sim_rand(void)
{
  s_rng_state ^= s_rng_state << 13;
  s_rng_state ^= s_rng_state >> 17;
  s_rng_state ^= s_rng_state << 5;
  return s_rng_state;
}

// Uniform jitter in [-amplitude, +amplitude].
static float env_sim_jitter(float amplitude)
{
  int32_t r = (int32_t)(env_sim_rand() % 2001) - 1000; // -1000..1000
  return (r / 1000.0f) * amplitude;
}

static void env_sim_update(struct k_work *work)
{
  float t_s = k_uptime_get() / 1000.0f;

  // Slow wandering drift around the baseline plus small per-sample noise.
  float ppm = ENV_SIM_BASELINE_PPM + ENV_SIM_DRIFT_PPM * sinf(t_s * 0.05f) +
              env_sim_jitter(ENV_SIM_NOISE_PPM);
  if(ppm < 0.0f) {
    ppm = 0.0f;
  }

  float frac = ppm / ENV_SIM_PPM_MAX;
  if(frac > 1.0f) {
    frac = 1.0f;
  }
  float voltage = ENV_SIM_VOLTAGE_MIN + frac * (ENV_SIM_VOLTAGE_MAX - ENV_SIM_VOLTAGE_MIN);

  device_status_set_environment(ppm, voltage, DEVICE_STATUS_OK);

  k_work_reschedule(&s_env_sim_work, K_MSEC(ENV_SIM_PERIOD_MS));
}

void env_sim_start(void)
{
  s_rng_state = (uint32_t)k_uptime_get() | 1u; // must be non-zero
  // Identity is fixed for the sim's lifetime, so it's set once here rather
  // than on every reading.
  device_status_set_environment_identity(ENV_SIM_SENSOR_NAME, ENV_SIM_CHANNEL);
  k_work_init_delayable(&s_env_sim_work, env_sim_update);
  k_work_reschedule(&s_env_sim_work, K_NO_WAIT);
}
