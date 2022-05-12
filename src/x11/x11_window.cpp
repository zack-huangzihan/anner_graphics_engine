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
	if(egl_init_x11((EGLNativeDisplayType) x_display) == -1) {
		printf("egl_init_x11 is fail\n");
		return -1;
	}
	pthread_create(&window_management, 0, anner_window_management, NULL);
	return 0;
}

int anner_deinit() {
	quit = true;
	egl_deinit_x11();
	x_deinit();
	return 0;
}