#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unistd.h> 
#include <sys/time.h>
#include <libdrm/drm_fourcc.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "anner.h" 

int main() {
	printf("dummy anner test begin\n");
	FILE *fp;
	int size_file = 1280*720*4;
	void* pixels = NULL;
    unsigned char * pPixelDataFront = NULL;
    pPixelDataFront = (unsigned char*)malloc(1280*720*4);
	int drm_fd = -1;
	anner_create_window(720,1280);
	anner_create_texture(&pixels, &drm_fd, 1280, 720, DRM_FORMAT_ABGR8888);
	fp = fopen("/home/rockchip/bgra-1280-720.bin","rb");
	if(fp){
		printf("textures size:%d\n",size_file);
		fread((void *)pixels, 1, size_file, fp);
		fclose(fp);
	}else {
		printf("open file is err\n");
		return -1;
	}
	int i = 0;
	while (i<100) {
		printf("render....\n");
		anner_activation_texture(pixels, drm_fd, 1280, 720, DRM_FORMAT_ABGR8888);
		anner_set_effects(90);
		anner_render(720, 1280);
		anner_disable_texture();
		i++;
	}
	anner_dumpPixels(1280*720*4, 720, 1280, pPixelDataFront, "/home/rockchip/dumplayer_out.bin" );
	free(pPixelDataFront);
	anner_delete_texture(pixels, drm_fd, 1280*720*4);
	anner_destory_window();
	printf("anner test end\n");
	return 0;
}