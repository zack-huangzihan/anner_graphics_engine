#!/bin/bash

../../buildroot/output/rockchip_rk3562/host/bin/aarch64-buildroot-linux-gnu-g++ gles2_test.c -o gles2_test -lmali -lEGL -lGLESv2 -ldrm -lm -lgbm -I../../buildroot/output/rockchip_rk3562/host/aarch64-buildroot-linux-gnu/sysroot/usr/include/libdrm