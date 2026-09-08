#include <zephyr/app_version.h>
#include "ui.h"
#include "ui_adapters.h"

static void scrSplash_postinit(void)
{
  lv_label_set_text_fmt(ui_splashVersion, "FRDM-MCXN236 - v%s (%s)", APP_VERSION_STRING, STRINGIFY(APP_BUILD_VERSION));
}

static void scrSplash_load(void)
{
  _ui_screen_change(&ui_scrSplash, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_scrSplash_screen_init);
}

static void scrSplash_unload(void)
{
  _ui_screen_delete(ui_scrSplash_screen_destroy);
}

const screen_ops_t scrSplash_ops = { "Splash", scrSplash_load, scrSplash_unload, scrSplash_postinit, NULL };