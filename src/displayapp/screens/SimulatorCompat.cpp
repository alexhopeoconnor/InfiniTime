#include <lvgl/lvgl.h>

// InfiniSim compiles every upstream screen, including the weather screens that
// ElixirTime deliberately excludes from its firmware target.  Give those
// unreachable simulator-only objects a harmless font definition without
// restoring the weather font to the watch build.
#if defined(MONITOR_ZOOM)
lv_font_t fontawesome_weathericons {};
#endif
