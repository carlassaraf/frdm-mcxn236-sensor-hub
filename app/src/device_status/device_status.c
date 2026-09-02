#include "device_status.h"

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

void device_status_set_uptime(uint32_t uptime_s)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  s_status.uptime_s = uptime_s;
  k_mutex_unlock(&s_mutex);
  notify_changed(DEVICE_STATUS_EVT_UPTIME);
}

void device_status_set_overall_status(device_status_t status)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  s_status.overall_status = status;
  k_mutex_unlock(&s_mutex);
  notify_changed(DEVICE_STATUS_EVT_OVERALL);
}

void device_status_set_tilt(float x, float y, float z, device_status_t status)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  s_status.tilt_x = x;
  s_status.tilt_y = y;
  s_status.tilt_z = z;
  s_status.tilt_status = status;
  k_mutex_unlock(&s_mutex);
  notify_changed(DEVICE_STATUS_EVT_TILT);
}

void device_status_set_environment_identity(const char *sensor_name, uint8_t channel)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  s_status.env_sensor_name = sensor_name;
  s_status.env_channel = channel;
  k_mutex_unlock(&s_mutex);
  notify_changed(DEVICE_STATUS_EVT_ENVIRONMENT);
}

void device_status_set_environment(float value, float voltage, device_status_t status)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  s_status.env_value = value;
  s_status.env_voltage = voltage;
  s_status.env_status = status;
  k_mutex_unlock(&s_mutex);
  notify_changed(DEVICE_STATUS_EVT_ENVIRONMENT);
}

void device_status_set_can(bool loopback_ok, uint32_t frame_id, uint32_t tx_interval_ms,
                            uint32_t tx_count, device_status_t status)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  s_status.can_loopback_ok = loopback_ok;
  s_status.can_frame_id = frame_id;
  s_status.can_tx_interval_ms = tx_interval_ms;
  s_status.can_tx_count = tx_count;
  s_status.can_status = status;
  k_mutex_unlock(&s_mutex);
  notify_changed(DEVICE_STATUS_EVT_CAN);
}

void device_status_set_active_screen(screen_id_t screen)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  s_status.active_screen = screen;
  k_mutex_unlock(&s_mutex);
  // Diagnostic-only field: no producer waits on it, no need to wake anyone.
}

void device_status_get(struct device_status *out)
{
  k_mutex_lock(&s_mutex, K_FOREVER);
  *out = s_status;
  k_mutex_unlock(&s_mutex);
}

int device_status_wait(uint32_t events_mask, k_timeout_t timeout)
{
  return k_event_wait_safe(&s_changed, events_mask, false, timeout);
}