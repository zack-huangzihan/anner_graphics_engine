cmake_minimum_required(VERSION 2.8.12)

PROJECT(anner)
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

find_package(PkgConfig REQUIRED)
pkg_search_module(EGL REQUIRED egl)
pkg_search_module(EGLESV2 REQUIRED glesv2)
pkg_search_module(X11 REQUIRED x11)

include_directories(include)
include_directories(${EGL_INCLUDE_DIRS})
include_directories(${EGLESV2_INCLUDE_DIRS})
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
add_subdirectory(demo)