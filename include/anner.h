#include <libdrm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

void anner_create_window(int window_width, int window_height);
int anner_create_texture(unsigned char* pixels, int w, int h, int format);
int anner_delete_texture(void);
void anner_destory_window(void);
void anner_render(int w, int h);
int anner_dumpPixels(int len, int inWindowWidth, int inWindowHeight, unsigned char * pPixelDataFront, char* file_name);

//Off-screen rendering dummy function
int anner_create_intput(void** pixels, int *drmbuf_fd, int w, int h, int format, int stride);
int anner_create_output(void** pixels, int *drmbuf_fd, int w, int h, int format, int stride);
void anner_activation_texture(void* pixels, int drmbuf_fd, int w, int h, int format, int stride);
int anner_disable_texture();
int anner_delete_buf(void* pixels, int drm_fd, int len, int type);
void anner_set_effects(int Angle);