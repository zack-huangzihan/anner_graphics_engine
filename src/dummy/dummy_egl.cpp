/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// OpenGL ES 2.0 code

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <assert.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdbool.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>

#include <libdrm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <anner_effects.h>

#define IVI_SURFACE_ID 9000

#define ALIGN(_v, _d) (((_v) + ((_d) - 1)) & ~((_d) - 1))

#define DEBUG_MSG(fmt, ...) \
        do { \
            printf(fmt " (%s:%d)\n", \
                    ##__VA_ARGS__, __FUNCTION__, __LINE__); \
        } while (0)
#define ERROR_MSG(fmt, ...) \
        do { printf("ERROR: " fmt " (%s:%d)\n", \
                ##__VA_ARGS__, __FUNCTION__, __LINE__); } while (0)


#define ECHK(x) do { \
        EGLImageKHR status; \
        DEBUG_MSG(">>> %s", #x); \
        status = (EGLImageKHR)(x); \
        if (!status) { \
            EGLint err = eglGetError(); \
            ERROR_MSG("<<< %s: failed: 0x%04x (    )", #x, err); \
            exit(-1); \
        } \
        DEBUG_MSG("<<< %s: succeeded", #x); \
    } while (0)

#define GCHK(x) do { \
        GLenum err; \
        DEBUG_MSG(">>> %s", #x); \
        x; \
        err = glGetError(); \
        if (err != GL_NO_ERROR) { \
            ERROR_MSG("<<< %s: failed: 0x%04x ( )", #x, err ); \
            exit(-1); \
        } \
        DEBUG_MSG("<<< %s: succeeded", #x); \
    } while (0)

static void printGLString(const char *name, GLenum s) {
    // fprintf(stderr, "printGLString %s, %d\n", name, s);
    const char *v = (const char *) glGetString(s);
    fprintf(stderr, "GL %s = %s\n", name, v);
}


static void checkGlError(const char* op) {
    for (GLint error = glGetError(); error; error
            = glGetError()) {
        fprintf(stderr, "after %s() glError (0x%x)\n", op, error);
    }
}

static const char gVertexShader[] =
      "#version 300 es                            \n"
      "layout(location = 0) in vec4 a_position;   \n"
      "layout(location = 1) in vec2 a_texCoord;   \n"
      "out vec2 v_texCoord;                       \n"
      "void main()                                \n"
      "{                                          \n"
      "   gl_Position = a_position;               \n"
      "   v_texCoord = a_texCoord;                \n"
      "}                                          \n";

static const char gFragmentShader[] =
      "#version 300 es                                     \n"
      "precision mediump float;                            \n"
      "in vec2 v_texCoord;                                 \n"
      "layout(location = 0) out vec4 outColor;             \n"
      "uniform sampler2D s_texture;                        \n"
      "void main()                                         \n"
      "{                                                   \n"
      "  outColor = texture( s_texture, v_texCoord );      \n"
      "}                                                   \n";

GLuint gProgram;
GLuint gvTextureSamplerHandle;
GLuint Gtexture;
GLuint Otexture;
EGLBoolean returnValue;
EGLConfig myConfig = {0};

EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
EGLint s_configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE };

EGLint majorVersion;
EGLint minorVersion;
EGLContext context;
EGLSurface surface;
EGLint surface_w, surface_h;

EGLDisplay dpy;
GLuint out_fbo_id = 0;

uint32_t in_handle;
uint32_t out_handle;  

GLuint loadShader(GLenum shaderType, const char* pSource) {
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        glShaderSource(shader, 1, &pSource, NULL);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen) {
                char* buf = (char*) malloc(infoLen);
                if (buf) {
                    glGetShaderInfoLog(shader, infoLen, NULL, buf);
                    fprintf(stderr, "Could not compile shader %d:\n%s\n",
                            shaderType, buf);
                    free(buf);
                }
                glDeleteShader(shader);
                shader = 0;
            }
        }
    }
    return shader;
}

GLuint createProgram(const char* pVertexSource, const char* pFragmentSource) {
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
    if (!vertexShader) {
        return 0;
    }

    GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
    if (!pixelShader) {
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program) {
        glAttachShader(program, vertexShader);
        checkGlError("glAttachShader");
        glAttachShader(program, pixelShader);
        checkGlError("glAttachShader");
        glLinkProgram(program);
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint bufLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
            if (bufLength) {
                char* buf = (char*) malloc(bufLength);
                if (buf) {
                    glGetProgramInfoLog(program, bufLength, NULL, buf);
                    fprintf(stderr, "Could not link program:\n%s\n", buf);
                    free(buf);
                }
            }
            glDeleteProgram(program);
            program = 0;
        }
    }
    return program;
}

GLfloat gTriangleVertices[] = { -1.0f,  -1.0f, 0.0f,  // Position 0
                                0.0f,  0.0f,        // TexCoord 0 
                                -1.0f, 1.0f, 0.0f,  // Position 1
                                0.0f,  1.0f,        // TexCoord 1
                                1.0f, 1.0f, 0.0f,  // Position 2
                                1.0f,  1.0f,        // TexCoord 2
                                1.0f,  -1.0f, 0.0f,  // Position 3
                                1.0f,  0.0f         // TexCoord 3
                              };

GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

static void
set_glView(GLfloat *data, uint32_t width, uint32_t height, uint32_t angle)
{
    matrix_rotation(data, angle);
    printf("set_glView w = %d h= %d angle = %d\n",width, height, angle);

    glViewport(0, 0, width, height);

}

bool setupGraphics() {
    gProgram = createProgram(gVertexShader, gFragmentShader);
    if (!gProgram) {
        return false;
    }

    gvTextureSamplerHandle = glGetUniformLocation(gProgram, "s_texture");
    checkGlError("glGetAttribLocation");
    //glViewport(0, 0, w, h);
    glUseProgram(gProgram);
    return true;
}

void renderFrame(int w, int h) {

    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    checkGlError("glClearColor");
    glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    checkGlError("glClear");

    checkGlError("glUseProgram");
    set_glView(gTriangleVertices, w, h, effects_angle);
    glVertexAttribPointer ( 0, 3, GL_FLOAT,
                           GL_FALSE, 5 * sizeof ( GLfloat ), gTriangleVertices );

    glVertexAttribPointer ( 1, 2, GL_FLOAT,
                           GL_FALSE, 5 * sizeof ( GLfloat ), &gTriangleVertices[3] );
    glEnableVertexAttribArray (0);
    glEnableVertexAttribArray (1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, Gtexture);
    glUniform1i (gvTextureSamplerHandle, 0);
    glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices );
    checkGlError("glDrawElements");
}



static void checkEglError(const char* op, EGLBoolean returnVal = EGL_TRUE) {
    if (returnVal != EGL_TRUE) {
        fprintf(stderr, "%s() returned %d\n", op, returnVal);
    }

    for (EGLint error = eglGetError(); error != EGL_SUCCESS; error
            = eglGetError()) {
        fprintf(stderr, "after %s() eglError (0x%x)\n", op,error);
    }
}

int anner_dumpPixels(int len, int inWindowWidth, int inWindowHeight, unsigned char * pPixelDataFront, char* file_name){
    glReadPixels(0, 0, inWindowWidth, inWindowHeight, GL_RGBA, GL_UNSIGNED_BYTE, pPixelDataFront);
    //sprintf(file_name,"/home/rockchip/gpu_anner/dumplayer_%d_%dx%d.bin");
    if(1)
    {
        FILE *file = fopen(file_name, "wb+");
        if (!file)
        {
            printf("Could not open /%s \n",file_name);
            return -1;
        } else {
            printf("open %s and write ok\n",file_name);
        }
        fwrite(pPixelDataFront, len, 1, file);
        fclose(file);
    }
    printf("anner_dumpPixels is end\n");
    return 0;
}

void *buf_alloc(int *fd, int Tex_w, int Tex_h, int type)
{
    struct drm_prime_handle fd_args;
    struct drm_mode_map_dumb mmap_arg;
    struct drm_mode_destroy_dumb destory_arg;
    struct drm_mode_create_dumb alloc_arg;
    static const char* card = "/dev/dri/card0";
    int drm_fd = -1;
    int flag = O_RDWR;
    int ret;
    void *map = NULL;

    void *vir_addr = NULL;

    drm_fd = open(card, flag);
    if(drm_fd < 0)
    {
        printf("failed to open %s\n", card);
        return NULL;
    }

    memset(&alloc_arg, 0, sizeof(alloc_arg));
    alloc_arg.bpp = 32;
    alloc_arg.width = Tex_w;
    alloc_arg.height = Tex_h;

    ret = drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &alloc_arg);
    if (ret) {
        printf("failed to create dumb buffer: %s\n", strerror(errno));
        return NULL;
    }

    memset(&fd_args, 0, sizeof(fd_args));
    fd_args.fd = -1;
    fd_args.handle = alloc_arg.handle;;
    fd_args.flags = 0;
    ret = drmIoctl(drm_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &fd_args);
    if (ret)
    {
        printf("rk-debug handle_to_fd failed ret=%d,err=%s, handle=%x \n",ret,strerror(errno),fd_args.handle);
        return NULL;
    }
    printf("Dump fd = %d \n",fd_args.fd);
    *fd = fd_args.fd;

  //handle to Virtual address
    memset(&mmap_arg, 0, sizeof(mmap_arg));
    mmap_arg.handle = alloc_arg.handle;

    ret = drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mmap_arg);
    if (ret) {
        printf("failed to create map dumb: %s\n", strerror(errno));
        vir_addr = NULL;
        goto destory_dumb;
    }
    vir_addr = map = mmap64(0, alloc_arg.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, mmap_arg.offset);
    if (map == MAP_FAILED) {
        printf("failed to mmap buffer: %s\n", strerror(errno));
        vir_addr = NULL;
        goto destory_dumb;
    }
    if (type == 0) {
      in_handle = alloc_arg.handle;  
    } else {
      out_handle = alloc_arg.handle;    
    }
    printf("alloc map=%x \n",map);
    return vir_addr;
destory_dumb:
  memset(&destory_arg, 0, sizeof(destory_arg));
  destory_arg.handle = alloc_arg.handle;
  int fdd = *fd ;
  ret = drmIoctl(fdd, DRM_IOCTL_MODE_DESTROY_DUMB, &destory_arg);
  if (ret)
    printf("failed to destory dumb %d\n", ret);
  return vir_addr;
}

void anner_create_window(int window_width, int window_height) {
    checkEglError("<init>");
    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    checkEglError("eglGetDisplay");
    if (dpy == EGL_NO_DISPLAY) {
        printf("eglGetDisplay returned EGL_NO_DISPLAY.\n");
        exit(0);
    }

    returnValue = eglInitialize(dpy, &majorVersion, &minorVersion);
    checkEglError("eglInitialize", returnValue);
    fprintf(stderr, "EGL version %d.%d\n", majorVersion, minorVersion);
    if (returnValue != EGL_TRUE) {
        printf("eglInitialize failed\n");
        exit(0);
    }


    EGLint numConfig = 0;
    eglChooseConfig(dpy, s_configAttribs, 0, 0, &numConfig);
    int num = numConfig;
    if(num != 0){
       EGLConfig configs[num];
       //获取所有满足attributes的configs
       eglChooseConfig(dpy, s_configAttribs, configs, num, &numConfig);
       myConfig = configs[0]; //以某种规则选择一个config，这里使用了最简单的规则。
    }


    int sw = window_width;
    int sh = window_height;
    EGLint attribs[] = { EGL_WIDTH, sw, EGL_HEIGHT, sh, EGL_LARGEST_PBUFFER, EGL_TRUE, EGL_NONE, EGL_NONE };
    surface = eglCreatePbufferSurface(dpy, myConfig, attribs);

    checkEglError("eglCreateWindowSurface");
    if (surface == EGL_NO_SURFACE) {
        printf("eglCreateWindowSurface failed.\n");
        exit(0);
    }

    context = eglCreateContext(dpy, myConfig, EGL_NO_CONTEXT, context_attribs);
    checkEglError("eglCreateContext");
    if (context == EGL_NO_CONTEXT) {
        printf("eglCreateContext failed\n");
        exit(0);
    }
    returnValue = eglMakeCurrent(dpy, surface, surface, context);
    checkEglError("eglMakeCurrent", returnValue);
    if (returnValue != EGL_TRUE) {
        exit(0);
    }
    eglQuerySurface(dpy, surface, EGL_WIDTH, &surface_w);
    checkEglError("eglQuerySurface");
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &surface_h);
    checkEglError("eglQuerySurface");

    fprintf(stderr, "Window dimensions: %d x %d\n", surface_w, surface_h);
}

int anner_create_intput(void** pixels, int *drmbuf_fd, int w, int h, int format){

    *pixels = buf_alloc(drmbuf_fd, w, h, 0);
    printf("intput rk-debug [%d,%x] \n",*drmbuf_fd, *pixels);

    return 0;
}

int anner_create_output(void** pixels, int *drmbuf_fd, int w, int h, int format){

    *pixels = buf_alloc(drmbuf_fd, w, h, 1);
    EGLImageKHR img = NULL;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d;
    PFNEGLCREATEIMAGEKHRPROC create_image;
    printf("output rk-debug [%d,%x] \n",*drmbuf_fd, *pixels);
    create_image = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
    image_target_texture_2d = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress("glEGLImageTargetTexture2DOES");
    
    int out_fd = *drmbuf_fd;
    unsigned stride = ALIGN(w, 32) * 4;
    EGLint attr[] = {
            EGL_WIDTH, w,
            EGL_HEIGHT, h,
            EGL_LINUX_DRM_FOURCC_EXT, format,
            EGL_DMA_BUF_PLANE0_FD_EXT, out_fd,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, stride,
            EGL_NONE
    };

    img = create_image(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, (EGLClientBuffer)NULL, attr);
    ECHK(img);
    if(img == EGL_NO_IMAGE_KHR)
    {
        printf("rk-debug eglCreateImageKHR NULL \n ");
        return 0;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glActiveTexture(GL_TEXTURE1);
    glGenTextures(1, &Otexture);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, Otexture);
    image_target_texture_2d(GL_TEXTURE_EXTERNAL_OES, img);

    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    glGenFramebuffers(1, &out_fbo_id);
    glBindFramebuffer(GL_FRAMEBUFFER, out_fbo_id);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_EXTERNAL_OES, Otexture, 0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        printf("rk_debug create fbo success! fbo = %d\n", out_fbo_id);
    } else {
        printf("rk_debug create fbo failed!\n");
    }


    return 0;
}

void anner_activation_texture(void* pixels, int drmbuf_fd, int w, int h, int format) {
    EGLImageKHR img = NULL;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d;

    PFNEGLCREATEIMAGEKHRPROC create_image;
    unsigned stride = ALIGN(w, 32) * 4;
    EGLint attr[] = {
            EGL_WIDTH, w,
            EGL_HEIGHT, h,
            EGL_LINUX_DRM_FOURCC_EXT, format,
            EGL_DMA_BUF_PLANE0_FD_EXT, drmbuf_fd,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, stride,
            EGL_NONE
    };
    printf("in rk-debug [%d,%x] \n",drmbuf_fd, pixels);
    create_image = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
    image_target_texture_2d = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress("glEGLImageTargetTexture2DOES");

    img = create_image(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, (EGLClientBuffer)NULL, attr);
    ECHK(img);
    if(img == EGL_NO_IMAGE_KHR)
    {
        printf("rk-debug eglCreateImageKHR NULL \n ");
        return ;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &Gtexture);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, Gtexture);
    image_target_texture_2d(GL_TEXTURE_EXTERNAL_OES, img);

    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
}

void anner_set_effects(int Angle) {
    effects_angle = Angle;
}

void anner_render(int w, int h) {
    if(!setupGraphics()) {
        fprintf(stderr, "Could not set up graphics.\n");
        exit(0);
    }
    renderFrame(w, h);
    glFinish();
}

int anner_disable_texture() {
    glDeleteTextures(1, &Gtexture);
}

int anner_delete_buf(void* pixels, int drm_fd, int len, int type) {
    glDeleteTextures(1, &Gtexture);
    glDeleteTextures(1, &Otexture);
    if (pixels) {
        munmap(pixels, len);
    }
    if (drm_fd) {
        close(drm_fd);
    }

    struct drm_mode_destroy_dumb destory_arg;
    memset(&destory_arg, 0, sizeof(destory_arg));
    if (type == 0) {
        destory_arg.handle = in_handle;  
    } else {
        destory_arg.handle = out_handle;
    } 
    int ret = drmIoctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destory_arg);
    return ret;
}

void anner_destory_window(void) {
    eglDestroyContext(dpy, context);
    eglDestroySurface(dpy, surface);
    eglTerminate(dpy);
}