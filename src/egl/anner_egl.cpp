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

EGLDisplay  	egl_display;
EGLContext  	egl_context;
EGLSurface  	egl_surface;
EGLConfig         ecfg;

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

void shader_init() {

	vertexShader   = load_shader ( vertex_src ,GL_VERTEX_SHADER);     // load vertex shader
	fragmentShader = load_shader ( fragment_src ,GL_FRAGMENT_SHADER);  // load fragment shader
	
	shaderProgram  = glCreateProgram ();                 // create program object
	glAttachShader ( shaderProgram, vertexShader );             // and attach both...
	glAttachShader ( shaderProgram, fragmentShader );           // ... shaders to it

	glLinkProgram(shaderProgram); 
	glUseProgram(shaderProgram);
}

static GLuint CreateSimpleTexture2D(unsigned char* pixels, int w, int h, int format)
{
   // Texture object handle
   GLuint textureId;

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

int egl_render(int w, int h) {

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