#include  <iostream>
#include  <cstdlib>
#include  <cstring>
#include  <cmath>
#include  <sys/time.h>
#include  <pthread.h>
#include  <X11/Xlib.h>
#include  <X11/Xatom.h>
#include  <X11/Xutil.h>
#include  <cassert>
#include  <unistd.h> 
#include  "anner_egl.h"

using namespace std; 

extern EGLDisplay  	egl_display;
extern EGLContext  	egl_context;
extern EGLSurface  	egl_surface;
extern EGLConfig       ecfg;
extern void shader_init();

Display    *x_display;
Window      win;
XSetWindowAttributes  swa;
XSetWindowAttributes  xattr;
XWMHints hints;
Atom  atom;
Atom wm_state;
Atom fullscreen;
XEvent xev;
bool quit = false;
pthread_t window_management;
int deinit_flag = 0;

int anner_init() {
	x_display = XOpenDisplay ( NULL );   // open the standard display (the primary screen)
	if ( x_display == NULL ) {
		cerr << "cannot connect to X server" << endl;
		return -1;
	}
	return 0;
}

int x_deinit() {
	XDestroyWindow    ( x_display, win );
	XCloseDisplay     ( x_display );
	return 0;
}

int egl_init (void* display, Window win){
	egl_display  =  eglGetDisplay( (EGLNativeDisplayType) display );
	if ( egl_display == EGL_NO_DISPLAY ) {
		cerr << "Got no EGL display." << endl;
		return -1;
	}
	if ( !eglInitialize( egl_display, NULL, NULL ) ) {
		cerr << "Unable to initialize EGL" << endl;
		return -1;
	}
	if ( !eglBindAPI(EGL_OPENGL_ES_API) ) {
		cerr << "Unable to eglBindAPI EGL" << endl;
		return -1;		
	}

	EGLint attr[] = {       // some attributes to set up our egl-interface
		EGL_BUFFER_SIZE, 16,
		EGL_RENDERABLE_TYPE,
		EGL_OPENGL_ES2_BIT,
		EGL_NONE
	};
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
	return 0;
}

void *anner_window_management(void *arg) {
	while ( !quit ) {    // the main loop
		while ( XPending ( x_display ) ) {   // check for events from the x-server
			XNextEvent( x_display, &xev );
			if ( xev.type == KeyPress ) {
				printf("\nefqwfegfef\n");
				quit = true;
			}   
		}
	}
}

int anner_create_window(int window_width, int window_height) {
	x_display = XOpenDisplay ( NULL );   // open the standard display (the primary screen)
	if ( x_display == NULL ) {
		cerr << "cannot connect to X server" << endl;
		return -1;
	}
	Window root  =  DefaultRootWindow( x_display );   // get the root window (usually the whole screen)
	swa.event_mask  =  ExposureMask | PointerMotionMask | KeyPressMask;
	win  =  XCreateWindow (   // create a window with the provided parameters
												 x_display, root,
												 0, 0, window_width, window_height,   0,
												 CopyFromParent, InputOutput,
												 CopyFromParent, CWEventMask,
												 &swa );
	int   one = 1;
	xattr.override_redirect = False;
	XChangeWindowAttributes ( x_display, win, CWOverrideRedirect, &xattr );
	//atom = XInternAtom ( x_display, "_NET_WM_STATE_FULLSCREEN", True );
	XChangeProperty (
				x_display, win,
				XInternAtom ( x_display, "_NET_WM_STATE", True ),
				XA_ATOM,  32,  PropModeReplace,
				(unsigned char*) &atom,  1 );
	
	XChangeProperty (
				x_display, win,
				XInternAtom ( x_display, "_HILDON_NON_COMPOSITED_WINDOW", False ),
				XA_INTEGER,  32,  PropModeReplace,
				(unsigned char*) &one,  1);
	hints.input = True;
	hints.flags = InputHint;
	XSetWMHints(x_display, win, &hints);
	XMapWindow ( x_display , win );             // make the window visible on the screen
	XStoreName ( x_display , win , "test" ); // give the window a name
	// get identifiers for the provided atom name strings
	wm_state   = XInternAtom ( x_display, "_NET_WM_STATE", False );
	fullscreen = XInternAtom ( x_display, "_NET_WM_STATE_FULLSCREEN", False );
	memset ( &xev, 0, sizeof(xev) );
	xev.type                 = ClientMessage;
	xev.xclient.window       = win;
	xev.xclient.message_type = wm_state;
	xev.xclient.format       = 32;
	xev.xclient.data.l[0]    = 1;
	xev.xclient.data.l[1]    = 0;
	XSendEvent (                // send an event mask to the X-server
							x_display,
							DefaultRootWindow ( x_display ),
							False,
							SubstructureNotifyMask,
							&xev );
	if(egl_init((EGLNativeDisplayType) x_display, win) == -1) {
		printf("egl_init_x11 is fail\n");
		return -1;
	}

	shader_init();
	pthread_create(&window_management, 0, anner_window_management, NULL);
	return 0;
}

void anner_render(int w, int h) {
		egl_render(w, h);
}

int egl_deinit_x11() {
	eglDestroyContext ( egl_display, egl_context );
	eglDestroySurface ( egl_display, egl_surface );
	eglTerminate      ( egl_display );
	return 0;
}

void anner_destory_window() {
	quit = true;
	egl_deinit_x11();
	x_deinit();
}

