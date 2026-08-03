#ifndef DEVICE_STATUS_H
#define DEVICE_STATUS_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  DEVICE_STATUS_UNKNOWN = 0,
  DEVICE_STATUS_OK,
  DEVICE_STATUS_WARN,
  DEVICE_STATUS_ERROR,
} device_status_t;

typedef enum {
  SCREEN_SPLASH = 0,
  SCREEN_OVERVIEW,
  SCREEN_TILT,
  SCREEN_ENVIRONMENT,
  SCREEN_CAN,
  SCREEN_POWER,
  SCREEN_COUNT
} screen_id_t;

// UI-facing derived state. Raw sensor readings live in sensor_snapshot instead.
struct device_status {
  uint32_t uptime_s;
  device_status_t overall_status;

  float tilt_x;
  float tilt_y;
  float tilt_z;
  device_status_t tilt_status;

  float env_value;
  const char *env_unit;
  const char *env_sensor_name;
  uint8_t env_channel;
  float env_voltage;
  device_status_t env_status;

  bool can_loopback_ok;
  uint32_t can_frame_id;
  uint32_t can_tx_interval_ms;
  uint32_t can_tx_count;
  device_status_t can_status;

  // Written by the UI Manager only; diagnostic, not read by any producer.
  screen_id_t active_screen;
};

void device_status_init(void);

// Fine-grained setters: each producer touches only the fields it owns.
void device_status_set_uptime(uint32_t uptime_s);
void device_status_set_overall_status(device_status_t status);
void device_status_set_tilt(float x, float y, float z, device_status_t status);
void device_status_set_environment(float value, const char *unit, const char *sensor_name,
                                    uint8_t channel, float voltage, device_status_t status);
void device_status_set_can(bool loopback_ok, uint32_t frame_id, uint32_t tx_interval_ms,
                            uint32_t tx_count, device_status_t status);
void device_status_set_active_screen(screen_id_t screen);

// Copies the whole struct out under the mutex. Never hand out a pointer to the
// live state — callers (esp. the UI Manager) must own their snapshot.
void device_status_get(struct device_status *out);

// Blocks until a setter has fired since the last wake, or timeout elapses.
// Wraps the coalescing binary semaphore so callers never touch k_sem directly.
int device_status_wait(k_timeout_t timeout);

#ifdef __cplusplus
}
#endif

#endif
