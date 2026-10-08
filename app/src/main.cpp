#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/app_version.h>

#include "device_status.h"
#include "can_service.h"
#include "ui_manager.h"
#include "mq.h"
#if defined(CONFIG_TILT_SIM)
#include "tilt_sim.h"
#endif
#if defined(CONFIG_ENV_SIM)
#include "env_sim.h"
#endif

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* k_work to update device's uptime */

struct k_work_delayable uptime_work;
static void device_status_uptime_update(struct k_work *work);

/* k_work to update environment data */

#if DT_NODE_EXISTS(DT_NODELABEL(mq2))
static const struct device *mq2_dev = DEVICE_DT_GET(DT_NODELABEL(mq2));
static struct k_work_delayable mq2_poll_work;

#define MQ2_WORK_STACK_SIZE  512
#define MQ2_WORK_PRIORITY  1

K_THREAD_STACK_DEFINE(mq2_stack, MQ2_WORK_STACK_SIZE);

static struct k_work_q mq2_work_queue;
static const struct k_work_queue_config mq2_work_queue_cfg = {
  .name = "mq2 work queue",
};

static void mq2_poll_update(struct k_work *work)
{
  int ret = sensor_sample_fetch(mq2_dev);
  if (ret != 0) {
    LOG_ERR("MQ-2 sample fetch failed: %d", ret);
    device_status_set_environment(0.0f, 0.0f, DEVICE_STATUS_ERROR);
  } else {
    struct sensor_value smoke;
    struct sensor_value voltage;

    if (sensor_channel_get(mq2_dev, (enum sensor_channel)SENSOR_CHAN_MQ_SMOKE, &smoke) == 0 &&
        sensor_channel_get(mq2_dev, (enum sensor_channel)SENSOR_CHAN_MQ_MV, &voltage) == 0) {
      // Update device_status to be read by screen
      float smoke_f = sensor_value_to_float(&smoke);
      float voltage_f = sensor_value_to_float(&voltage);
      LOG_DBG("smoke = %.2f ppm, voltage = %.3f", smoke_f, voltage_f);
      // Match error code according the level thresholds
      if (smoke_f < 150.0f) {
        device_status_set_environment(smoke_f, voltage_f, DEVICE_STATUS_OK);
      } else if (smoke_f < 300.0f) {
        device_status_set_environment(smoke_f, voltage_f, DEVICE_STATUS_WARN);
      } else {
        device_status_set_environment(smoke_f, voltage_f, DEVICE_STATUS_ERROR);
      }
    } else {
      LOG_ERR("MQ-2 channel_get failed");
      device_status_set_environment(0.0f, 0.0f, DEVICE_STATUS_ERROR);
    }
  }
  k_work_reschedule(&mq2_poll_work, K_SECONDS(1));
}
#endif

/* k_work to update tilt data */

#if DT_NODE_EXISTS(DT_NODELABEL(fxls))
static const struct device *fxls = DEVICE_DT_GET(DT_NODELABEL(fxls));
static struct k_work_delayable fxls_poll_work;

#define FXLS_WORK_STACK_SIZE  512
#define FXLS_WORK_PRIORITY  1

K_THREAD_STACK_DEFINE(fxls_stack, FXLS_WORK_STACK_SIZE);

static struct k_work_q fxls_work_queue;
static const struct k_work_queue_config fxls_work_queue_cfg = {
  .name = "fxls work queue",
};

static void fxls_poll_update(struct k_work *work)
{
  int ret = sensor_sample_fetch(fxls);
  if (ret != 0) {
    LOG_ERR("FXLS sample_fetch failed: %d", ret);
    device_status_set_tilt(0.0f, 0.0f, 0.0f, DEVICE_STATUS_ERROR);
  } else {
    struct sensor_value raw_x, raw_y, raw_z;

    if (sensor_channel_get(fxls, SENSOR_CHAN_ACCEL_X, &raw_x) == 0 &&
        sensor_channel_get(fxls, SENSOR_CHAN_ACCEL_Y, &raw_y) == 0 &&
        sensor_channel_get(fxls, SENSOR_CHAN_ACCEL_Z, &raw_z) == 0) {
      // Convert to g values instead of m/s2
      float x = sensor_ms2_to_mg(&raw_x) / 1000.0f;
      float y = sensor_ms2_to_mg(&raw_y) / 1000.0f;
      float z = sensor_ms2_to_mg(&raw_z) / 1000.0f;

      device_status_set_tilt(x, y, z, DEVICE_STATUS_OK);
      LOG_DBG("x = %.2f | y = %.2f | z = %.2f", x, y, z);
    } else {
      LOG_ERR("FXLS channel_get failed");
      device_status_set_tilt(0.0f, 0.0f, 0.0f, DEVICE_STATUS_ERROR);
    }
  }
  k_work_reschedule(&fxls_poll_work, K_MSEC(100));
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
    k_work_queue_init(&mq2_work_queue);
    k_work_queue_start(
      &mq2_work_queue, mq2_stack,
      K_THREAD_STACK_SIZEOF(mq2_stack), MQ2_WORK_PRIORITY,
      &mq2_work_queue_cfg
    );
    k_work_init_delayable(&mq2_poll_work, mq2_poll_update);
    k_work_reschedule_for_queue(&mq2_work_queue, &mq2_poll_work, K_SECONDS(1));
  }
#endif

#if DT_NODE_EXISTS(DT_NODELABEL(fxls))
  if (!device_is_ready(fxls)) {
    LOG_ERR("FXLS device not ready");
  } else {
    k_work_queue_init(&fxls_work_queue);
    k_work_queue_start(
      &fxls_work_queue, fxls_stack,
      K_THREAD_STACK_SIZEOF(fxls_stack), FXLS_WORK_PRIORITY,
      &fxls_work_queue_cfg
    );
    k_work_init_delayable(&fxls_poll_work, fxls_poll_update);
    k_work_reschedule_for_queue(&fxls_work_queue, &fxls_poll_work, K_MSEC(100));
  }
#endif

  // Logs its own failure and reports it as a CAN fault on screen
  int ret = can_service_start();
  if (ret != 0) {
    return ret;
  }
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

#if DT_NODE_HAS_STATUS_OKAY(DT_NODELABEL(flexcan0))
#include <zephyr/init.h>
#include <fsl_clock.h>

static int flexcan0_clock_init(void)
{
  CLOCK_SetClkDiv(kCLOCK_DivFlexcan0Clk, 3U);
  CLOCK_AttachClk(kPLL0_to_FLEXCAN0);
  return 0;
}
SYS_INIT(flexcan0_clock_init, PRE_KERNEL_1, 0);
#endif