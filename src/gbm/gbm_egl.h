#ifndef __LVGL_EGL_H_
#define __LVGL_EGL_H_

#include <stdint.h>

void *egl_init_ctx(int fd, int width, int height, int format, int angle);
void egl_free_ctx(void *data);
void *egl_convert_fb(void *data, int dma_fd, int width, int height,
                     int stride, int format);

#endif
