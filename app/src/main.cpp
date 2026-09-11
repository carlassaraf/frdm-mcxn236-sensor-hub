#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/app_version.h>

#include "device_status.h"
#include "ui_manager.h"
#include "mq.h"
#if defined(CONFIG_TILT_SIM)
#include "tilt_sim.h"
#endif
#if defined(CONFIG_ENV_SIM)
#include "env_sim.h"
#endif

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

struct k_work_delayable uptime_work;
static void device_status_uptime_update(struct k_work *work);

/*
 * Standalone validation poll (ROADMAP.md #3's "validate standalone before
 * integrating into the main app" step) -- not the real sensor-sampling-thread
 * integration from ROADMAP.md #4 yet, just enough to confirm the driver
 * produces sane ppm/alarm values now that Ro is calibrated.
 */
#if DT_NODE_EXISTS(DT_NODELABEL(mq2))
static const struct device *mq2_dev = DEVICE_DT_GET(DT_NODELABEL(mq2));
static struct k_work_delayable mq2_poll_work;

static void mq2_poll_update(struct k_work *work)
{
  int ret = sensor_sample_fetch(mq2_dev);
  if (ret != 0) {
    LOG_ERR("MQ-2 sample fetch failed: %d", ret);
  } else {
    struct sensor_value smoke;
    struct sensor_value voltage;

    if (sensor_channel_get(mq2_dev, (enum sensor_channel)SENSOR_CHAN_MQ_SMOKE, &smoke) == 0 &&
        sensor_channel_get(mq2_dev, (enum sensor_channel)SENSOR_CHAN_MQ_MV, &voltage) == 0) {
      // Update device_status to be read by screen
      float smoke_f = sensor_value_to_float(&smoke);
      float voltage_f = sensor_value_to_float(&voltage);
      LOG_INF("smoke = %.2f ppm, voltage = %.3f", smoke_f, voltage_f);
      device_status_set_environment(smoke_f, voltage_f, DEVICE_STATUS_OK);
    }
  }

  k_work_reschedule(&mq2_poll_work, K_SECONDS(1));
}
#endif

int main(void)
{
  const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
  if(!gpio_is_ready_dt(&btn)) {
    LOG_ERR("SW2 not ready");
  }
  if(gpio_pin_configure_dt(&btn, GPIO_INPUT)) {
    LOG_ERR("SW2 not configured");
  }

  device_status_init();
  ui_manager_init();

  k_work_init_delayable(&uptime_work, device_status_uptime_update);
  k_work_reschedule(&uptime_work, K_SECONDS(1));

#if DT_NODE_EXISTS(DT_NODELABEL(mq2))
  if (!device_is_ready(mq2_dev)) {
    LOG_ERR("MQ-2 device not ready");
  } else {
    device_status_set_environment_identity(mq2_dev->name, DT_IO_CHANNELS_INPUT(DT_NODELABEL(mq2)));
    k_work_init_delayable(&mq2_poll_work, mq2_poll_update);
    k_work_reschedule(&mq2_poll_work, K_SECONDS(1));
  }
#endif

#if defined(CONFIG_TILT_SIM)
  tilt_sim_start();
#endif

  return 0;
}

/**
 * @brief Work handler called every second to update the device uptime
 */
static void device_status_uptime_update(struct k_work *work)
{
  uint32_t uptime_s = k_uptime_get() / 1000;
  device_status_set_uptime(uptime_s);
  k_work_reschedule(&uptime_work, K_SECONDS(1));
}