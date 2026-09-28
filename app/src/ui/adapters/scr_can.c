#include "device_status.h"
#include "ui.h"
#include "ui_adapters.h"

static void scrCan_hero_helper(bool loopback_ok, device_status_t status);

// Last values drawn on the widgets. File scope so postinit can force a full
// redraw after the screen is recreated (see scr_overview.c).
static struct device_status_can s_drawn;

static void scrCan_draw(bool force)
{
  struct device_status_can can;
  device_status_get_can(&can);

  if(force || s_drawn.status != can.status || s_drawn.loopback_ok != can.loopback_ok) {
    scrCan_hero_helper(can.loopback_ok, can.status);
  }

  if(force || s_drawn.frame_id != can.frame_id) {
    lv_label_set_text_fmt(ui_canFrameV, "0x%04X", can.frame_id);
  }

  if(force || s_drawn.tx_interval_ms != can.tx_interval_ms) {
    lv_label_set_text_fmt(ui_canTxIntervalV, "%u ms", can.tx_interval_ms);
  }

  if(force || s_drawn.tx_count != can.tx_count) {
    // Loopback mode hands every TX frame straight back as RX, so TX == RX is
    // exactly what the loopback self-test is proving right now. Swap the
    // second value for a real can_rx_count once an RX filter callback exists
    // for a physical bus (see ROADMAP.md's CAN section).
    lv_label_set_text_fmt(ui_canCountV, "%u / %u", can.tx_count, can.tx_count);
  }

  s_drawn = can;
}

static void scrCan_postinit(void)
{
  scrCan_draw(true);
}

static void scrCan_step(void)
{
  scrCan_draw(false);
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

static void scrCan_load(void)
{
  _ui_screen_change(&ui_scrCan, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_scrCan_screen_init);
}

static void scrCan_unload(void)
{
  _ui_screen_delete(ui_scrCan_screen_destroy);
}

const screen_ops_t scrCan_ops = { "CAN", scrCan_load, scrCan_unload, scrCan_postinit, scrCan_step };
