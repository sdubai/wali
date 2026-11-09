#pragma once
#include <lvgl.h>

namespace walle {
// Only for synchronous I2C OLED transfers. The draw callback has already
// returned before LVGL invokes its wait callback. An absent completion then
// means the frame failed, not that DMA is still reading the framebuffer.
inline void CompleteSynchronousOledFlush(lv_display_t* display) { lv_display_flush_ready(display); }
} // namespace walle
