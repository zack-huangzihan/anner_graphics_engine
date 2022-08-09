cmake_minimum_required(VERSION 2.8.12)

PROJECT(demo)
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
if (DUMMY)
pkg_search_module(LIBMALI REQUIRED mali)
set(TEST_SRC
	dummy_test.cpp
)
add_executable(anner_test ${TEST_SRC})
target_link_libraries(anner_test 
	libanner_dummy.so
	${LIBMALI_LIBRARIES}
)
endif ()

if (X11)
set(TEST_SRC
	test.cpp
)
add_executable(anner_test ${TEST_SRC})
target_link_libraries(anner_test 
	libanner_x11.so
)
endif ()

if (WAYLAND)
set(TEST_SRC
	test.cpp
)
add_executable(anner_test ${TEST_SRC})
target_link_libraries(anner_test 
	libanner_wayland.so
)
endif ()
