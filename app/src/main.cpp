#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/app_version.h>

#include "ui.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void) {

  const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  if (!device_is_ready(display)) {
    LOG_ERR("Display device not ready");
    return -1;
  }
  int ret = display_blanking_off(display);
  LOG_INF("blanking_off: %d", ret);

  const struct gpio_dt_spec btn = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
  if(!gpio_is_ready_dt(&btn)) {
    LOG_ERR("SW2 not ready");
  }
  if(gpio_pin_configure_dt(&btn, GPIO_INPUT)) {
    LOG_ERR("SW2 not configured");
  }

  ui_init();

  lv_obj_t *screens[] = {ui_scrSplash, ui_scrOverview, ui_scrTilt, ui_scrEnvironment, ui_scrCan};
  uint8_t i = 0;

  while (1) {
    static bool pressed = false;
    if(gpio_pin_get_dt(&btn) && !pressed) {
      i = (i + 1) % (sizeof(screens) / sizeof(screens[0]));
      _ui_screen_change(&(screens[i]), LV_SCR_LOAD_ANIM_NONE, 0, 0, NULL);
      if(lv_screen_active() == ui_scrSplash) {
        lv_label_set_text_fmt(ui_splashVersion, "FRDM-MCXN236 - v%s (%s)", APP_VERSION_STRING, STRINGIFY(APP_BUILD_VERSION));
      } else if(lv_screen_active() == ui_scrOverview) {
        lv_label_set_text_fmt(ui_overviewVersion, "FRDM-MCXN236 - v%s (%s)", APP_VERSION_STRING, STRINGIFY(APP_BUILD_VERSION));
      }
      pressed = true;
    } else if(!gpio_pin_get_dt(&btn) && pressed) {
      pressed = false;
    }
    lv_timer_handler();
    k_msleep(100);
  }
  return 0;
}