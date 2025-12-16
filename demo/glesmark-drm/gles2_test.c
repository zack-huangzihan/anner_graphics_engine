#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <math.h>
#include <getopt.h>
#include <signal.h>

#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>

// 如果DRM_CAP_ATOMIC未定义，手动定义
#ifndef DRM_CAP_ATOMIC
#define DRM_CAP_ATOMIC 0x15
#endif

// 全局变量
static int target_fps = 60;
static int show_fps = 0;
static int running = 1;

// 设备句柄
static int drm_device;
static drmModeModeInfo mode;
static struct gbm_device *gbm_device;
static struct gbm_surface *gbm_surface;
static drmModeCrtc *saved_crtc;
static uint32_t connector_id;

// EGL相关
static EGLDisplay egl_display;
static EGLContext egl_context;
static EGLSurface egl_surface;

// GBM缓冲区管理
static struct gbm_bo *current_bo = NULL;
static uint32_t current_fb;

// GLES2相关
static GLuint program;
static GLuint vbo;
static GLint position_attr;
static GLint rotation_uniform;
static GLint color_uniform;

// 原子提交相关
static drmModeAtomicReq *atomic_req = NULL;
static uint32_t mode_blob_id = 0;
static uint32_t crtc_id;
static int custom_width = 0;
static int custom_height = 0;
static int use_atomic = 1;

// 平面相关
static drmModePlaneResPtr plane_resources = NULL;
static uint32_t plane_id = 0;
static uint32_t custom_plane_id = 0;

// 旋转角度
static float rotation_angle = 0.0f;

// 顶点着色器源码
static const char *vertex_shader_code = 
    "attribute vec3 position;"
    "uniform mat4 rotation;"
    "void main() {"
    "    gl_Position = rotation * vec4(position, 1.0);"
    "}";

// 片段着色器源码
static const char *fragment_shader_code = 
    "precision mediump float;"
    "uniform vec4 color;"
    "void main() {"
    "    gl_FragColor = color;"
    "}";

// 三角形顶点数据
static const float vertices[] = {
    -0.5f, -0.5f, 0.0f,  // 左下角
     0.5f, -0.5f, 0.0f,  // 右下角
     0.0f,  0.5f, 0.0f   // 顶部
};

// 打印使用说明
static void print_usage(const char *prog_name) {
    printf("Usage: %s [OPTIONS]\n", prog_name);
    printf("Options:\n");
    printf("  -f, --fps FPS        Set target frames per second (default: 60)\n");
    printf("  -s, --show-fps       Show FPS counter\n");
    printf("  -w, --width WIDTH    Set custom width (default: auto)\n");
    printf("  -h, --height HEIGHT  Set custom height (default: auto)\n");
    printf("  -p, --plane PLANE_ID Specify plane ID to use (default: auto)\n");  // 新增
    printf("  -H, --help           Show this help message\n");
    printf("\nExamples:\n");
    printf("  %s                    # Run at 60 FPS with auto resolution\n", prog_name);
    printf("  %s -f 30              # Run at 30 FPS\n", prog_name);
    printf("  %s -w 1920 -h 1080    # Run at 1920x1080 resolution\n", prog_name);
    printf("  %s -f 120 -s -w 1280 -h 720  # 720p at 120FPS with FPS counter\n", prog_name);
    printf("  %s -p 100             # Use specific plane ID 100\n", prog_name);  // 新增
}

// 解析命令行参数
static void parse_arguments(int argc, char *argv[]) {
    static struct option long_options[] = {
        {"fps", required_argument, 0, 'f'},
        {"show-fps", no_argument, 0, 's'},
        {"width", required_argument, 0, 'w'},
        {"height", required_argument, 0, 'h'},
        {"plane", required_argument, 0, 'p'},  // 新增
        {"help", no_argument, 0, 'H'},
        {0, 0, 0, 0}
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "f:sw:h:p:H", long_options, NULL)) != -1) {  // 修改了选项字符串
        switch (opt) {
            case 'f':
                target_fps = atoi(optarg);
                if (target_fps < 1 || target_fps > 240) {
                    fprintf(stderr, "Warning: FPS should be between 1 and 240, using default 60\n");
                    target_fps = 60;
                }
                break;
            case 's':
                show_fps = 1;
                break;
            case 'w':
                custom_width = atoi(optarg);
                if (custom_width < 1 || custom_width > 7680) {
                    fprintf(stderr, "Warning: Width should be between 1 and 7680, using auto\n");
                    custom_width = 0;
                }
                break;
            case 'h':
                custom_height = atoi(optarg);
                if (custom_height < 1 || custom_height > 4320) {
                    fprintf(stderr, "Warning: Height should be between 1 and 4320, using auto\n");
                    custom_height = 0;
                }
                break;
            case 'p':  // 新增：处理平面ID参数
                custom_plane_id = atoi(optarg);
                printf("User specified plane ID: %d\n", custom_plane_id);
                break;
            case 'H':
                print_usage(argv[0]);
                exit(0);
            default:
                print_usage(argv[0]);
                exit(1);
        }
    }
}

static uint32_t get_property_id(int fd, uint32_t obj_id, uint32_t obj_type, const char *prop_name) {
    drmModeObjectProperties *props = drmModeObjectGetProperties(fd, obj_id, obj_type);
    if (!props) {
        fprintf(stderr, "Failed to get properties for object %d type %d\n", obj_id, obj_type);
        return 0;
    }
    
    uint32_t prop_id = 0;
    for (int i = 0; i < props->count_props; i++) {
        drmModePropertyPtr prop = drmModeGetProperty(fd, props->props[i]);
        if (prop) {
            if (strcmp(prop->name, prop_name) == 0) {
                prop_id = prop->prop_id;
                drmModeFreeProperty(prop);
                break;
            }
            drmModeFreeProperty(prop);
        }
    }
    
    drmModeFreeObjectProperties(props);
    return prop_id;
}

// 查找CRTC索引的辅助函数
static int find_crtc_index(drmModeRes *resources, uint32_t crtc_id) {
    for (int i = 0; i < resources->count_crtcs; i++) {
        if (resources->crtcs[i] == crtc_id) {
            return i;
        }
    }
    return -1;
}

static uint32_t find_available_plane(int fd, drmModeRes *resources, uint32_t crtc_id) {
    drmModePlaneResPtr plane_res = drmModeGetPlaneResources(fd);
    if (!plane_res) {
        fprintf(stderr, "Failed to get plane resources\n");
        return 0;
    }
    
    uint32_t plane_id = 0;
    int crtc_idx = find_crtc_index(resources, crtc_id);
    
    if (crtc_idx < 0) {
        fprintf(stderr, "Failed to find CRTC index\n");
        drmModeFreePlaneResources(plane_res);
        return 0;
    }
    
    printf("\n=== Available Planes Information ===\n");
    printf("Total planes found: %d\n", plane_res->count_planes);
    
    // 如果用户指定了平面ID，先检查该平面是否可用
    if (custom_plane_id > 0) {
        printf("Checking user specified plane ID: %d\n", custom_plane_id);
        
        for (uint32_t i = 0; i < plane_res->count_planes; i++) {
            if (plane_res->planes[i] == custom_plane_id) {
                drmModePlanePtr plane = drmModeGetPlane(fd, custom_plane_id);
                if (plane) {
                    // 检查平面是否支持当前CRTC
                    if (plane->possible_crtcs & (1 << crtc_idx)) {
                        // 检查平面格式支持
                        for (uint32_t j = 0; j < plane->count_formats; j++) {
                            if (plane->formats[j] == DRM_FORMAT_XRGB8888 || 
                                plane->formats[j] == DRM_FORMAT_ARGB8888) {
                                // 检查平面类型
                                drmModeObjectProperties *props = drmModeObjectGetProperties(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE);
                                if (props) {
                                    for (int k = 0; k < props->count_props; k++) {
                                        drmModePropertyPtr prop = drmModeGetProperty(fd, props->props[k]);
                                        if (prop && strcmp(prop->name, "type") == 0) {
                                            uint64_t value = props->prop_values[k];
                                            const char* type_str = "unknown";
                                            if (value == DRM_PLANE_TYPE_PRIMARY) type_str = "primary";
                                            else if (value == DRM_PLANE_TYPE_OVERLAY) type_str = "overlay";
                                            else if (value == DRM_PLANE_TYPE_CURSOR) type_str = "cursor";
                                            
                                            printf("✓ User specified plane %d is available (type: %s)\n", custom_plane_id, type_str);
                                            plane_id = custom_plane_id;
                                            
                                            drmModeFreeProperty(prop);
                                            drmModeFreeObjectProperties(props);
                                            drmModeFreePlane(plane);
                                            drmModeFreePlaneResources(plane_res);
                                            return plane_id;
                                        }
                                        if (prop) drmModeFreeProperty(prop);
                                    }
                                    drmModeFreeObjectProperties(props);
                                }
                            }
                        }
                    }
                    drmModeFreePlane(plane);
                }
                break;
            }
        }
        
        printf("✗ User specified plane %d is not available or not compatible\n", custom_plane_id);
    }
    
    // 遍历所有平面并打印信息
    for (uint32_t i = 0; i < plane_res->count_planes; i++) {
        drmModePlanePtr plane = drmModeGetPlane(fd, plane_res->planes[i]);
        if (!plane) continue;
        
        const char* supported = (plane->possible_crtcs & (1 << crtc_idx)) ? "YES" : "NO";
        
        // 检查格式支持
        int format_supported = 0;
        for (uint32_t j = 0; j < plane->count_formats; j++) {
            if (plane->formats[j] == DRM_FORMAT_XRGB8888 || 
                plane->formats[j] == DRM_FORMAT_ARGB8888) {
                format_supported = 1;
                break;
            }
        }
        
        // 获取平面类型
        const char* type_str = "unknown";
        drmModeObjectProperties *props = drmModeObjectGetProperties(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE);
        if (props) {
            for (int k = 0; k < props->count_props; k++) {
                drmModePropertyPtr prop = drmModeGetProperty(fd, props->props[k]);
                if (prop && strcmp(prop->name, "type") == 0) {
                    uint64_t value = props->prop_values[k];
                    if (value == DRM_PLANE_TYPE_PRIMARY) type_str = "primary";
                    else if (value == DRM_PLANE_TYPE_OVERLAY) type_str = "overlay";
                    else if (value == DRM_PLANE_TYPE_CURSOR) type_str = "cursor";
                    break;
                }
                if (prop) drmModeFreeProperty(prop);
            }
            drmModeFreeObjectProperties(props);
        }
        
        printf("Plane %d: type=%s, supports CRTC=%s, format=%s\n", 
               plane->plane_id, type_str, supported, 
               format_supported ? "XRGB/ARGB" : "unsupported");
        
        // 如果这是第一个可用的平面，且用户没有指定平面ID，则选择它
        if (!plane_id && supported[0] == 'Y' && format_supported && 
            (strcmp(type_str, "overlay") == 0 || strcmp(type_str, "cursor") == 0)) {
            plane_id = plane->plane_id;
            printf("  → Selected this plane (first available)\n");
        }
        
        drmModeFreePlane(plane);
    }
    
    printf("====================================\n\n");
    
    if (plane_id) {
        printf("Using plane ID: %d\n", plane_id);
    } else {
        printf("No suitable plane found!\n");
    }
    
    drmModeFreePlaneResources(plane_res);
    return plane_id;
}

// 创建模式blob
static int create_mode_blob(drmModeModeInfo *mode, uint32_t *blob_id) {
    int ret = drmModeCreatePropertyBlob(drm_device, mode, sizeof(*mode), blob_id);
    if (ret) {
        fprintf(stderr, "Failed to create mode property blob: %s\n", strerror(-ret));
    }
    return ret;
}

// 原子提交设置平面（参考drm_cursor.c的实现）
static int atomic_set_plane(uint32_t fb_id) {
    if (!use_atomic) {
        // 回退到传统模式
        return drmModeSetPlane(drm_device, plane_id, crtc_id, fb_id, 0,
                               0, 0, mode.hdisplay, mode.vdisplay,
                               0, 0, mode.hdisplay << 16, mode.vdisplay << 16);
    }
    
    drmModeAtomicReq *req = drmModeAtomicAlloc();
    if (!req) {
        fprintf(stderr, "Failed to allocate atomic request\n");
        return -1;
    }
    
    int ret = 0;
    
    // 获取属性ID
    uint32_t crtc_active_id = get_property_id(drm_device, crtc_id, DRM_MODE_OBJECT_CRTC, "ACTIVE");
    uint32_t crtc_mode_id = get_property_id(drm_device, crtc_id, DRM_MODE_OBJECT_CRTC, "MODE_ID");
    uint32_t plane_fb_id = get_property_id(drm_device, plane_id, DRM_MODE_OBJECT_PLANE, "FB_ID");
    uint32_t plane_crtc_id = get_property_id(drm_device, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_ID");
    uint32_t plane_src_x = get_property_id(drm_device, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_X");
    uint32_t plane_src_y = get_property_id(drm_device, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_Y");
    uint32_t plane_src_w = get_property_id(drm_device, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_W");
    uint32_t plane_src_h = get_property_id(drm_device, plane_id, DRM_MODE_OBJECT_PLANE, "SRC_H");
    uint32_t plane_crtc_x = get_property_id(drm_device, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_X");
    uint32_t plane_crtc_y = get_property_id(drm_device, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_Y");
    uint32_t plane_crtc_w = get_property_id(drm_device, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_W");
    uint32_t plane_crtc_h = get_property_id(drm_device, plane_id, DRM_MODE_OBJECT_PLANE, "CRTC_H");
    
    if (!crtc_active_id || !crtc_mode_id || !plane_fb_id || !plane_crtc_id) {
        fprintf(stderr, "Failed to get required property IDs\n");
        ret = -1;
        goto cleanup;
    }
    
    // 创建模式blob（如果还没有）
    if (!mode_blob_id) {
        if (create_mode_blob(&mode, &mode_blob_id) != 0) {
            ret = -1;
            goto cleanup;
        }
    }
    
    // 添加CRTC属性
    ret = drmModeAtomicAddProperty(req, crtc_id, crtc_active_id, 1);
    if (ret < 0) goto property_error;
    
    ret = drmModeAtomicAddProperty(req, crtc_id, crtc_mode_id, mode_blob_id);
    if (ret < 0) goto property_error;
    
    // 添加平面属性
    ret = drmModeAtomicAddProperty(req, plane_id, plane_fb_id, fb_id);
    if (ret < 0) goto property_error;
    
    ret = drmModeAtomicAddProperty(req, plane_id, plane_crtc_id, crtc_id);
    if (ret < 0) goto property_error;
    
    ret = drmModeAtomicAddProperty(req, plane_id, plane_src_x, 0);
    if (ret < 0) goto property_error;
    
    ret = drmModeAtomicAddProperty(req, plane_id, plane_src_y, 0);
    if (ret < 0) goto property_error;
    
    ret = drmModeAtomicAddProperty(req, plane_id, plane_src_w, mode.hdisplay << 16);
    if (ret < 0) goto property_error;
    
    ret = drmModeAtomicAddProperty(req, plane_id, plane_src_h, mode.vdisplay << 16);
    if (ret < 0) goto property_error;
    
    ret = drmModeAtomicAddProperty(req, plane_id, plane_crtc_x, 0);
    if (ret < 0) goto property_error;
    
    ret = drmModeAtomicAddProperty(req, plane_id, plane_crtc_y, 0);
    if (ret < 0) goto property_error;
    
    ret = drmModeAtomicAddProperty(req, plane_id, plane_crtc_w, mode.hdisplay);
    if (ret < 0) goto property_error;
    
    ret = drmModeAtomicAddProperty(req, plane_id, plane_crtc_h, mode.vdisplay);
    if (ret < 0) goto property_error;
    
    // 提交原子请求
    ret = drmModeAtomicCommit(drm_device, req, DRM_MODE_ATOMIC_NONBLOCK, NULL);
    if (ret) {
        fprintf(stderr, "Atomic commit failed: %s\n", strerror(-ret));
        // 原子提交失败时回退到传统模式
        use_atomic = 0;
        fprintf(stderr, "Falling back to legacy mode\n");
        drmModeAtomicFree(req);
        return drmModeSetPlane(drm_device, plane_id, crtc_id, fb_id, 0,
                               0, 0, mode.hdisplay, mode.vdisplay,
                               0, 0, mode.hdisplay << 16, mode.vdisplay << 16);
    }
    
    drmModeAtomicFree(req);
    return 0;
    
property_error:
    fprintf(stderr, "Failed to add property to atomic request\n");
cleanup:
    drmModeAtomicFree(req);
    return -1;
}

// 初始化DRM和显示
static int init_drm() {
    drm_device = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (drm_device < 0) {
        perror("Cannot open DRM device");
        return -1;
    }

        
    // 启用原子客户端能力
    if (drmSetClientCap(drm_device, DRM_CLIENT_CAP_ATOMIC, 1) < 0) {
        fprintf(stderr, "Failed to set atomic client capability, falling back to legacy mode\n");
        use_atomic = 0;
    }else {
            printf("DRM atomic modesetting supported\n");
            use_atomic = 1;
    }

    drmModeRes *resources = drmModeGetResources(drm_device);
    if (!resources) {
        fprintf(stderr, "Unable to get DRM resources\n");
        close(drm_device);
        return -1;
    }

    // 查找连接的显示器
    drmModeConnector *connector = NULL;
    for (int i = 0; i < resources->count_connectors; i++) {
        connector = drmModeGetConnector(drm_device, resources->connectors[i]);
        if (connector && connector->connection == DRM_MODE_CONNECTED && connector->count_modes > 0) {
            break;
        }
        if (connector) {
            drmModeFreeConnector(connector);
            connector = NULL;
        }
    }

    if (!connector) {
        fprintf(stderr, "Unable to get connector\n");
        drmModeFreeResources(resources);
        close(drm_device);
        return -1;
    }

    connector_id = connector->connector_id;
    
    // 设置分辨率
    if (custom_width > 0 && custom_height > 0) {
        // 使用自定义分辨率
        memset(&mode, 0, sizeof(mode));
        mode.hdisplay = custom_width;
        mode.vdisplay = custom_height;
        mode.clock = custom_width * custom_height * 60 / 1000;
        mode.hsync_start = custom_width + 48;
        mode.hsync_end = custom_width + 48 + 32;
        mode.htotal = custom_width + 48 + 32 + 80;
        mode.vsync_start = custom_height + 3;
        mode.vsync_end = custom_height + 3 + 5;
        mode.vtotal = custom_height + 3 + 5 + 24;
        mode.vrefresh = 60;
        mode.type = DRM_MODE_TYPE_USERDEF;
        strcpy(mode.name, "Custom");
        printf("Using custom resolution: %dx%d@%dHz\n", mode.hdisplay, mode.vdisplay, mode.vrefresh);
    } else {
        // 使用显示器默认模式
        mode = connector->modes[0];
        printf("Using display resolution: %dx%d@%dHz\n", mode.hdisplay, mode.vdisplay, mode.vrefresh);
    }

    // 查找编码器
    drmModeEncoder *encoder = NULL;
    if (connector->encoder_id) {
        encoder = drmModeGetEncoder(drm_device, connector->encoder_id);
    }

    if (!encoder) {
        fprintf(stderr, "Unable to get encoder\n");
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(drm_device);
        return -1;
    }

    crtc_id = encoder->crtc_id;
    if (!crtc_id && resources->count_crtcs > 0) {
        crtc_id = resources->crtcs[0];
    }

    saved_crtc = drmModeGetCrtc(drm_device, crtc_id);
    
    // 查找可用的平面
    plane_id = find_available_plane(drm_device, resources, crtc_id);
    if (!plane_id) {
        fprintf(stderr, "No available plane found\n");
        drmModeFreeEncoder(encoder);
        drmModeFreeConnector(connector);
        drmModeFreeResources(resources);
        close(drm_device);
        return -1;
    }
    
    drmModeFreeEncoder(encoder);
    drmModeFreeConnector(connector);
    drmModeFreeResources(resources);

    if (!crtc_id) {
        fprintf(stderr, "No available CRTC found\n");
        close(drm_device);
        return -1;
    }

    printf("Using CRTC: %d, Plane: %d\n", crtc_id, plane_id);
    return 0;
}

// 初始化GBM
static int init_gbm() {
    gbm_device = gbm_create_device(drm_device);
    if (!gbm_device) {
        fprintf(stderr, "Failed to create GBM device\n");
        return -1;
    }

    gbm_surface = gbm_surface_create(gbm_device, mode.hdisplay, mode.vdisplay, 
                                     GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (!gbm_surface) {
        fprintf(stderr, "Failed to create GBM surface\n");
        return -1;
    }

    return 0;
}

// 配置匹配
static int match_config_to_visual(EGLint visual_id, EGLConfig *configs, int count) {
    EGLint id;
    for (int i = 0; i < count; i++) {
        if (!eglGetConfigAttrib(egl_display, configs[i], EGL_NATIVE_VISUAL_ID, &id)) {
            continue;
        }
        if (id == visual_id) {
            return i;
        }
    }
    return -1;
}

// 初始化EGL
static int init_egl() {
    static const EGLint config_attribs[] = {
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_DEPTH_SIZE, 8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    static const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2,
        EGL_NONE
    };

    egl_display = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm_device, NULL);
    if (egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "Failed to get EGL display\n");
        return -1;
    }

    EGLint major, minor;
    if (!eglInitialize(egl_display, &major, &minor)) {
        fprintf(stderr, "Failed to initialize EGL\n");
        return -1;
    }
    printf("Initialized EGL version: %d.%d\n", major, minor);

    EGLint num_configs;
    EGLConfig *configs = NULL;
    EGLint count;
    
    eglGetConfigs(egl_display, NULL, 0, &count);
    configs = (EGLConfig*)malloc(count * sizeof(EGLConfig));
    
    if (!eglChooseConfig(egl_display, config_attribs, configs, count, &num_configs)) {
        fprintf(stderr, "Failed to get EGL configs\n");
        free(configs);
        return -1;
    }

    int config_index = match_config_to_visual(GBM_FORMAT_XRGB8888, configs, num_configs);
    if (config_index < 0) {
        fprintf(stderr, "Failed to find matching EGL config\n");
        free(configs);
        return -1;
    }

    egl_context = eglCreateContext(egl_display, configs[config_index], EGL_NO_CONTEXT, context_attribs);
    if (egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "Failed to create EGL context\n");
        free(configs);
        return -1;
    }

    egl_surface = eglCreateWindowSurface(egl_display, configs[config_index], gbm_surface, NULL);
    if (egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "Failed to create EGL surface\n");
        free(configs);
        return -1;
    }

    free(configs);

    if (!eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_context)) {
        fprintf(stderr, "Failed to make EGL context current\n");
        return -1;
    }

    return 0;
}

// 编译着色器
static GLuint compile_shader(GLenum type, const char *source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);
    
    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        GLchar info_log[512];
        glGetShaderInfoLog(shader, 512, NULL, info_log);
        fprintf(stderr, "Shader compilation error: %s\n", info_log);
        return 0;
    }
    return shader;
}

// 创建着色器程序
static int create_program() {
    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_shader_code);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_shader_code);
    
    if (!vertex_shader || !fragment_shader) {
        return -1;
    }
    
    program = glCreateProgram();
    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);
    
    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        GLchar info_log[512];
        glGetProgramInfoLog(program, 512, NULL, info_log);
        fprintf(stderr, "Program linking error: %s\n", info_log);
        return -1;
    }
    
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);
    
    return 0;
}

// 初始化GLES2
static int init_gles() {
    if (create_program() != 0) {
        return -1;
    }
    
    glUseProgram(program);
    
    position_attr = glGetAttribLocation(program, "position");
    rotation_uniform = glGetUniformLocation(program, "rotation");
    color_uniform = glGetUniformLocation(program, "color");
    
    if (position_attr == -1 || rotation_uniform == -1 || color_uniform == -1) {
        fprintf(stderr, "Failed to get attribute/uniform locations\n");
        return -1;
    }
    
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    
    glEnableVertexAttribArray(position_attr);
    glVertexAttribPointer(position_attr, 3, GL_FLOAT, GL_FALSE, 0, 0);
    
    glViewport(0, 0, mode.hdisplay, mode.vdisplay);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    
    printf("OpenGL ES initialized\n");
    return 0;
}

// GBM交换缓冲区（使用原子提交）
static void gbm_swap_buffers() {
    eglSwapBuffers(egl_display, egl_surface);
    
    // 锁定前端缓冲区
    struct gbm_bo *bo = gbm_surface_lock_front_buffer(gbm_surface);
    if (!bo) {
        fprintf(stderr, "Failed to lock front buffer\n");
        return;
    }
    
    uint32_t handle = gbm_bo_get_handle(bo).u32;
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t width = gbm_bo_get_width(bo);
    uint32_t height = gbm_bo_get_height(bo);
    
    // 创建framebuffer
    uint32_t fb;
    int ret = drmModeAddFB(drm_device, width, height, 24, 32, stride, handle, &fb);
    if (ret) {
        fprintf(stderr, "Failed to add DRM framebuffer: %s\n", strerror(-ret));
        gbm_surface_release_buffer(gbm_surface, bo);
        return;
    }
    
    // 使用原子提交设置平面
    if (atomic_set_plane(fb) != 0) {
        fprintf(stderr, "Failed to set plane\n");
        drmModeRmFB(drm_device, fb);
        gbm_surface_release_buffer(gbm_surface, bo);
        return;
    }
    
    // 释放之前的缓冲区
    if (current_bo) {
        drmModeRmFB(drm_device, current_fb);
        gbm_surface_release_buffer(gbm_surface, current_bo);
    }
    
    current_bo = bo;
    current_fb = fb;
}

// 渲染帧
static void render_frame() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    float angle = rotation_angle;
    float c = cosf(angle);
    float s = sinf(angle);
    
    float rotation_matrix[16] = {
        c, -s, 0, 0,
        s,  c, 0, 0,
        0,  0, 1, 0,
        0,  0, 0, 1
    };
    
    glUniformMatrix4fv(rotation_uniform, 1, GL_FALSE, rotation_matrix);
    
    float r = (sinf(angle) + 1.0f) * 0.5f;
    float g = (cosf(angle * 0.7f) + 1.0f) * 0.5f;
    float b = (sinf(angle * 1.3f) + 1.0f) * 0.5f;
    glUniform4f(color_uniform, r, g, b, 1.0f);
    
    glDrawArrays(GL_TRIANGLES, 0, 3);
    
    gbm_swap_buffers();
}

// 信号处理
static void signal_handler(int sig) {
    running = 0;
}

// 主循环
static void main_loop() {
    struct timespec last_time, current_time;
    int frame_count = 0;
    float total_time = 0.0f;
    
    const float frame_duration = 1.0f / target_fps;
    const float rotation_speed = M_PI;
    
    clock_gettime(CLOCK_MONOTONIC, &last_time);
    
    printf("Starting rendering loop at %d FPS\n", target_fps);
    printf("Using %s mode\n", use_atomic ? "atomic" : "legacy");
    printf("Press Ctrl+C to exit\n");
    
    while (running) {
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        
        float delta_time = (current_time.tv_sec - last_time.tv_sec) +
                          (current_time.tv_nsec - last_time.tv_nsec) / 1e9f;
        
        if (delta_time >= frame_duration) {
            rotation_angle += delta_time * rotation_speed;
            if (rotation_angle > 2 * M_PI) {
                rotation_angle -= 2 * M_PI;
            }
            
            render_frame();
            
            frame_count++;
            total_time += delta_time;
            
            if (show_fps && total_time >= 1.0f) {
                float fps = frame_count / total_time;
                printf("FPS: %.1f\n", fps);
                frame_count = 0;
                total_time = 0.0f;
            }
            
            last_time = current_time;
        } else {
            usleep(1000);
        }
    }
}

// 清理资源
static void cleanup() {
    printf("Cleaning up resources...\n");
    
    // 禁用平面
    if (use_atomic) {
        atomic_set_plane(0);
    } else {
        drmModeSetPlane(drm_device, plane_id, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    }
    
    // 恢复原始CRTC设置
    if (saved_crtc) {
        drmModeSetCrtc(drm_device, saved_crtc->crtc_id, saved_crtc->buffer_id,
                      saved_crtc->x, saved_crtc->y, &connector_id, 1, &saved_crtc->mode);
    }
    
    // 清理原子提交相关资源
    if (atomic_req) {
        drmModeAtomicFree(atomic_req);
    }
    if (mode_blob_id) {
        drmModeDestroyPropertyBlob(drm_device, mode_blob_id);
    }
    
    // 清理framebuffer
    if (current_bo) {
        drmModeRmFB(drm_device, current_fb);
        gbm_surface_release_buffer(gbm_surface, current_bo);
    }
    
    // 清理GLES2资源
    if (program) {
        glDeleteProgram(program);
    }
    if (vbo) {
        glDeleteBuffers(1, &vbo);
    }
    
    // 清理EGL资源
    if (egl_context != EGL_NO_CONTEXT) {
        eglDestroyContext(egl_display, egl_context);
    }
    if (egl_surface != EGL_NO_SURFACE) {
        eglDestroySurface(egl_display, egl_surface);
    }
    if (egl_display != EGL_NO_DISPLAY) {
        eglTerminate(egl_display);
    }
    
    // 清理GBM资源
    if (gbm_surface) {
        gbm_surface_destroy(gbm_surface);
    }
    if (gbm_device) {
        gbm_device_destroy(gbm_device);
    }
    
    // 恢复原始CRTC设置
    if (saved_crtc) {
        drmModeFreeCrtc(saved_crtc);
    }
    
    // 关闭DRM设备
    if (drm_device >= 0) {
        close(drm_device);
    }
}

int main(int argc, char *argv[]) {
    parse_arguments(argc, argv);
    
    printf("DRM/GBM GLES2 Rotating Triangle\n");
    printf("Target FPS: %d\n", target_fps);
    
    signal(SIGINT, signal_handler);
    
    if (init_drm() != 0) {
        fprintf(stderr, "DRM initialization failed\n");
        return -1;
    }
    
    if (init_gbm() != 0) {
        fprintf(stderr, "GBM initialization failed\n");
        cleanup();
        return -1;
    }
    
    if (init_egl() != 0) {
        fprintf(stderr, "EGL initialization failed\n");
        cleanup();
        return -1;
    }
    
    if (init_gles() != 0) {
        fprintf(stderr, "GLES2 initialization failed\n");
        cleanup();
        return -1;
    }
    
    printf("Rendering rotating triangle at %dx%d resolution\n", mode.hdisplay, mode.vdisplay);
    
    main_loop();
    
    cleanup();
    
    printf("Program exited successfully\n");
    return 0;
}