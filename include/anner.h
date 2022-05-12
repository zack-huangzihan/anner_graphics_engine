int anner_init(void);
int anner_deinit(void);
int anner_create_window(int window_width, int window_height);
int anner_create_texture(unsigned char* pixels, int w, int h, int format);
int anner_delete_texture(void);
int anner_render(int w, int h);