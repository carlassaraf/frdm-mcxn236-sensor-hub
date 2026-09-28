#include "device_status.h"
#include <ctype.h>

static struct device_status s_status;
static struct k_mutex s_mutex;
static struct k_event s_changed;

void device_status_init(void)
{
  k_mutex_init(&s_mutex);
  // Binary semaphore, starts empty: a give with no taker pending just leaves
  // it at 1, so bursts of writes between two waits coalesce for free.
  k_event_init(&s_changed);
}

static void notify_changed(uint32_t bits)
{
  k_event_post(&s_changed, bits);
}

static void update_overall_status(void)
{
  // No lock since this is called within a mutex lock
  const device_status_t sections[] = {
    s_status.tilt.status, s_status.environment.status, s_status.can.status,
  };
  // Worst status among sections that have reported; UNKNOWN only if none have.
  // Relies on the enum order UNKNOWN < OK < WARN < ERROR.
  device_status_t overall = DEVICE_STATUS_UNKNOWN;
  for (size_t i = 0; i < ARRAY_SIZE(sections); i++) {
    if (sections[i] > overall) {
      overall = sections[i];
    }
  }
  s_status.device.overall_status = overall;
  notify_changed(DEVICE_STATUS_EVT_OVERALL);
}

void device_status_set_uptime(uint32_t uptime_s)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  s_status.device.uptime_s = uptime_s;
  k_mutex_unlock(&s_mutex);
  notify_changed(DEVICE_STATUS_EVT_UPTIME);
}

void device_status_set_tilt(float x, float y, float z, device_status_t status)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  s_status.tilt.x = x;
  s_status.tilt.y = y;
  s_status.tilt.z = z;

  device_status_t prev = s_status.tilt.status;
  s_status.tilt.status = status;
  // Update only if status changed
  if (prev != status) {
    update_overall_status();
  }
  k_mutex_unlock(&s_mutex);
  notify_changed(DEVICE_STATUS_EVT_TILT);
}

void device_status_set_environment_identity(const char *sensor_name, uint8_t channel)
{
  static char name[4];
  strncpy(name, sensor_name, 3);
  name[0] = toupper(name[0]); name[1] = toupper(name[1]);

  k_mutex_lock(&s_mutex, K_FOREVER);
  s_status.environment.sensor_name = name;
  s_status.environment.channel = channel;
  k_mutex_unlock(&s_mutex);
  notify_changed(DEVICE_STATUS_EVT_ENVIRONMENT);
}

void device_status_set_environment(float value, float voltage, device_status_t status)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  s_status.environment.value = value;
  s_status.environment.voltage = voltage;

  device_status_t prev = s_status.environment.status;
  s_status.environment.status = status;
  // Update only if status changed
  if (prev != status) {
    update_overall_status();
  }
  k_mutex_unlock(&s_mutex);
  notify_changed(DEVICE_STATUS_EVT_ENVIRONMENT);
}

void device_status_set_can(bool loopback_ok, uint32_t frame_id, uint32_t tx_interval_ms,
                            uint32_t tx_count, device_status_t status)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  s_status.can.loopback_ok = loopback_ok;
  s_status.can.frame_id = frame_id;
  s_status.can.tx_interval_ms = tx_interval_ms;
  s_status.can.tx_count = tx_count;

  device_status_t prev = s_status.can.status;
  s_status.can.status = status;
  // Update only if status changed
  if (prev != status) {
    update_overall_status();
  }
  k_mutex_unlock(&s_mutex);
  notify_changed(DEVICE_STATUS_EVT_CAN);
}

void device_status_set_active_screen(screen_id_t screen)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  s_status.device.active_screen = screen;
  k_mutex_unlock(&s_mutex);
  // Diagnostic-only field: no producer waits on it, no need to wake anyone.
}

void device_status_set_display_sleeping(bool is_sleeping)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  s_status.device.display_sleeping = is_sleeping;
  k_mutex_unlock(&s_mutex);
  notify_changed(DEVICE_STATUS_EVT_DISPLAY);
}

void device_status_get(struct device_status *out)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  *out = s_status;
  k_mutex_unlock(&s_mutex);
}

void device_status_get_device(struct device_status_device *out)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  *out = s_status.device;
  k_mutex_unlock(&s_mutex);
}

void device_status_get_tilt(struct device_status_tilt *out)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  *out = s_status.tilt;
  k_mutex_unlock(&s_mutex);
}

void device_status_get_environment(struct device_status_environment *out)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  *out = s_status.environment;
  k_mutex_unlock(&s_mutex);
}

void device_status_get_can(struct device_status_can *out)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  *out = s_status.can;
  k_mutex_unlock(&s_mutex);
}

int device_status_wait(uint32_t events_mask, k_timeout_t timeout)
{
  return k_event_wait_safe(&s_changed, events_mask, false, timeout);
}