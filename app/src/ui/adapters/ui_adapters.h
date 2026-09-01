#ifndef UI_ADAPTERS_H
#define UI_ADAPTERS_H

#ifdef __cplusplus
extern "C" {
#endif

// Splash screen adapter (scr_splash.c)
void scrSplash_postinit(void);

// Overview screen adapter (scr_overview.c)
void scrOverview_postinit(void);
void scrOverview_step(void);

// Tilt screen adapter (scr_tilt.c)
void scrTilt_step(void);

// Environment screen adapter (scr_environment.c)
void scrEnvironment_postInit(void);
void scrEnvironment_step(void);

#ifdef __cplusplus
}
#endif

#endif
