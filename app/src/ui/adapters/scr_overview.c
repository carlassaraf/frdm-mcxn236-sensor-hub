#include <zephyr/app_version.h>
#include "device_status.h"
#include "ui.h"
#include "ui_adapters.h"

static void scrOverview_status_helper(device_status_t status, lv_obj_t *lbl, const char *ok_text, const char *wrn_text, const char *err_text, const char *unkn_text);

static void scrOverview_postinit(void)
{
  lv_label_set_text_fmt(ui_overviewVersion, "FRDM-MCXN236 - v%s (%s)", APP_VERSION_STRING, STRINGIFY(APP_BUILD_VERSION));
}

static void scrOverview_step(void)
{
  static uint32_t uptime_s = 0;
  static device_status_t env_status;
  static device_status_t can_status;
  static device_status_t tilt_status;
  static device_status_t overall_status;

  struct device_status status;
  device_status_get(&status);
  
  if(uptime_s != status.uptime_s) {
    // Avoid updating label if uptime hasn't changed
    uptime_s = status.uptime_s;
    lv_label_set_text_fmt(ui_overviewUptime, "%02d:%02d:%02d", uptime_s / 3600, (uptime_s / 60) % 60, uptime_s % 60);
  }

  // Update status indicators
  if(env_status != status.env_status) {
    env_status = status.env_status;
    scrOverview_status_helper(status.env_status, ui_overviewMqStatus, "available", "faulty", NULL, NULL);
  }
  if(can_status != status.can_status) {
    can_status = status.can_status;
    scrOverview_status_helper(status.can_status, ui_overviewCanStatus, "available", "faulty", NULL, NULL);
  }
  if(tilt_status != status.tilt_status) {
    tilt_status = status.tilt_status;
    scrOverview_status_helper(status.tilt_status, ui_overviewTiltStatus, "available", "faulty", NULL, NULL);
  }
  if(overall_status != status.overall_status) {
    overall_status = status.overall_status;
    scrOverview_status_helper(status.overall_status, ui_overviewStatus, "NORMAL", "FAULT", "DEGRADED", NULL);
  }
}

static void scrOverview_status_helper(device_status_t status, lv_obj_t *lbl, const char *ok_text, const char *wrn_text, const char *err_text, const char *unkn_text)
{
  if(ok_text && status == DEVICE_STATUS_OK) {
    lv_label_set_text(lbl, ok_text);
    ui_object_set_themeable_style_property(lbl, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR, _ui_theme_color_okg);
  } else if(wrn_text && status == DEVICE_STATUS_WARN) {
    lv_label_set_text(lbl, wrn_text);
    ui_object_set_themeable_style_property(lbl, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR, _ui_theme_color_wrn);
  } else if(err_text && status == DEVICE_STATUS_ERROR) {
    lv_label_set_text(lbl, err_text);
    ui_object_set_themeable_style_property(lbl, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR, _ui_theme_color_err);
  } else if(unkn_text && status == DEVICE_STATUS_UNKNOWN) {
    lv_label_set_text(lbl, unkn_text);
    ui_object_set_themeable_style_property(lbl, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR, _ui_theme_color_tx2);
  }
}

static void scrOverview_load(void)
{
  _ui_screen_change(&ui_scrOverview, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_scrOverview_screen_init);
}

static void scrOverview_unload(void)
{
  _ui_screen_delete(ui_scrOverview_screen_destroy);
}

const screen_ops_t scrOverview_ops = { "Overview", scrOverview_load, scrOverview_unload, scrOverview_postinit, scrOverview_step };