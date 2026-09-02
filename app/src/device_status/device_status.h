#ifndef DEVICE_STATUS_H
#define DEVICE_STATUS_H

#include <zephyr/kernel.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_STATUS_EVT_UPTIME        BIT(0)
#define DEVICE_STATUS_EVT_OVERALL       BIT(1)
#define DEVICE_STATUS_EVT_TILT          BIT(2)
#define DEVICE_STATUS_EVT_ENVIRONMENT   BIT(3)
#define DEVICE_STATUS_EVT_CAN           BIT(4)
#define DEVICE_STATUS_EVT_ALL           (DEVICE_STATUS_EVT_UPTIME | DEVICE_STATUS_EVT_OVERALL | DEVICE_STATUS_EVT_TILT | DEVICE_STATUS_EVT_ENVIRONMENT | DEVICE_STATUS_EVT_CAN)

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
void device_status_set_environment_identity(const char *sensor_name, uint8_t channel);
void device_status_set_environment(float value, float voltage, device_status_t status);
void device_status_set_can(bool loopback_ok, uint32_t frame_id, uint32_t tx_interval_ms,
                            uint32_t tx_count, device_status_t status);
void device_status_set_active_screen(screen_id_t screen);

/**
 * @brief Copies the whole struct under the mutex
 * @param out Pointer to copy the values
 */
void device_status_get(struct device_status *out);

/** 
 * @brief Blocks until the event bit has been set
 * @param events_mask One of the available DEVICE_STATUS_EVT masks
 * @param timeout Time to wait for event
 */
int device_status_wait(uint32_t events_mask, k_timeout_t timeout);

#ifdef __cplusplus
}
#endif

#endif
