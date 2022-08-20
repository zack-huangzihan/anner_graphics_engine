/*
 *  Copyright (c) 2021, Jeffy Chen <jeffy.chen@rock-chips.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#include <errno.h>
#include <malloc.h>
#include <unistd.h>

#include <linux/dma-buf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <poll.h>
#include <drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libdrm/drm_fourcc.h>

#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "gbm_egl.h"

#define DRM_ERROR printf

static const GLfloat texcoords[] = {
  0.0f,  1.0f,
  1.0f,  1.0f,
  0.0f,  0.0f,
  1.0f,  0.0f,
};

static const GLfloat texcoords90[] = {
  0.0f,  0.0f,
  0.0f,  1.0f,
  1.0f,  0.0f,
  1.0f,  1.0f,
};

static const GLfloat texcoords180[] = {
  1.0f,  0.0f,
  0.0f,  0.0f,
  1.0f,  1.0f,
  0.0f,  1.0f,
};

static const GLfloat texcoords270[] = {
  1.0f,  1.0f,
  1.0f,  0.0f,
  0.0f,  1.0f,
  0.0f,  0.0f,
};

static const char vertex_shader_source[] =
"attribute vec4 position;\n"
"attribute vec2 texcoord;\n"
"varying vec2 v_texcoord;\n"
"void main()\n"
"{\n"
"   gl_Position = position;\n"
"   v_texcoord = texcoord;\n"
"}\n";

static const char fragment_shader_source[] =
"#extension GL_OES_EGL_image_external : require\n"
"precision mediump float;\n"
"varying vec2 v_texcoord;\n"
"uniform samplerExternalOES tex;\n"
"void main()\n"
"{\n"
"    gl_FragColor = texture2D(tex, v_texcoord);\n"
"}\n";

typedef struct {
  int fd;

  struct gbm_device *gbm_dev;
  struct gbm_surface *gbm_surfaces;

  EGLDisplay egl_display;
  EGLContext egl_context;
  EGLConfig egl_config;
  EGLSurface egl_surfaces;
  GLuint vertex_shader, fragment_shader, program;
  GLenum target;

  int width;
  int height;

  int format;
  void *out_ptr;
  int out_fd;
  int out_size;
} egl_ctx;

struct yuv_plane_descriptor {
    int width_divisor;
    int height_divisor;
    int format;
    int plane_index;
};

struct yuv_format_descriptor {
    uint32_t format;
    int planes;
    struct yuv_plane_descriptor plane[4];
};

struct yuv_format_descriptor format_descs[] = {
    {
        .format = DRM_FORMAT_NV12,
        .planes = 2,
        {{
            .width_divisor = 1,
            .height_divisor = 1,
            .plane_index = 0,
        }, {
            .width_divisor = 2,
            .height_divisor = 2,
            .plane_index = 1,
        }}
    }, {
        .format = DRM_FORMAT_NV16,
        .planes = 2,
        {{
            .width_divisor = 1,
            .height_divisor = 1,
            .plane_index = 0,
        }, {
            .width_divisor = 2,
            .height_divisor = 1,
            .plane_index = 1,
        }}
    }, {
        .format = DRM_FORMAT_YUV420,
        .planes = 3,
        {{
            .width_divisor = 1,
            .height_divisor = 1,
            .plane_index = 0,
        }, {
            .width_divisor = 2,
            .height_divisor = 2,
            .plane_index = 1,
        }, {
            .width_divisor = 2,
            .height_divisor = 2,
            .plane_index = 2,
        }}
    }, {
        .format = DRM_FORMAT_YUV422,
        .planes = 3,
        {{
            .width_divisor = 1,
            .height_divisor = 1,
            .plane_index = 0,
        }, {
            .width_divisor = 2,
            .height_divisor = 1,
            .plane_index = 1,
        }, {
            .width_divisor = 2,
            .height_divisor = 1,
            .plane_index = 2,
        }}
    }, {
        .format = DRM_FORMAT_YUV444,
        .planes = 3,
        {{
            .width_divisor = 1,
            .height_divisor = 1,
            .plane_index = 0,
        }, {
            .width_divisor = 1,
            .height_divisor = 1,
            .plane_index = 1,
        }, {
            .width_divisor = 1,
            .height_divisor = 1,
            .plane_index = 2,
        }}
    }
};

static float *egl_angle_to_texcoords(int angle)
{
    switch (angle)
    {
    case 90:
        return texcoords90;
    case 180:
        return texcoords180;
    case 270:
        return texcoords270;
    default:
        return texcoords;
    }
}

void egl_free_ctx(void *data)
{
  egl_ctx *ctx = data;
  int i;

  if (ctx->egl_display != EGL_NO_DISPLAY) {
    eglMakeCurrent(ctx->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                   EGL_NO_CONTEXT);

    if (ctx->program)
      glDeleteProgram(ctx->program);

    if (ctx->fragment_shader)
      glDeleteShader(ctx->fragment_shader);

    if (ctx->vertex_shader)
      glDeleteShader(ctx->vertex_shader);

    if (ctx->egl_surfaces != EGL_NO_SURFACE)
      eglDestroySurface(ctx->egl_display, ctx->egl_surfaces);

    if (ctx->egl_context != EGL_NO_CONTEXT)
      eglDestroyContext(ctx->egl_display, ctx->egl_context);

    eglTerminate(ctx->egl_display);
    eglReleaseThread();
  }

  if (ctx->out_fd > 0)
  {
    struct dma_buf_sync sync = { 0 };
    sync.flags = DMA_BUF_SYNC_RW | DMA_BUF_SYNC_END;
    ioctl(ctx->out_fd, DMA_BUF_IOCTL_SYNC, &sync);
    close(ctx->out_fd);
  }

  if (ctx->out_ptr)
    drmUnmap(ctx->out_ptr, ctx->out_size);
  if (ctx->gbm_surfaces)
    gbm_surface_destroy(ctx->gbm_surfaces);

  if (ctx->gbm_dev)
    gbm_device_destroy(ctx->gbm_dev);

  if (ctx->fd >= 0)
    close(ctx->fd);

  free(ctx);
}

static int egl_flush_surfaces(egl_ctx *ctx)
{
  int i;

  /* Re-create surfaces */

  eglMakeCurrent(ctx->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                 EGL_NO_CONTEXT);

  if (ctx->egl_surfaces != EGL_NO_SURFACE) {
    eglDestroySurface(ctx->egl_display, ctx->egl_surfaces);
    ctx->egl_surfaces = EGL_NO_SURFACE;
  }

  if (ctx->gbm_surfaces) {
    gbm_surface_destroy(ctx->gbm_surfaces);
    ctx->gbm_surfaces = NULL;
  }

  ctx->gbm_surfaces =
    gbm_surface_create(ctx->gbm_dev, ctx->width, ctx->height,
                       ctx->format, 0);
  if (!ctx->gbm_surfaces) {
    DRM_ERROR("failed to create GBM surface\n");
    return -1;
  }

  ctx->egl_surfaces =
    eglCreateWindowSurface(ctx->egl_display, ctx->egl_config,
                           (EGLNativeWindowType)ctx->gbm_surfaces, NULL);
  if (ctx->egl_surfaces == EGL_NO_SURFACE) {
    DRM_ERROR("failed to create EGL surface\n");
    return -1;
  }

  return 0;
}

void *egl_init_ctx(int fd, int width, int height, int format, int angle)
{
  PFNEGLGETPLATFORMDISPLAYEXTPROC get_platform_display;

#define EGL_MAX_CONFIG 64
  EGLConfig configs[EGL_MAX_CONFIG];
  EGLint num_configs;
  egl_ctx *ctx;

  GLint texcoord;
  GLint status;
  const char *source;
  char msg[512];
  int i;

  static const EGLint context_attribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };

  static const EGLint config_attribs[] = {
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_RED_SIZE, 1,
    EGL_GREEN_SIZE, 1,
    EGL_BLUE_SIZE, 1,
    EGL_ALPHA_SIZE, 0,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE
  };
  const char *extensions;
  extensions = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
  if (extensions) {
    DRM_ERROR("%s\n", extensions);
  }
  get_platform_display = (void *)eglGetProcAddress("eglGetPlatformDisplayEXT");
  if (!get_platform_display) {
    DRM_ERROR("failed to get proc address\n");
    return NULL;
  }

  ctx = calloc(1, sizeof(*ctx));
  if (!ctx) {
    DRM_ERROR("failed to alloc ctx\n");
    return NULL;
  }

  ctx->width = width;
  ctx->height = height;
  ctx->format = format;

  ctx->fd = dup(fd);
  if (ctx->fd < 0) {
    DRM_ERROR("failed to dup drm fd\n");
    goto err;
  }

  ctx->gbm_dev = gbm_create_device(ctx->fd);
  if (!ctx->gbm_dev) {
    DRM_ERROR("failed to create gbm device\n");
    goto err;
  }

  ctx->egl_display = get_platform_display(EGL_PLATFORM_GBM_KHR,
                                          (void*)ctx->gbm_dev, NULL);
  if (ctx->egl_display == EGL_NO_DISPLAY) {
    DRM_ERROR("failed to get platform display\n");
    goto err;
  }

  if (!eglInitialize(ctx->egl_display, NULL, NULL)) {
    DRM_ERROR("failed to init egl\n");
    goto err;
  }

  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    DRM_ERROR("failed to bind api\n");
    goto err;
  }

  if (!eglChooseConfig(ctx->egl_display, config_attribs,
                       configs, EGL_MAX_CONFIG, &num_configs) ||
      num_configs < 1) {
    DRM_ERROR("failed to choose config\n");
    goto err;
  }

  for (i = 0; i < num_configs; i++) {
    EGLint value;

    if (!eglGetConfigAttrib(ctx->egl_display, configs[i],
                            EGL_NATIVE_VISUAL_ID, &value))
      continue;

    if (value == format)
      break;
  }

  if (i == num_configs) {
    DRM_ERROR("failed to find EGL config for %4s, force using the first\n",
              (char *)&format);
    ctx->egl_config = configs[0];
  } else {
    ctx->egl_config = configs[i];
  }

  ctx->egl_context = eglCreateContext(ctx->egl_display, ctx->egl_config,
                                      EGL_NO_CONTEXT, context_attribs);
  if (ctx->egl_context == EGL_NO_CONTEXT) {
    DRM_ERROR("failed to create EGL context\n");
    goto err;
  }

  if (egl_flush_surfaces(ctx) < 0) {
    DRM_ERROR("failed to flush surfaces\n");
    goto err;
  }

  eglMakeCurrent(ctx->egl_display, ctx->egl_surfaces,
                 ctx->egl_surfaces,
                 ctx->egl_context);

  source = vertex_shader_source;
  ctx->vertex_shader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(ctx->vertex_shader, 1, &source, NULL);
  glCompileShader(ctx->vertex_shader);
  glGetShaderiv(ctx->vertex_shader, GL_COMPILE_STATUS, &status);
  if (!status) {
    glGetShaderInfoLog(ctx->vertex_shader, sizeof(msg), NULL, msg);
    DRM_ERROR("failed to compile shader: %s\n", msg);
    goto err;
  }

  source = fragment_shader_source;
  ctx->fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(ctx->fragment_shader, 1, &source, NULL);
  glCompileShader(ctx->fragment_shader);
  glGetShaderiv(ctx->fragment_shader, GL_COMPILE_STATUS, &status);
  if (!status) {
    glGetShaderInfoLog(ctx->fragment_shader, sizeof(msg), NULL, msg);
    DRM_ERROR("failed to compile shader: %s\n", msg);
    goto err;
  }

  ctx->program = glCreateProgram();
  glAttachShader(ctx->program, ctx->vertex_shader);
  glAttachShader(ctx->program, ctx->fragment_shader);
  glLinkProgram(ctx->program);

  glGetProgramiv(ctx->program, GL_LINK_STATUS, &status);
  if (!status) {
    glGetProgramInfoLog(ctx->program, sizeof(msg), NULL, msg);
    DRM_ERROR("failed to link: %s\n", msg);
    goto err;
  }

  glUseProgram(ctx->program);

  texcoord = glGetAttribLocation(ctx->program, "texcoord");
  glVertexAttribPointer(texcoord, 2, GL_FLOAT, GL_FALSE, 0, egl_angle_to_texcoords(angle));
  glEnableVertexAttribArray(texcoord);

  glUniform1i(glGetUniformLocation(ctx->program, "tex"), 0);

  glViewport(0, 0, width, height);
  printf("glViewport %d %d\n", width, height);

  return ctx;
err:
  egl_free_ctx(ctx);
  return NULL;
}

static int egl_handle_to_fd(int fd, uint32_t handle)
{
  struct drm_prime_handle args = {
    .fd = -1,
    .handle = handle,
  };
  int ret;

  ret = drmIoctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &args);
  if (ret < 0) {
    DRM_ERROR("failed to get fd (%d)\n", errno);
    return ret;
  }

  return args.fd;
}

static void *egl_bo_to_ptr(egl_ctx *ctx, struct gbm_bo* bo, int format, int *size)
{
    struct drm_mode_map_dumb arg = {
        .handle = gbm_bo_get_handle(bo).u32,
    };
    struct drm_prime_handle fd_args = {
        .fd = -1,
        .handle = gbm_bo_get_handle(bo).u32,
        .flags = 0,
    };
    struct dma_buf_sync sync = { 0 };
    int ret;

    ret = drmIoctl(ctx->fd, DRM_IOCTL_MODE_MAP_DUMB, &arg);
    if (ret)
        return ret;

    ret = drmIoctl(ctx->fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &fd_args);
    if (ret)
    {
        printf("handle_to_fd failed ret=%d, handle=%x \n", ret ,fd_args.handle);
        return -1;
    }

    sync.flags = DMA_BUF_SYNC_RW | DMA_BUF_SYNC_START;
    ioctl(fd_args.fd, DMA_BUF_IOCTL_SYNC, &sync);
    ctx->out_fd = fd_args.fd;

    *size = gbm_bo_get_height(bo) * gbm_bo_get_stride(bo);
    char *ptr = mmap(0, *size, PROT_READ, MAP_SHARED,
                     ctx->fd, arg.offset);
    if (ptr == MAP_FAILED) {
        return NULL;
    }

    return ptr;
}

static int egl_attach_dmabuf(egl_ctx *ctx, int dma_fd,
                             int width, int height,
                             int stride, int format)
{
  static PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d = NULL;
  static PFNEGLCREATEIMAGEKHRPROC create_image = NULL;
  static PFNEGLDESTROYIMAGEKHRPROC destroy_image = NULL;
  struct yuv_format_descriptor *fmt_desc = NULL;
  EGLImageKHR image;

  EGLint attrs[52];
  int attrsi = 0;

  for (int i = 0; i < sizeof(format_descs) / sizeof(format_descs[0]); i++)
  {
      if (format == format_descs[i].format)
      {
          fmt_desc = &format_descs[i];
          break;
      }
  }
  if (!fmt_desc)
  {
      DRM_ERROR("No support %x\n", format);
      return NULL;
  }

  attrs[attrsi++] = EGL_WIDTH;
  attrs[attrsi++] = width;
  attrs[attrsi++] = EGL_HEIGHT;
  attrs[attrsi++] = height;
  attrs[attrsi++] = EGL_LINUX_DRM_FOURCC_EXT;
  attrs[attrsi++] = format;
  attrs[attrsi++] = EGL_IMAGE_PRESERVED_KHR;
  attrs[attrsi++] = EGL_TRUE;
  attrs[attrsi++] = EGL_YUV_COLOR_SPACE_HINT_EXT;
  attrs[attrsi++] = EGL_ITU_REC601_EXT;
  attrs[attrsi++] = EGL_SAMPLE_RANGE_HINT_EXT;
  attrs[attrsi++] = EGL_YUV_NARROW_RANGE_EXT;

  attrs[attrsi++] = EGL_DMA_BUF_PLANE0_FD_EXT;
  attrs[attrsi++] = dma_fd;
  attrs[attrsi++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
  attrs[attrsi++] = 0;
  attrs[attrsi++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
  attrs[attrsi++] = stride;

  if (fmt_desc->planes >= 2)
  {
    int s = stride / fmt_desc->plane[0].width_divisor;
    int h = ALIGN(height, 16) / fmt_desc->plane[0].height_divisor;
    attrs[attrsi++] = EGL_DMA_BUF_PLANE1_FD_EXT;
    attrs[attrsi++] = dma_fd;
    attrs[attrsi++] = EGL_DMA_BUF_PLANE1_OFFSET_EXT;
    attrs[attrsi++] = s * h;
    attrs[attrsi++] = EGL_DMA_BUF_PLANE1_PITCH_EXT;
    attrs[attrsi++] = stride;
  }

  if (fmt_desc->planes >= 3)
  {
    int s0 = stride / fmt_desc->plane[0].width_divisor;
    int h0 = ALIGN(height, 16) / fmt_desc->plane[0].height_divisor;
    int s = stride / fmt_desc->plane[1].width_divisor;
    int h = ALIGN(height, 16) / fmt_desc->plane[1].height_divisor;
    attrs[attrsi++] = EGL_DMA_BUF_PLANE2_FD_EXT;
    attrs[attrsi++] = dma_fd;
    attrs[attrsi++] = EGL_DMA_BUF_PLANE2_OFFSET_EXT;
    attrs[attrsi++] = s * h + s0 * h0;
    attrs[attrsi++] = EGL_DMA_BUF_PLANE2_PITCH_EXT;
    attrs[attrsi++] = stride;
  }

  attrs[attrsi++] = EGL_NONE;

  if (!create_image)
    create_image = (void *) eglGetProcAddress("eglCreateImageKHR");

  if (!destroy_image)
    destroy_image = (void *) eglGetProcAddress("eglDestroyImageKHR");

  if (!image_target_texture_2d)
    image_target_texture_2d =
      (void *) eglGetProcAddress("glEGLImageTargetTexture2DOES");

  if (!create_image || !destroy_image || !image_target_texture_2d) {
    DRM_ERROR("failed to get proc address\n");
    return -1;
  }

  image = create_image(ctx->egl_display, EGL_NO_CONTEXT,
                       EGL_LINUX_DMA_BUF_EXT, NULL, attrs);
  if (image == EGL_NO_IMAGE) {
    DRM_ERROR("failed to create egl image: 0x%x\n", eglGetError());
    return -1;
  }

  image_target_texture_2d(ctx->target, (GLeglImageOES)image);
  destroy_image(ctx->egl_display, image);
  return 0;
}

void *egl_convert_fb(void *data, int dma_fd,
                     int width, int height,
                     int stride, int format)
{
  egl_ctx *ctx = data;
  GLint position;
  GLuint texture;
  struct gbm_bo* bo;

  GLfloat verts[] = {
    -1.0f, -1.0f,
     1.0f, -1.0f,
    -1.0f,  1.0f,
     1.0f,  1.0f,
  };

  eglMakeCurrent(ctx->egl_display, ctx->egl_surfaces,
                 ctx->egl_surfaces,
                 ctx->egl_context);

  position = glGetAttribLocation(ctx->program, "position");
  glVertexAttribPointer(position, 2, GL_FLOAT, GL_FALSE, 0, verts);
  glEnableVertexAttribArray(position);

  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  GLenum target = GL_TEXTURE_EXTERNAL_OES;
  glActiveTexture(GL_TEXTURE0);
  glGenTextures(1, &texture);
  glBindTexture(target, texture);
  glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  ctx->target = target;

  if (egl_attach_dmabuf(ctx, dma_fd, width, height, stride, format) < 0) {
    DRM_ERROR("failed to attach dmabuf\n");
    goto err_del_texture;
  }

  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  eglSwapBuffers(ctx->egl_display, ctx->egl_surfaces);

  bo = gbm_surface_lock_front_buffer(ctx->gbm_surfaces);
  if (!bo) {
    DRM_ERROR("failed to get front bo\n");
    goto err_del_texture;
  }
  ctx->out_ptr = egl_bo_to_ptr(ctx, bo, ctx->format, &ctx->out_size);
  gbm_surface_release_buffer(ctx->gbm_surfaces, bo);

err_del_texture:
  glDeleteTextures(1, &texture);
  return ctx->out_ptr;
}
