#include <zephyr/app_version.h>
#include "ui.h"
#include "ui_adapters.h"

void scrSplash_postinit(void)
{
  lv_label_set_text_fmt(ui_splashVersion, "FRDM-MCXN236 - v%s (%s)", APP_VERSION_STRING, STRINGIFY(APP_BUILD_VERSION));
}