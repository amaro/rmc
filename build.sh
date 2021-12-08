#!/bin/bash

build() {
    mkdir ${BUILD_DIR} -p
    echo "building ${EXECS} with ${CXX}"
    pushd ${BUILD_DIR}
    CXX=${CXX} CXXFLAGS=${CXXFLAGS} cmake ../
    make ${EXECS} $1
    popd
}

BUILD_DIR=$(realpath ./build_x86)
CXX=g++-10
CXXFLAGS="-march=native"
EXECS="client hostserver nicserver nicserver_rdma_hash nicserver_rdma_log nicserver_dram nicserver_dram_hash nicserver_dram_log nicserver_rdmacomp nicserver_client nicserver_client_hash nicserver_client_log"
build $1

BUILD_DIR=$(realpath ./build_arm)
CXX=/home/amaro/RMC/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-g++
CXXFLAGS="-march=armv8-a+simd+crc -mtune=cortex-a72"
EXECS="nicserver nicserver_rdma_hash nicserver_rdma_log nicserver_dram nicserver_rdmacomp"
build $1
