#include <zephyr/app_version.h>
#include "device_status.h"
#include "ui.h"
#include "ui_adapters.h"

void scrOverview_postinit(void)
{
  lv_label_set_text_fmt(ui_overviewVersion, "FRDM-MCXN236 - v%s (%s)", APP_VERSION_STRING, STRINGIFY(APP_BUILD_VERSION));
}

void scrOverview_step(void)
{
  static uint32_t uptime_s = 0;

  struct device_status status;
  device_status_get(&status);
  
  if(uptime_s != status.uptime_s) {
    // Avoid updating label if uptime hasn't changed
    uptime_s = status.uptime_s;
    lv_label_set_text_fmt(ui_overviewUptime, "%02d:%02d:%02d", uptime_s / 3600, (uptime_s / 60) % 60, uptime_s % 60);
  }
}