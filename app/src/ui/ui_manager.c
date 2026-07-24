#include "ui_manager.h"
#include "ui.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ui_manager, LOG_LEVEL_INF);

// Screen struct definition
typedef struct {
  const char *name;
  lv_obj_t **scr;
  void (*init)(void);
  void (*destroy)(void);
  void (*postinit)(void);
  void (*step)(void);
} screen_t;

#define UI_SCREEN(name, scrObj, post, step) {name, &scrObj, scrObj##_screen_init, scrObj##_screen_destroy}

// Screen registration
static screen_t screens[] = {
  [SCREEN_SPLASH]       = UI_SCREEN("Splash", ui_scrSplash, NULL, NULL),
  [SCREEN_OVERVIEW]     = UI_SCREEN("Overview", ui_scrOverview, NULL, NULL),
  [SCREEN_TILT]         = UI_SCREEN("Tilt", ui_scrTilt, NULL, NULL),
  [SCREEN_ENVIRONMENT]  = UI_SCREEN("Environment", ui_scrEnvironment, NULL, NULL),
  [SCREEN_CAN]          = UI_SCREEN("CAN", ui_scrCan, NULL, NULL),
  [SCREEN_POWER]        = UI_SCREEN("Power", ui_scrPower, NULL, NULL),
  [SCREEN_COUNT]        = {NULL, NULL, NULL, NULL, NULL, NULL}
};

// Thread specific variables
K_THREAD_STACK_DEFINE(ui_thread_stack, CONFIG_UI_THREAD_STACK_SIZE);
static struct k_thread ui_thread;
static k_tid_t ui_thread_id;

// Screen tracking
static screen_id_t s_current_screen = SCREEN_COUNT;
static screen_id_t s_pending_screen = SCREEN_SPLASH;

// Display sleep tracking (SW2)
static const struct device *s_display;
static lv_obj_t *s_sleep_overlay;
static bool s_display_sleeping = false;

// Private prototypes
static void lvgl_thread(void *arg1, void *arg2, void *arg3);
static void ui_go_to_screen(screen_id_t screen);
static void ui_manager_input_cb(struct input_event *evt, void *user_data);

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_PATH(gpio_keys)), ui_manager_input_cb, NULL);

void ui_manager_init(void)
{
  s_display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  if (!device_is_ready(s_display)) {
    LOG_ERR("Display device not ready");
    return;
  }
  int ret = display_blanking_off(s_display);
  LOG_INF("blanking_off: %d", ret);

  lv_disp_t *dispp = lv_display_get_default();
  lv_theme_t *theme = lv_theme_default_init(dispp, lv_palette_main(LV_PALETTE_BLUE), lv_palette_main(LV_PALETTE_RED), true, LV_FONT_DEFAULT);
  lv_disp_set_theme(dispp, theme);

  // Full-screen black overlay used for "sleep": the ILI9341 has no backlight
  // control, and its DISPOFF command just leaves the panel undriven (white),
  // so software-blanking the content is the only way to get a dark screen.
  s_sleep_overlay = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_sleep_overlay);
  lv_obj_set_size(s_sleep_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_sleep_overlay, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(s_sleep_overlay, LV_OPA_COVER, 0);
  lv_obj_add_flag(s_sleep_overlay, LV_OBJ_FLAG_HIDDEN);

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
  bool overlay_visible = false;

  while (1) {
    // Sync the sleep overlay with the latest SW2 request
    if (s_display_sleeping != overlay_visible) {
      if (s_display_sleeping) {
        lv_obj_clear_flag(s_sleep_overlay, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(s_sleep_overlay, LV_OBJ_FLAG_HIDDEN);
      }
      overlay_visible = s_display_sleeping;
    }

    // Check if there is a pending screen to change to
    if (s_current_screen != s_pending_screen) {
      screen_t curr = screens[s_current_screen];
      screen_t next = screens[s_pending_screen];
      LOG_INF("Changing screen to %s", next.name);
      // Change to pending screen and create widgets on spot
      if (next.init) {
        _ui_screen_change(next.scr, LV_SCR_LOAD_ANIM_NONE, 0, 0, next.init);
        // After screen creation and transition, run any available port creation
        if (next.postinit) {
          next.postinit();
        }
      } if (curr.destroy) {
        // Destroy previous screen to free memory
        _ui_screen_delete(curr.destroy);
      }
      // Update screen tracking
      s_current_screen = s_pending_screen;
    }
    // Run any available 

    lv_timer_handler();
    k_msleep(10);
  }
}

static void ui_go_to_screen(screen_id_t next)
{
  s_pending_screen = next;
}

/**
 * @brief SW2/SW3 gpio-keys handler. SW3 (INPUT_KEY_0) cycles through the
 * dashboard screens, SW2 (INPUT_KEY_WAKEUP) toggles display sleep.
 */
static void ui_manager_input_cb(struct input_event *evt, void *user_data)
{
  ARG_UNUSED(user_data);

  // Only react to key-press, ignore release
  if (evt->type != INPUT_EV_KEY || evt->value != 1) {
    return;
  }

  // Special case, any key press dismisses the splash screen
  if(s_current_screen == SCREEN_SPLASH) {
    ui_go_to_screen(SCREEN_OVERVIEW);
    return;
  }

  switch (evt->code) {
  case INPUT_KEY_0: {
    // Cycle OVERVIEW..POWER, skipping SPLASH
    screen_id_t next = (s_pending_screen % (SCREEN_COUNT - 1)) + 1;
    ui_go_to_screen(next);
    break;
  }
  case INPUT_KEY_WAKEUP:
    s_display_sleeping = !s_display_sleeping;
    LOG_INF("Display %s", s_display_sleeping ? "sleeping" : "awake");
    break;
  default:
    break;
  }
}