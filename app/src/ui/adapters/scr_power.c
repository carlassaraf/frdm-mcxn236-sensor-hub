#include "ui.h"
#include "ui_adapters.h"

static void scrPower_load(void)
{
  _ui_screen_change(&ui_scrPower, LV_SCR_LOAD_ANIM_NONE, 0, 0, ui_scrPower_screen_init);
}

static void scrPower_unload(void)
{
  _ui_screen_delete(ui_scrPower_screen_destroy);
}

// No postinit/step: the sleep overlay (lv_layer_top(), above every screen) always
// covers this screen's hero/instructions text in the one state ("SLEEPING") they'd
// need to change to, so SquareLine's static "AWAKE" / "Push SW2 to sleep" text is
// already correct whenever this screen is actually visible. See ROADMAP.md's
// ui_adapter_power note.
const screen_ops_t scrPower_ops = { "Power", scrPower_load, scrPower_unload, NULL, NULL };
