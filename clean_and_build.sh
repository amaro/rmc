#!/bin/bash

BUILD_DIR_x86=$(realpath ./build_x86)
BUILD_DIR_arm=$(realpath ./build_arm)
rm ${BUILD_DIR_x86} -rf && rm ${BUILD_DIR_arm} -rf
./build.sh -j
