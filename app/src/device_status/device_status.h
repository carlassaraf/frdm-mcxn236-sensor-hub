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
#define DEVICE_STATUS_EVT_DISPLAY       BIT(5)
#define DEVICE_STATUS_EVT_ALL           (DEVICE_STATUS_EVT_UPTIME | DEVICE_STATUS_EVT_OVERALL | DEVICE_STATUS_EVT_TILT | DEVICE_STATUS_EVT_ENVIRONMENT | DEVICE_STATUS_EVT_CAN | DEVICE_STATUS_EVT_DISPLAY)

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

struct device_status_device {
  uint32_t uptime_s;
  device_status_t overall_status;
  // Written by the UI Manager only; diagnostic, not read by any producer.
  screen_id_t active_screen;
  bool display_sleeping;
};

struct device_status_tilt {
  float x;
  float y;
  float z;
  device_status_t status;
};

struct device_status_environment {
  float value;
  const char *sensor_name;
  uint8_t channel;
  float voltage;
  device_status_t status;
};

struct device_status_can {
  bool loopback_ok;
  uint32_t frame_id;
  uint32_t tx_interval_ms;
  uint32_t tx_count;
  device_status_t status;
};

// Latest readings and derived status from every producer, grouped by section.
// Sections are embedded by value: still one struct, one mutex, one copy.
struct device_status {
  struct device_status_device device;
  struct device_status_tilt tilt;
  struct device_status_environment environment;
  struct device_status_can can;
};

void device_status_init(void);

// Fine-grained setters: each producer touches only the fields it owns.
void device_status_set_uptime(uint32_t uptime_s);
void device_status_set_tilt(float x, float y, float z, device_status_t status);
void device_status_set_environment_identity(const char *sensor_name, uint8_t channel);
void device_status_set_environment(float value, float voltage, device_status_t status);
void device_status_set_can(bool loopback_ok, uint32_t frame_id, uint32_t tx_interval_ms,
                            uint32_t tx_count, device_status_t status);
void device_status_set_active_screen(screen_id_t screen);
void device_status_set_display_sleeping(bool is_sleeping);

/**
 * @brief Copies the whole struct under the mutex
 * @param out Pointer to copy the values
 */
void device_status_get(struct device_status *out);

/**
 * @brief Copies a single section under the mutex. Prefer these over
 * device_status_get() when a consumer only needs one section.
 * @param out Pointer to copy the values
 */
void device_status_get_device(struct device_status_device *out);
void device_status_get_tilt(struct device_status_tilt *out);
void device_status_get_environment(struct device_status_environment *out);
void device_status_get_can(struct device_status_can *out);

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
