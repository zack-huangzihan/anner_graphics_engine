# anner_graphics_engine

An EGL rendering wrapper library that supports popular display frameworks, currently x11 and Wayland Weston.

compile:

cd build && cmake .. -DWAYLAND=YES && make  (WAYLAND)
cd build && cmake .. -DX11=YES && make  (WAYLAND)