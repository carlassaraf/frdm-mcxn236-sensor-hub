#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/display.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

int main(void) {

  const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  if (!device_is_ready(display)) {
    LOG_ERR("Display device not ready");
    return -1;
  }
  int ret = display_blanking_off(display);
  LOG_INF("blanking_off: %d", ret);

  while(1);
  return 0;
}