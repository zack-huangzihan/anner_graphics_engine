#include <iostream>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <unistd.h> 
#include <sys/time.h>

#include "anner.h" 

int main() {
	printf("anner test begin\n");
	FILE *fp;
	int size_file = 1280*720*4;
	unsigned char pixels[1280*720*4];
	fp = fopen("/home/rockchip/bgra-1280-720.bin","rt");
	printf("textures size:%d\n",size_file);
	fread(pixels, 1, size_file, fp);
	fclose(fp);
	anner_create_window(1280,720);
	bool quit = false;
	while ( !quit ) {
		anner_create_texture(pixels, 1280, 720, 0x80E1);
		anner_render(1280, 720);
		anner_delete_texture();
	}
	anner_destory_window();
	printf("anner test end\n");
	return 0;
}