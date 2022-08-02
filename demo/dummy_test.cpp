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
	void* out_pixels = NULL;
    //pPixelDataFront = (unsigned char*)malloc(1280*720*4);
	int drm_fd = -1;
	int out_drm_fd = -1;
	anner_create_window(720,1280);
	anner_create_intput(&pixels, &drm_fd, 1280, 720, DRM_FORMAT_ABGR8888);
	anner_create_output(&out_pixels, &out_drm_fd, 720, 1280, DRM_FORMAT_ABGR8888);
	printf("huangzihan   test\n");
	fp = fopen("/userdata/bgra-1280-720.bin","rb");
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
	//anner_dumpPixels(1280*720*4, 720, 1280, pPixelDataFront, "/userdata/dumplayer_out.bin" );
	//free(pPixelDataFront);
	const char* file_name = "/userdata/dumplayer_out.bin";
    FILE *file = fopen(file_name, "wb+");
    if (!file)
    {
        printf("Could not open /%s \n",file_name);
        return -1;
    } else {
        printf("open %s and write ok\n",file_name);
    }
    fwrite(out_pixels, 1280*720*4, 1, file);
    fclose(file);
	anner_delete_buf(pixels, drm_fd, 1280*720*4, 0);
	anner_delete_buf(out_pixels, out_drm_fd, 1280*720*4, 1);
	anner_destory_window();
	printf("anner test end\n");
	return 0;
}
