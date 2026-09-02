#include "device_status.h"
#include "ui.h"
#include "ui_adapters.h"

static void scrCan_hero_helper(bool loopback_ok, device_status_t status);

void scrCan_step(void)
{
  static uint32_t frame_id = 0;
  static uint32_t tx_interval_ms = 0;
  static uint32_t tx_count = 0;
  static bool loopback_ok;
  static device_status_t can_status;

  struct device_status status;
  device_status_get(&status);

  if(can_status != status.can_status || loopback_ok != status.can_loopback_ok) {
    can_status = status.can_status;
    loopback_ok = status.can_loopback_ok;
    scrCan_hero_helper(loopback_ok, can_status);
  }

  if(frame_id != status.can_frame_id) {
    frame_id = status.can_frame_id;
    lv_label_set_text_fmt(ui_canFrameV, "0x%04X", frame_id);
  }

  if(tx_interval_ms != status.can_tx_interval_ms) {
    tx_interval_ms = status.can_tx_interval_ms;
    lv_label_set_text_fmt(ui_canTxIntervalV, "%u ms", tx_interval_ms);
  }

  if(tx_count != status.can_tx_count) {
    tx_count = status.can_tx_count;
    // Loopback mode hands every TX frame straight back as RX, so TX == RX is
    // exactly what the loopback self-test is proving right now. Swap the
    // second value for a real can_rx_count once an RX filter callback exists
    // for a physical bus (see ROADMAP.md's CAN section).
    lv_label_set_text_fmt(ui_canCountV, "%u / %u", tx_count, tx_count);
  }
}

static void scrCan_hero_helper(bool loopback_ok, device_status_t status)
{
  if(status == DEVICE_STATUS_OK) {
    lv_label_set_text(ui_canHero, loopback_ok ? "Loopback OK" : "Loopback mismatch");
    ui_object_set_themeable_style_property(ui_canHero, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR,
                                            loopback_ok ? _ui_theme_color_okg : _ui_theme_color_wrn);
  } else if(status == DEVICE_STATUS_WARN) {
    lv_label_set_text(ui_canHero, "CAN degraded");
    ui_object_set_themeable_style_property(ui_canHero, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR, _ui_theme_color_wrn);
  } else if(status == DEVICE_STATUS_ERROR) {
    lv_label_set_text(ui_canHero, "CAN fault");
    ui_object_set_themeable_style_property(ui_canHero, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR, _ui_theme_color_err);
  } else {
    lv_label_set_text(ui_canHero, "No CAN data");
    ui_object_set_themeable_style_property(ui_canHero, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_TEXT_COLOR, _ui_theme_color_tx2);
  }
}
