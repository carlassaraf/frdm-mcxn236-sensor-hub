#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/app_version.h>

// #include "ui.h"

#include "ui_manager.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void) {

  const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
  if(!gpio_is_ready_dt(&btn)) {
    LOG_ERR("SW2 not ready");
  }
  if(gpio_pin_configure_dt(&btn, GPIO_INPUT)) {
    LOG_ERR("SW2 not configured");
  }

  ui_manager_init();

  // while (1) {
  //   k_msleep(100);
  // }
  return 0;
}