cmake_minimum_required(VERSION 2.8.12)

include(CheckCXXCompilerFlag)
CHECK_CXX_COMPILER_FLAG("-std=c++11" COMPILER_SUPPORTS_CXX11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")
pkg_search_module(LIBDRM REQUIRED libdrm)
include_directories(${LIBDRM_INCLUDE_DIRS})
include_directories(${LIBMALI_INCLUDE_DIRS})

set(MODESET_SINGLE_BUFFER
	modeset-single-buffer.c
)
add_executable(modeset-single-buffer ${MODESET_SINGLE_BUFFER})
target_link_libraries(modeset-single-buffer 
	${LIBDRM_LIBRARIES}
)


set(MODESET_DOUBLE_BUFFER
	modeset-double-buffer.c
)
add_executable(modeset-double-buffer ${MODESET_DOUBLE_BUFFER})
target_link_libraries(modeset-double-buffer 
	${LIBDRM_LIBRARIES}
)


set(MODESET_PAGE_FLIP
	modeset-page-flip.c
)
add_executable(modeset-page-flip ${MODESET_PAGE_FLIP})
target_link_libraries(modeset-page-flip
	${LIBDRM_LIBRARIES}
)

set(MODESET_PLANE_TEST
	modeset-plane-test.c
)
add_executable(modeset-plane-test ${MODESET_PLANE_TEST})
target_link_libraries(modeset-plane-test
	${LIBDRM_LIBRARIES}
)

set(MODESET_ATOMIC_CRTC
	modeset-atomic-crtc.c
)
add_executable(modeset-atomic-crtc ${MODESET_ATOMIC_CRTC})
target_link_libraries(modeset-atomic-crtc
	${LIBDRM_LIBRARIES}
)

set(MODESET_ATOMIC_PLANE
	modeset-atomic-plane.c
)
add_executable(modeset-atomic-plane ${MODESET_ATOMIC_PLANE})
target_link_libraries(modeset-atomic-plane
	${LIBDRM_LIBRARIES}
)