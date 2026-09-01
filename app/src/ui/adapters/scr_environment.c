#include "device_status.h"
#include "ui.h"
#include "ui_adapters.h"

static void scrEnvironment_status_helper(device_status_t status, lv_obj_t *lbl, const char *ok_text, const char *wrn_text, const char *err_text, const char *unkn_text);

void scrEnvironment_postInit(void)
{
  struct device_status dev;
  device_status_get(&dev);

  lv_label_set_text(ui_envSensorV, dev.env_sensor_name);
  lv_label_set_text_fmt(ui_envChannelV, "ADC0_CH%d", dev.env_channel);
}

void scrEnvironment_step(void)
{
  struct device_status dev;
  device_status_get(&dev);

  lv_label_set_text_fmt(ui_envHeroValue, "%3d", (uint16_t)dev.env_value);
  scrEnvironment_status_helper((dev.env_value > 150)? DEVICE_STATUS_ERROR : DEVICE_STATUS_OK, ui_envHerounit, "ppm - good", "", "ppm - dangerous", "");
  lv_label_set_text_fmt(ui_envVoltageV, "%01d.%03d V", (uint8_t)(dev.env_voltage), ((uint32_t)(dev.env_voltage * 1000) % 1000));
  scrEnvironment_status_helper(dev.env_status, ui_envStatusV, "Good", "Degraded", "Faulty", "Unknown");
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