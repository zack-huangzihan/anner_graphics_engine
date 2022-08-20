#if 1
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <fcntl.h>
#include <poll.h>
#include <drm_mode.h>
#include <libdrm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "gbm_egl.h"

struct drm_bo {
    int fd;
    void *ptr;
    size_t size;
    size_t offset;
    size_t pitch;
    unsigned int handle;
    int fb_id;
    int buf_fd;
    int w;
    int h;
};

static int bo_map(int fd, struct drm_bo *bo)
{
    struct drm_mode_map_dumb arg = {
        .handle = bo->handle,
    };
    struct drm_prime_handle fd_args = {
        .fd = -1,
        .handle = bo->handle,
        .flags = 0,
    };
    int ret;

    ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &arg);
    if (ret)
        return ret;

    ret = drmIoctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &fd_args);
    if (ret)
    {
        printf("handle_to_fd failed ret=%d, handle=%x \n", ret ,fd_args.handle);
        return -1;
    }
    bo->buf_fd = fd_args.fd;

    bo->ptr = mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   fd, arg.offset);
    if (bo->ptr == MAP_FAILED) {
        bo->ptr = NULL;
        return -1;
    }

    return 0;
}

static void bo_unmap(int fd, struct drm_bo *bo)
{
    if (!bo->ptr)
        return;

    drmUnmap(bo->ptr, bo->size);
    bo->ptr = NULL;
}

void bo_destroy(int fd, struct drm_bo *bo)
{
    struct drm_mode_destroy_dumb arg = {
        .handle = bo->handle,
    };

    if (bo->fb_id)
        drmModeRmFB(fd, bo->fb_id);

    bo_unmap(fd, bo);

    if (bo->handle)
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &arg);

    free(bo);
}

static struct drm_bo *
bo_create(int fd, int width, int height, int format)
{
    struct drm_mode_create_dumb arg = {
        .bpp = 32,
        .width = width,
        .height = height,
    };
    struct drm_bo *bo;
    int ret;

    bo = malloc(sizeof(struct drm_bo));
    if (bo == NULL) {
        fprintf(stderr, "allocate bo failed\n");
        return NULL;
    }
    memset(bo, 0, sizeof(*bo));
    if (format == DRM_FORMAT_NV12) {
        arg.bpp = 8;
        arg.height = height * 3 / 2;
    }

    ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &arg);
    if (ret) {
        fprintf(stderr, "create dumb failed\n");
        goto err;
    }

    bo->fd = fd;
    bo->handle = arg.handle;
    bo->size = arg.size;
    bo->pitch = arg.pitch;
    bo->w = width;
    bo->h = height;

    ret = bo_map(fd, bo);
    if (ret) {
        fprintf(stderr, "map bo failed\n");
        goto err;
    }

    return bo;
err:
    bo_destroy(fd, bo);
    return NULL;
}


static int mdrm_init(void)
{
    int fd;

    fd = drmOpen(NULL, NULL);
    if (fd < 0)
        fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "drm open failed\n");
        return 0;
    }
    fcntl(fd, F_SETFD, FD_CLOEXEC);

    drmSetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);

    return fd;
}

int main(int argc, char *argv[])
{
    int drm_fd;
    struct drm_bo *in_bo;
    int w, h, ow, oh;
    int angle;
    void *ectx;
    void *ptr;
    FILE *fd;

    w = atoi(argv[3]);
    h = atoi(argv[4]);
    ow = atoi(argv[5]);
    oh = atoi(argv[6]);
    angle = atoi(argv[7]);

    drm_fd = mdrm_init();
    in_bo = bo_create(drm_fd, w, h, DRM_FORMAT_NV12);

    fd = fopen(argv[1],"rb");
    if (!fd)
        return -1;
    if (fread(in_bo->ptr, 1, in_bo->size, fd) <= 0)
        return -1;
    fclose(fd);

    ectx = egl_init_ctx(drm_fd, ow, oh, DRM_FORMAT_XRGB8888, angle);
    ptr = egl_convert_fb(ectx, in_bo->buf_fd, w, h,
                         in_bo->pitch, DRM_FORMAT_NV12);

    printf("out %p %d %d\n", ptr, ow, oh);
    fd = fopen(argv[2], "wb+");
    if (!fd)
        return -1;
    if (fwrite(ptr, ow * oh * 4, 1, fd) <= 0)
        return -1;
    fclose(fd);
    egl_free_ctx(ectx);

    return 0;
}
#endif
