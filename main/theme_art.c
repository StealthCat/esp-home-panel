#include "theme_art.h"

/*
 * main/CMakeLists.txt generates this include for every build path. Keep this
 * include unconditional: if sprite generation ever breaks, the build must fail
 * rather than producing firmware that quietly omits all theme artwork.
 */
#if !LV_USE_DRAW_SW
#error "Theme artwork requires the LVGL software renderer (LV_USE_DRAW_SW)."
#endif

#if !LV_DRAW_SW_SUPPORT_ARGB8888
#error "Theme artwork uses ARGB8888 sprites; enable CONFIG_LV_DRAW_SW_SUPPORT_ARGB8888."
#endif

#include "theme_art_generated.inc"
