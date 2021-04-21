#!/bin/bash
CLIENT_ADDR=c8.millennium.berkeley.edu
CLIENT_USER=amaro
CLIENT_REMOTE_DIR=/home/amaro

NICSERVER_ADDR=192.168.100.2
NICSERVER_USER=ubuntu
NICSERVER_REMOTE_DIR=/home/ubuntu

ARM_BIN=build_arm/bin
X86_BIN=build_x86/bin

scp ${ARM_BIN}/nicserver ${NICSERVER_USER}@${NICSERVER_ADDR}:${NICSERVER_REMOTE_DIR}
scp scripts/runme_benchmarks_bf1.sh ${NICSERVER_USER}@${NICSERVER_ADDR}:${NICSERVER_REMOTE_DIR}
scp ${X86_BIN}/client ${CLIENT_USER}@${CLIENT_ADDR}:${CLIENT_REMOTE_DIR}
