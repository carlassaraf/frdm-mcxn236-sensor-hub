#include "ui_manager.h"
#include "ui_adapters.h"

#include <lvgl.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(ui_manager, LOG_LEVEL_INF);

// Never redraw sooner than the floor after the last
// one, never wait past the ceiling even if nothing changed
#define UI_WAIT_FLOOR_MS   0
#define UI_WAIT_CEILING_MS 10

// Private prototypes
static void lvgl_thread(void *arg1, void *arg2, void *arg3);
static void ui_go_to_screen(screen_id_t screen);
static void ui_manager_input_cb(struct input_event *evt, void *user_data);

// Stand-in for screens[SCREEN_COUNT]: read once on the very first loop iteration,
// before any real screen has loaded, so `curr` is always a valid pointer to
// dereference rather than NULL.
static const screen_ops_t s_no_screen = {0};

// Screen registration -- one entry per screen_id_t, each backed by that screen's
// adapter-owned const screen_ops_t (see ui_adapters.h). No generated SquareLine
// header is included here; only opaque function pointers cross this boundary.
static const screen_ops_t *screens[] = {
  [SCREEN_SPLASH]       = &scrSplash_ops,
  [SCREEN_OVERVIEW]     = &scrOverview_ops,
  [SCREEN_TILT]         = &scrTilt_ops,
  [SCREEN_ENVIRONMENT]  = &scrEnvironment_ops,
  [SCREEN_CAN]          = &scrCan_ops,
  [SCREEN_POWER]        = &scrPower_ops,
  [SCREEN_COUNT]        = &s_no_screen,
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

// Register callback for input switches
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
  int64_t last_redraw_ms = k_uptime_get();

  while (1) {
    // Get current screen
    const screen_ops_t *curr = screens[s_current_screen];
    // Get the current sleep state
    struct device_status dev;
    device_status_get(&dev);
    // Sync the sleep overlay with the latest SW2 request
    if (dev.display_sleeping != overlay_visible) {
      if (dev.display_sleeping) {
        lv_obj_clear_flag(s_sleep_overlay, LV_OBJ_FLAG_HIDDEN);
      } else {
        lv_obj_add_flag(s_sleep_overlay, LV_OBJ_FLAG_HIDDEN);
      }
      overlay_visible = dev.display_sleeping;
    }

    // Check if there is a pending screen to change to
    if (s_current_screen != s_pending_screen) {
      const screen_ops_t *next = screens[s_pending_screen];
      LOG_INF("Changing screen to %s", next->name);
      // Change to pending screen and create widgets on spot
      if (next->load) {
        next->load();
        // After screen creation and transition, run any available port creation
        if (next->postinit) {
          LOG_INF("Calling post-init function for %s", next->name);
          next->postinit();
        }
      } if (curr->unload) {
        // Destroy previous screen to free memory
        curr->unload();
      }
      // Update screen tracking
      s_current_screen = s_pending_screen;
      // Lets other modules see what's on screen without reaching into the UI Manager.
      device_status_set_active_screen(s_current_screen);
      curr = screens[s_current_screen];
    }
    // Run any available step callback
    if(curr->step) {
      curr->step();
    }

    lv_timer_handler();

    // Enforce the floor unconditionally so a burst of writes
    // can't shrink the gap between redraws below it
    int64_t since_last_ms = k_uptime_get() - last_redraw_ms;
    if (since_last_ms < UI_WAIT_FLOOR_MS) {
      k_msleep(UI_WAIT_FLOOR_MS - since_last_ms);
    }
    last_redraw_ms = k_uptime_get();
    // Block until a producer changes device_status, or the ceiling elapses --
    // whichever comes first. Either way the loop re-checks s_pending_screen and
    // the sleep flag next iteration, so this never delays a screen switch or a
    // sleep toggle by more than UI_WAIT_CEILING_MS.
    device_status_wait(DEVICE_STATUS_EVT_ALL, K_MSEC(UI_WAIT_CEILING_MS));
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
    struct device_status dev;
    device_status_get(&dev);
    bool sleeping = !dev.display_sleeping;
    device_status_set_display_sleeping(sleeping);
    LOG_INF("Display %s", sleeping ? "sleeping" : "awake");
    break;
  default:
    break;
  }
}
