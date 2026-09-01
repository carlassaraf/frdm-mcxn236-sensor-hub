#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/app_version.h>

#include "device_status.h"
#include "ui_manager.h"
#if defined(CONFIG_TILT_SIM)
#include "tilt_sim.h"
#endif

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

struct k_work_delayable uptime_work;
static void device_status_uptime_update(struct k_work *work);

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