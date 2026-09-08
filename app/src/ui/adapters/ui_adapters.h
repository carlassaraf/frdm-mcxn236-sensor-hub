#ifndef UI_ADAPTERS_H
#define UI_ADAPTERS_H

#ifdef __cplusplus
extern "C" {
#endif

// One `screen_ops_t` per screen, defined as a const struct by that screen's
// adapter (scr_<name>.c). Opaque function pointers only -- no lv_obj_t or other
// LVGL/SquareLine type crosses this boundary, so ui_manager.c can drive the
// screen table without ever including a generated header itself. Per
// ARCHITECTURE.md, adapters are the only code allowed to reach into those.
typedef struct {
  const char *name;
  void (*load)(void);
  void (*unload)(void);
  void (*postinit)(void);
  void (*step)(void);
} screen_ops_t;

// Splash screen adapter (scr_splash.c)
extern const screen_ops_t scrSplash_ops;

// Overview screen adapter (scr_overview.c)
extern const screen_ops_t scrOverview_ops;

// Tilt screen adapter (scr_tilt.c)
extern const screen_ops_t scrTilt_ops;

// Environment screen adapter (scr_environment.c)
extern const screen_ops_t scrEnvironment_ops;

// Can screen adapter (scr_can.c)
extern const screen_ops_t scrCan_ops;

// Power screen adapter (scr_power.c)
extern const screen_ops_t scrPower_ops;

#ifdef __cplusplus
}
#endif

#endif
