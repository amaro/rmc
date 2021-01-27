#!/bin/bash

build() {
    mkdir ${BUILD_DIR} -p
    echo "building ${EXECS} with ${CXX}"
    pushd ${BUILD_DIR}
    CXX=${CXX} cmake ../ -DRDMA_CORE=${RDMA_CORE}
    make ${EXECS} $1
    popd
}

BUILD_DIR=$(realpath ./build_x86)
CXX=g++-10
EXECS="client hostserver normc_client"
RDMA_CORE=/home/amaro/RMC/rdma-core/build_x86
build $1

BUILD_DIR=$(realpath ./build_arm)
CXX=/home/amaro/RMC/gcc-arm-10.2-2020.11-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-g++
EXECS="nicserver"
RDMA_CORE=/home/amaro/RMC/rdma-core/build_arm
build $1
