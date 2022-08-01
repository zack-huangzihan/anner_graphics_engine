cmake_minimum_required(VERSION 2.8.12)

PROJECT(anner)
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
add_definitions(-g)
find_package(PkgConfig REQUIRED)
pkg_search_module(EGL REQUIRED egl)
pkg_search_module(EGLESV2 REQUIRED glesv2)
include_directories(${EGL_INCLUDE_DIRS})
include_directories(${EGLESV2_INCLUDE_DIRS})
include_directories(src)
include_directories(include)

if (X11)
pkg_search_module(X11 REQUIRED x11)
include_directories(${X11_INCLUDE_DIRS})
link_directories(build)
set(ANNER_SRC
      src/x11/x11_window.cpp
      src/egl/anner_egl.cpp
)

add_library(anner_x11 SHARED ${ANNER_SRC})

target_link_libraries(anner_x11 
	${EGL_LIBRARIES}
	${EGLESV2_LIBRARIES}
	${X11_LIBRARIES}
)
endif ()

if (WAYLAND)
pkg_search_module(WAYLAND_CURSOR REQUIRED wayland-cursor)
pkg_search_module(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_search_module(WAYLAND_EGL REQUIRED wayland-egl)
#pkg_search_module(WESTON REQUIRED libweston-desktop-9)

include_directories(${WAYLAND_CURSOR_INCLUDE_DIRS})
include_directories(${WAYLAND_CLIENT_INCLUDE_DIRS})
include_directories(${WAYLAND_EGL_INCLUDE_DIRS})
#include_directories(${WESTON_INCLUDE_DIRS})

link_directories(build)
set(ANNER_SRC
      src/wayland/wayland_window.cpp
      src/wayland/xdg-shell-protocol.c
      src/wayland/platform.h
      src/egl/anner_egl.cpp
)

add_library(anner_wayland SHARED ${ANNER_SRC})

target_link_libraries(anner_wayland 
	${EGL_LIBRARIES}
	${EGLESV2_LIBRARIES}
	${WAYLAND_CURSOR_LIBRARIES}
	${WAYLAND_CLIENT_LIBRARIES}
	${WAYLAND_EGL_LIBRARIES}
#	${WESTON_LIBRARIES}
)
endif ()

if (DUMMY)
pkg_search_module(LIBDRM REQUIRED libdrm)
pkg_search_module(LIBMALI REQUIRED mali)
include_directories(${LIBDRM_INCLUDE_DIRS})
include_directories(${LIBMALI_INCLUDE_DIRS})
link_directories(build)
set(ANNER_SRC
      src/dummy/dummy_egl.cpp
      src/anner_effects.cpp
)

add_library(anner_dummy SHARED ${ANNER_SRC})
target_link_libraries(anner_dummy 
	${EGL_LIBRARIES}
	${EGLESV2_LIBRARIES}
	${LIBDRM_LIBRARIES}
	${LIBMALI_LIBRARIES}
)
endif ()

set(ANNER_TEST
      src/opengles-simple-egl.c
      src/wayland/xdg-shell-protocol.c
      src/wayland/platform.h
)

add_subdirectory(demo)
