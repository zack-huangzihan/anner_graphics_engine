cmake_minimum_required(VERSION 2.8.12)

PROJECT(demo)
include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(TEST_SRC
	test.cpp
)
add_executable(anner_test ${TEST_SRC})
target_link_libraries(anner_test 
	libanner_x11.so
)