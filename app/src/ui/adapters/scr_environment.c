#include "device_status.h"
#include "ui.h"
#include "ui_adapters.h"

static void scrEnvironment_status_helper(device_status_t status, lv_obj_t *lbl, const char *ok_text, const char *wrn_text, const char *err_text, const char *unkn_text);

// Last values drawn on the widgets. File scope so postInit can force a full
// redraw after the screen is recreated (see scr_overview.c).
static struct device_status_environment s_drawn;

static void scrEnvironment_draw(bool force)
{
  struct device_status_environment env;
  device_status_get_environment(&env);

  if(force) {
    lv_label_set_text(ui_envSensorV, env.sensor_name);
    lv_label_set_text_fmt(ui_envChannelV, "ADC0_CH%d", env.channel);
  }
  if(force || s_drawn.value != env.value) {
    lv_label_set_text_fmt(ui_envHeroValue, "%.1f", (double)env.value);
  }
  if(force || s_drawn.voltage != env.voltage) {
    lv_label_set_text_fmt(ui_envVoltageV, "%.3f V", (double)env.voltage);
  }
  if(force || s_drawn.status != env.status) {
    scrEnvironment_status_helper(env.status, ui_envStatusV, "Good", "Degraded", "Faulty", "Unknown");
    scrEnvironment_status_helper(env.status, ui_envHerounit, "ppm - good", "ppm - elevated", "ppm - dangerous", "ppm");
  }

  s_drawn = env;
}

static void scrEnvironment_postInit(void)
{
  scrEnvironment_draw(true);
}

static void scrEnvironment_step(void)
{
  scrEnvironment_draw(false);
}

static void scrEnvironment_status_helper(device_status_t status, lv_obj_t *lbl, const char *ok_text, const char *wrn_text, const char *err_text, const char *unkn_text)
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

static void scrEnvironment_load(void)
{
  _ui_screen_change(&ui_scrEnvironment, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_scrEnvironment_screen_init);
}

static void scrEnvironment_unload(void)
{
  _ui_screen_delete(ui_scrEnvironment_screen_destroy);
}

const screen_ops_t scrEnvironment_ops = { "Environment", scrEnvironment_load, scrEnvironment_unload, scrEnvironment_postInit, scrEnvironment_step };