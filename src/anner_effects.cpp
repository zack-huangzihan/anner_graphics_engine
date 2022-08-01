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

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
void matrix_rotation(GLfloat *data, uint32_t angle)
{
    GLfloat ret = 0;
    switch (angle) {
        
        case 90:            
            ret = data[0]; data[0] = data[15]; data[15]=ret;
            ret = data[1]; data[1] = data[16]; data[16]=ret;
            ret = data[0]; data[0] = data[10]; data[10]=ret;
            ret = data[1]; data[1] = data[11]; data[11]=ret;
            ret = data[0]; data[0] = data[5]; data[5]=ret;
            ret = data[1]; data[1] = data[6]; data[6]=ret;
         case 180:
            ret = data[0]; data[0] = data[10]; data[10]=ret;
            ret = data[1]; data[1] = data[11]; data[11]=ret;
            ret = data[5]; data[5] = data[15]; data[15]=ret;
            ret = data[6]; data[6] = data[16]; data[16]=ret;
        }
}