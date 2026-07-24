#include "ui_manager.h"
#include "ui.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ui_manager, LOG_LEVEL_INF);

typedef struct {
  const char *name;
  lv_obj_t **scr;
  void (*init)(void);
  void (*destroy)(void);
} screen_t;

#define UI_SCREEN(name, scrObj) {name, &scrObj, scrObj##_screen_init, scrObj##_screen_destroy}

// Screen registration
static screen_t screens[] = {
  [SCREEN_SPLASH]       = UI_SCREEN("Splash", ui_scrSplash),
  [SCREEN_OVERVIEW]     = UI_SCREEN("Overview", ui_scrOverview),
  [SCREEN_TILT]         = UI_SCREEN("Tilt", ui_scrTilt),
  [SCREEN_ENVIRONMENT]  = UI_SCREEN("Environment", ui_scrEnvironment),
  [SCREEN_CAN]          = UI_SCREEN("CAN", ui_scrCan),
  [SCREEN_POWER]        = UI_SCREEN("Power", ui_scrPower),
  [SCREEN_COUNT]        = {NULL, NULL, NULL, NULL}
};

K_THREAD_STACK_DEFINE(ui_thread_stack, CONFIG_UI_THREAD_STACK_SIZE);
static struct k_thread ui_thread;
static k_tid_t ui_thread_id;

static screen_id_t s_current_screen = SCREEN_COUNT;
static screen_id_t s_pending_screen = SCREEN_SPLASH;

static void lvgl_thread(void *arg1, void *arg2, void *arg3);
static void ui_go_to_screen(screen_id_t screen);

void ui_manager_init(void)
{
  const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  if (!device_is_ready(display)) {
    LOG_ERR("Display device not ready");
    return;
  }
  int ret = display_blanking_off(display);
  LOG_INF("blanking_off: %d", ret);

  lv_disp_t *dispp = lv_display_get_default();
  lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
  lv_disp_set_theme(dispp, theme);

  ui_thread_id = k_thread_create(
    &ui_thread, ui_thread_stack, K_THREAD_STACK_SIZEOF(ui_thread_stack),
    lvgl_thread, NULL, NULL, NULL, CONFIG_UI_THREAD_PRIORITY, 0, K_NO_WAIT
  );
}

/**
 * @brief Dedicated LVGL thread. Owns every screen and widget
 */
static void lvgl_thread(void *arg1, void *arg2, void *arg3)
{
  while(1) {
    // Check if there is a pending screen to change to
    if(s_current_screen != s_pending_screen) {
      screen_t curr = screens[s_current_screen];
      screen_t next = screens[s_pending_screen];
      LOG_INF("Changing screen to %s", next.name);
      if(next.init) {
        _ui_screen_change(next.scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, next.init);
      } if(curr.destroy) {
        _ui_screen_delete(curr.destroy);
      }
      s_current_screen = s_pending_screen;
      s_pending_screen = (s_pending_screen + 1) % SCREEN_COUNT; 
    }
    lv_timer_handler();
    k_msleep(500);
  }
}

static void ui_go_to_screen(screen_id_t next)
{
  s_pending_screen = next;
}