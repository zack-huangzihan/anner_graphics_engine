#include  "anner_egl.h"
#include  <iostream>
#include  <cstdlib>
#include  <cstring>
#include  <cmath>

using namespace std; 

const char vertex_src [] =
      "#version 300 es                            \n"
      "layout(location = 0) in vec4 a_position;   \n"
      "layout(location = 1) in vec2 a_texCoord;   \n"
      "out vec2 v_texCoord;                       \n"
      "void main()                                \n"
      "{                                          \n"
      "   gl_Position = a_position;               \n"
      "   v_texCoord = a_texCoord;                \n"
      "}                                          \n";
 
 
const char fragment_src [] =
      "#version 300 es                                     \n"
      "precision mediump float;                            \n"
      "in vec2 v_texCoord;                                 \n"
      "layout(location = 0) out vec4 outColor;             \n"
      "uniform sampler2D s_texture;                        \n"
      "void main()                                         \n"
      "{                                                   \n"
      "  outColor = texture( s_texture, v_texCoord );      \n"
      "}                                                   \n";

EGLDisplay  egl_display;
EGLContext  egl_context;
EGLSurface  egl_surface;
GLuint 		vertexShader;
GLuint 		fragmentShader;
GLuint 		shaderProgram;
GLuint 		position_loc, textureId;


extern Window win;

static GLuint load_shader ( const char  *shader_source, GLenum type){
	GLuint  shader = glCreateShader( type );
	glShaderSource  ( shader , 1 , &shader_source , NULL );
	glCompileShader ( shader );
	return shader;
}

int egl_init_x11(void* display) {
	egl_display  =  eglGetDisplay( (EGLNativeDisplayType) display );
	if ( egl_display == EGL_NO_DISPLAY ) {
		cerr << "Got no EGL display." << endl;
		return -1;
	}
	if ( !eglInitialize( egl_display, NULL, NULL ) ) {
		cerr << "Unable to initialize EGL" << endl;
		return -1;
	}
	EGLint attr[] = {       // some attributes to set up our egl-interface
		EGL_BUFFER_SIZE, 16,
		EGL_RENDERABLE_TYPE,
		EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};
	EGLConfig  ecfg;
	EGLint     num_config;
	if ( !eglChooseConfig( egl_display, attr, &ecfg, 1, &num_config ) ) {
		cerr << "Failed to choose config (eglError: " << eglGetError() << ")" << endl;
		return -1;
	}
	if ( num_config != 1 ) {
		cerr << "Didn't get exactly one config, but " << num_config << endl;
		return -1;
	}
	egl_surface = eglCreateWindowSurface ( egl_display, ecfg, win, NULL );
	if ( egl_surface == EGL_NO_SURFACE ) {
		cerr << "Unable to create EGL surface (eglError: " << eglGetError() << ")" << endl;
		return -1;
	}
	// egl-contexts collect all state descriptions needed required for operation
	EGLint ctxattr[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	egl_context = eglCreateContext ( egl_display, ecfg, EGL_NO_CONTEXT, ctxattr );
	if ( egl_context == EGL_NO_CONTEXT ) {
		cerr << "Unable to create EGL context (eglError: " << eglGetError() << ")" << endl;
		return -1;
	}
	// associate the egl-context with the egl-surface
	eglMakeCurrent( egl_display, egl_surface, egl_surface, egl_context );
	vertexShader   = load_shader ( vertex_src ,GL_VERTEX_SHADER);     // load vertex shader
	fragmentShader = load_shader ( fragment_src ,GL_FRAGMENT_SHADER);  // load fragment shader
	
	shaderProgram  = glCreateProgram ();                 // create program object
	glAttachShader ( shaderProgram, vertexShader );             // and attach both...
	glAttachShader ( shaderProgram, fragmentShader );           // ... shaders to it
	glLinkProgram(shaderProgram); 
	glUseProgram(shaderProgram);
	return 0;
}

static GLuint CreateSimpleTexture2D(unsigned char* pixels, int w, int h, int format)
{
   // Texture object handle
   GLuint textureId;
   // int size_file = WINDOW_W * WINDOW_H * 4;
   // GLubyte pixels[WINDOW_W * WINDOW_H * 4];

   // GLubyte pixelss[4 * 3] =
   // {
   //    255,   0,   0, // Red
   //      0, 255,   0, // Green
   //      0,   0, 255, // Blue
   //    255, 255,   0  // Yellow
   // };

	// FILE *fp;
	// fp = fopen("/home/linaro/bgra-1920-1080.bin","rt");
	// printf("textures size:%d\n",size_file);
	// fread(pixels, 1, size_file, fp);
	// fclose(fp);

   // Use tightly packed data
   glPixelStorei ( GL_UNPACK_ALIGNMENT, 1 );

   // Generate a texture object
   glGenTextures ( 1, &textureId );
   
   // Bind the texture object
   glBindTexture ( GL_TEXTURE_2D, textureId );

   // Load the texture GL_BGRA_EXT 0x80E1
   glTexImage2D ( GL_TEXTURE_2D, 0, GL_BGRA_EXT, w, h, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, pixels);
   //glTexImage2D ( GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB, GL_UNSIGNED_BYTE, pixelss);


   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   
   return textureId;
}

int anner_create_texture(unsigned char* pixels, int w, int h, int format) {
	textureId 	  = CreateSimpleTexture2D(pixels, w, h, format);
	position_loc  = glGetUniformLocation  (shaderProgram, "s_texture" );
	return 0;
}

int anner_delete_texture() {
	glDeleteTextures(1, &textureId);
	return 0;
}

int anner_render(int w, int h) {
	//正常贴图
   	GLfloat vVertices[] = { -1.0f,  1.0f, 0.0f,  // Position 0
                            0.0f,  0.0f,        // TexCoord 0 
                           -1.0f, -1.0f, 0.0f,  // Position 1
                            0.0f,  1.0f,        // TexCoord 1
                            1.0f, -1.0f, 0.0f,  // Position 2
                            1.0f,  1.0f,        // TexCoord 2
                            1.0f,  1.0f, 0.0f,  // Position 3
                            1.0f,  0.0f         // TexCoord 3
                         };
    GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

	glViewport(0, 0, w, h);
	glClear ( GL_COLOR_BUFFER_BIT );
   	glVertexAttribPointer ( 0, 3, GL_FLOAT,
                           GL_FALSE, 5 * sizeof ( GLfloat ), vVertices );

   	glVertexAttribPointer ( 1, 2, GL_FLOAT,
                           GL_FALSE, 5 * sizeof ( GLfloat ), &vVertices[3] );
   	glEnableVertexAttribArray (0);
   	glEnableVertexAttribArray (1);
   	glActiveTexture ( GL_TEXTURE0 );
   	glBindTexture ( GL_TEXTURE_2D, textureId );
   	// Set the sampler texture unit to 0
   	glUniform1i (position_loc, 0);

   	glDrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices );
   	eglSwapBuffers ( egl_display, egl_surface );
   	return 0;
}

int egl_deinit_x11() {
	eglDestroyContext ( egl_display, egl_context );
	eglDestroySurface ( egl_display, egl_surface );
	eglTerminate      ( egl_display );
	return 0;
}