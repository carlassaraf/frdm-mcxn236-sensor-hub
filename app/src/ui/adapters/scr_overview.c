#include <zephyr/app_version.h>
#include "device_status.h"
#include "ui.h"
#include "ui_adapters.h"

static void scrOverview_status_helper(device_status_t status, lv_obj_t *lbl, const char *ok_text, const char *wrn_text, const char *err_text, const char *unkn_text);

// Last values drawn on the widgets. File scope so postinit can force a full
// redraw after the screen is recreated -- the widgets start with SquareLine's
// placeholder text, so whatever was cached from the previous visit is stale.
static struct device_status s_drawn;

static void scrOverview_draw(bool force)
{
  struct device_status status;
  device_status_get(&status);

  if(force || s_drawn.device.uptime_s != status.device.uptime_s) {
    uint32_t uptime_s = status.device.uptime_s;
    lv_label_set_text_fmt(ui_overviewUptime, "%02d:%02d:%02d", uptime_s / 3600, (uptime_s / 60) % 60, uptime_s % 60);
  }

  // Update status indicators
  if(force || s_drawn.environment.status != status.environment.status) {
    scrOverview_status_helper(status.environment.status, ui_overviewMqStatus, "available", "degraded", "faulty", "unknown");
  }
  if(force || s_drawn.can.status != status.can.status) {
    scrOverview_status_helper(status.can.status, ui_overviewCanStatus, "available", "degraded", "faulty", "unknown");
  }
  if(force || s_drawn.tilt.status != status.tilt.status) {
    scrOverview_status_helper(status.tilt.status, ui_overviewTiltStatus, "available", "degraded", "faulty", "unknown");
  }
  if(force || s_drawn.device.overall_status != status.device.overall_status) {
    scrOverview_status_helper(status.device.overall_status, ui_overviewStatus, "NORMAL", "DEGRADED", "FAULT", "STARTING");
  }

  s_drawn = status;
}

static void scrOverview_postinit(void)
{
  lv_label_set_text_fmt(ui_overviewVersion, "FRDM-MCXN236 - v%s (%s)", APP_VERSION_STRING, STRINGIFY(APP_BUILD_VERSION));
  scrOverview_draw(true);
}

static void scrOverview_step(void)
{
  scrOverview_draw(false);
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