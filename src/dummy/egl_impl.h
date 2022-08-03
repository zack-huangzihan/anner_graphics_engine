#ifndef __EGL_IMPL_H__
#define __EGL_IMPL_H__

void *ectx_create_window(int w, int h);
void ectx_destory_window(void *_impl);
int ectx_import_output(void *_impl, int drmbuf_fd,
                       int w, int h, int stride, int format);
int ectx_activation_texture(void *_impl, int drmbuf_fd,
                            int w, int h, int stride, int format);
void ectx_render(void *_impl, int w, int h, int angle);

#endif

