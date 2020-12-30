#!/bin/bash

BUILD_DIR_x86=$(realpath ./build_x86)
BUILD_DIR_arm=$(realpath ./build_arm)
CXX_x86=g++-10
CXX_arm=/home/amaro/downloads/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-g++
EXECS_x86="client hostserver normc_client"
EXECS_arm="nicserver"

mkdir ${BUILD_DIR_x86} -p && mkdir ${BUILD_DIR_arm} -p

echo "building x86 executables ${EXECS_x86}"
pushd ${BUILD_DIR_x86}
CXX=${CXX_x86} cmake ../
make ${EXECS_x86} $1
popd

echo "building ARM executables ${EXECS_arm}"
pushd ${BUILD_DIR_arm}
CXX=${CXX_arm} cmake ../
make ${EXECS_arm} $1
popd
