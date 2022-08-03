#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <assert.h>
#include <errno.h>
#include <sys/mman.h>
#include <stdbool.h>

#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>

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
        DEBUG_MSG("<<< %s: successed", #x); \
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
        DEBUG_MSG("<<< %s: successed", #x); \
    } while (0)

struct egl_ctx
{
    GLuint program;
    GLuint gvTextureSamplerHandle;
    GLuint Gtexture;
    GLuint Otexture;
    EGLConfig myConfig;

    EGLint majorVersion;
    EGLint minorVersion;
    EGLContext context;
    EGLSurface surface;
    EGLint surface_w;
    EGLint surface_h;

    EGLDisplay dpy;
    GLfloat triangleVertices[20];
    uint32_t format;
    int out_fbo_id;
};

static const char gVertexShader[] =
    "attribute vec4 a_position;                 \n"
    "attribute vec2 a_texCoord;                 \n"
    "varying vec2 v_texCoord;                   \n"
    "void main()                                \n"
    "{                                          \n"
    "   v_texCoord = a_texCoord;                \n"
    "   gl_Position = a_position;               \n"
    "}                                          \n";

static const char gFragmentShader[] =
    "precision mediump float;                            \n"
    "varying vec2 v_texCoord;                            \n"
    "uniform sampler2D s_texture;                        \n"
    "void main()                                         \n"
    "{                                                   \n"
    "  gl_FragColor = texture2D( s_texture, v_texCoord );\n"
    "}                                                   \n";

EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
EGLint s_configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE };

static const GLfloat gTriangleVertices[] = {
    -1.0f,  -1.0f,  0.0f,  // Position 0
     0.0f,   0.0f,         // TexCoord 0
    -1.0f,   1.0f,  0.0f,  // Position 1
     0.0f,   1.0f,         // TexCoord 1
     1.0f,   1.0f,  0.0f,  // Position 2
     1.0f,   1.0f,         // TexCoord 2
     1.0f,  -1.0f,  0.0f,  // Position 3
     1.0f,   0.0f          // TexCoord 3
};
static const GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

static void initTriangleVertices(GLfloat *triangleVertices)
{
    memcpy(triangleVertices, &gTriangleVertices, sizeof(gTriangleVertices));
}

static void matrix_rotation(GLfloat *data, uint32_t angle)
{
    GLfloat ret = 0;

    switch (angle) {
    case 180:
        ret = data[3]; data[3] = data[18]; data[18]=ret;
        ret = data[4]; data[4] = data[19]; data[19]=ret;
        ret = data[8]; data[8] = data[13]; data[13]=ret;
        ret = data[9]; data[9] = data[14]; data[14]=ret;
        break;
    case 90:
        ret = data[3]; data[3] = data[8]; data[8]=ret;
        ret = data[4]; data[4] = data[9]; data[9]=ret;
        ret = data[3]; data[3] = data[13]; data[13]=ret;
        ret = data[4]; data[4] = data[14]; data[14]=ret;
        ret = data[3]; data[3] = data[18]; data[18]=ret;
        ret = data[4]; data[4] = data[19]; data[19]=ret;
        break;
    }
}

static void checkGlError(const char* op) {
    for (GLint error = glGetError(); error; error
            = glGetError()) {
        fprintf(stderr, "after %s() glError (0x%x)\n", op, error);
    }
}

static void checkEglError(const char* op) {
    for (EGLint error = eglGetError(); error != EGL_SUCCESS; error
            = eglGetError()) {
        fprintf(stderr, "after %s() eglError (0x%x)\n", op,error);
    }
}

static GLuint loadShader(GLenum shaderType, const char* pSource) {
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        glShaderSource(shader, 1, &pSource, NULL);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            fprintf(stderr, "Could not compile shader %x:\n", shaderType);
            if (infoLen) {
                char* buf = (char*) malloc(infoLen);
                if (buf) {
                    glGetShaderInfoLog(shader, infoLen, NULL, buf);
                    fprintf(stderr, "%s\n", buf);
                    free(buf);
                }
                glDeleteShader(shader);
                shader = 0;
            }
        }
    }
    return shader;
}

static GLuint createProgram(const char* pVertexSource, const char* pFragmentSource) {
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
            fprintf(stderr, "Could not link program:\n");
            if (bufLength) {
                char* buf = (char*) malloc(bufLength);
                if (buf) {
                    glGetProgramInfoLog(program, bufLength, NULL, buf);
                    fprintf(stderr, "%s\n", buf);
                    free(buf);
                }
            }
            glDeleteProgram(program);
            program = 0;
        }
    } else {
        printf("create program faild\n");
    }
    return program;
}

static int setupGraphics(struct egl_ctx *ectx) {
    ectx->program = createProgram(gVertexShader, gFragmentShader);
    if (!ectx->program)
        return -1;

    ectx->gvTextureSamplerHandle = glGetUniformLocation(ectx->program, "s_texture");
    checkGlError("glGetAttribLocation");
    glUseProgram(ectx->program);

    return 0;
}

static void renderFrame(struct egl_ctx *ectx, int w, int h, int angle) {
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    checkGlError("glClearColor");
    glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    checkGlError("glClear");

    checkGlError("glUseProgram");
    matrix_rotation(ectx->triangleVertices, angle);
    printf("set_glView w = %d h= %d angle = %d\n", w, h, angle);
    glViewport(0, 0, w, h);

    glVertexAttribPointer ( 0, 3, GL_FLOAT,
                           GL_FALSE, 5 * sizeof ( GLfloat ), ectx->triangleVertices );

    glVertexAttribPointer ( 1, 2, GL_FLOAT,
                           GL_FALSE, 5 * sizeof ( GLfloat ), &(ectx->triangleVertices[3]) );
    glEnableVertexAttribArray (0);
    glEnableVertexAttribArray (1);
    glActiveTexture(GL_TEXTURE0);
    checkGlError("glActiveTexture");
    glBindTexture(GL_TEXTURE_2D, ectx->Gtexture);
    checkGlError("glBindTexture");
    glUniform1i (ectx->gvTextureSamplerHandle, 0);
    checkGlError("glUniform1i");
    glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices );
    checkGlError("glDrawElements");
}

void *ectx_create_window(int w, int h)
{
    struct egl_ctx *ectx;
    int returnValue;

    ectx = malloc(sizeof(struct egl_ctx));
    if (!ectx)
        return NULL;

    initTriangleVertices(ectx->triangleVertices);

    checkEglError("<init>");
    ectx->dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    checkEglError("eglGetDisplay");
    if (ectx->dpy == EGL_NO_DISPLAY) {
        printf("eglGetDisplay returned EGL_NO_DISPLAY.\n");
        return NULL;
    }

    returnValue = eglInitialize(ectx->dpy, &ectx->majorVersion, &ectx->minorVersion);
    checkEglError("eglInitialize");
    fprintf(stderr, "EGL version %d.%d\n", ectx->majorVersion, ectx->minorVersion);
    if (returnValue != EGL_TRUE) {
        printf("eglInitialize failed\n");
        return NULL;
    }

    EGLint numConfig = 0;
    eglChooseConfig(ectx->dpy, s_configAttribs, 0, 0, &numConfig);
    int num = numConfig;
    if(num != 0){
       EGLConfig configs[num];
       //获取所有满足attributes的configs
       eglChooseConfig(ectx->dpy, s_configAttribs, configs, num, &numConfig);
       ectx->myConfig = configs[0]; //以某种规则选择一个config，这里使用了最简单的规则。
    }

    EGLint attribs[] = { EGL_WIDTH, w, EGL_HEIGHT, h, EGL_LARGEST_PBUFFER, EGL_TRUE, EGL_NONE, EGL_NONE };
    ectx->surface = eglCreatePbufferSurface(ectx->dpy, ectx->myConfig, attribs);

    checkEglError("eglCreateWindowSurface");
    if (ectx->surface == EGL_NO_SURFACE) {
        printf("eglCreateWindowSurface failed.\n");
        return NULL;
    }

    ectx->context = eglCreateContext(ectx->dpy, ectx->myConfig, EGL_NO_CONTEXT, context_attribs);
    checkEglError("eglCreateContext");
    if (ectx->context == EGL_NO_CONTEXT) {
        printf("eglCreateContext failed\n");
        return NULL;
    }
    returnValue = eglMakeCurrent(ectx->dpy, ectx->surface, ectx->surface, ectx->context);
    checkEglError("eglMakeCurrent");
    if (returnValue != EGL_TRUE) {
        return NULL;
    }
    eglQuerySurface(ectx->dpy, ectx->surface, EGL_WIDTH, &ectx->surface_w);
    checkEglError("eglQuerySurface");
    eglQuerySurface(ectx->dpy, ectx->surface, EGL_HEIGHT, &ectx->surface_h);
    checkEglError("eglQuerySurface");

    fprintf(stderr, "Window dimensions: %d x %d\n", ectx->surface_w, ectx->surface_h);

    return (void *)ectx;
}

int ectx_activation_texture(void *_ectx, int drmbuf_fd,
                            int w, int h, int stride, int format) {
    struct egl_ctx *ectx = (struct egl_ctx *)_ectx;
    EGLImageKHR img = NULL;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d;

    PFNEGLCREATEIMAGEKHRPROC create_image;
    EGLint attr[] = {
            EGL_WIDTH, w,
            EGL_HEIGHT, h,
            EGL_LINUX_DRM_FOURCC_EXT, format,
            EGL_DMA_BUF_PLANE0_FD_EXT, drmbuf_fd,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, stride,
            EGL_NONE
    };
    ectx->format = format;
    create_image = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
    image_target_texture_2d =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress("glEGLImageTargetTexture2DOES");

    img = create_image(ectx->dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, (EGLClientBuffer)NULL, attr);
    ECHK(img);
    if(img == EGL_NO_IMAGE_KHR)
    {
        printf("rk-debug eglCreateImageKHR NULL \n ");
        return -1;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glGenTextures(1, &ectx->Gtexture);
    glBindTexture(GL_TEXTURE_2D, ectx->Gtexture);
    image_target_texture_2d(GL_TEXTURE_2D, img);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    return 0;
}

int ectx_import_output(void *_ectx, int drmbuf_fd,
                       int w, int h, int stride, int format) {
    struct egl_ctx *ectx = (struct egl_ctx *)_ectx;
    EGLImageKHR img = NULL;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target_texture_2d;
    PFNEGLCREATEIMAGEKHRPROC create_image;
    int out_fd = drmbuf_fd;
    EGLint attr[] = {
            EGL_WIDTH, w,
            EGL_HEIGHT, h,
            EGL_LINUX_DRM_FOURCC_EXT, format,
            EGL_DMA_BUF_PLANE0_FD_EXT, out_fd,
            EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
            EGL_DMA_BUF_PLANE0_PITCH_EXT, stride,
            EGL_NONE
    };
    create_image = (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
    image_target_texture_2d = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress("glEGLImageTargetTexture2DOES");

    img = create_image(ectx->dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, (EGLClientBuffer)NULL, attr);
    ECHK(img);
    if(img == EGL_NO_IMAGE_KHR)
    {
        printf("rk-debug eglCreateImageKHR NULL \n ");
        return 0;
    }
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glActiveTexture(GL_TEXTURE1);
    glGenTextures(1, &ectx->Otexture);
    glBindTexture(GL_TEXTURE_2D, ectx->Otexture);
    image_target_texture_2d(GL_TEXTURE_2D, img);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);

    glGenFramebuffers(1, &ectx->out_fbo_id);
    glBindFramebuffer(GL_FRAMEBUFFER, ectx->out_fbo_id);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ectx->Otexture, 0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
        printf("rk_debug create fbo success! fbo = %d\n", ectx->out_fbo_id);
    } else {
        printf("rk_debug create fbo failed!\n");
    }


    return 0;
}

void ectx_render(void *_ectx, int w, int h, int angle) {
    struct egl_ctx *ectx = (struct egl_ctx *)_ectx;
    if(setupGraphics(_ectx) == -1) {
        fprintf(stderr, "Could not set up graphics.\n");
        exit(0);
    }
    renderFrame(ectx, w, h, angle);
    glFinish();
}

void ectx_destory_window(void *_ectx) {
    struct egl_ctx *ectx = (struct egl_ctx *)_ectx;
    glDeleteTextures(1, &ectx->Gtexture);
    glDeleteTextures(1, &ectx->Otexture);
    eglDestroyContext(ectx->dpy, ectx->context);
    eglDestroySurface(ectx->dpy, ectx->surface);
    eglTerminate(ectx->dpy);
}

