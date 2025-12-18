# anner_graphics_engine

An EGL rendering wrapper library that supports popular display frameworks, currently x11 and Wayland Weston.
Of course, this is also a simple example, let you understand the arm platform image data through the gpu rendering to the process of sending.

Under the "demo" directory, there are a large number of examples about gles2 rendering and drm display.

compile:

cd build && cmake .. -DWAYLAND=YES && make  (WAYLAND)

cd build && cmake .. -DX11=YES && make  (X11)

cd build && cmake .. -DUMMY=YES && make  (DUMMY)
