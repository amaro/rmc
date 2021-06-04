#!/bin/bash
CLIENT_ADDR=c8.millennium.berkeley.edu
CLIENT_USER=amaro
CLIENT_REMOTE_DIR=/home/amaro

NICSERVER_ADDR=192.168.100.2
NICSERVER_USER=ubuntu
NICSERVER_REMOTE_DIR=/home/ubuntu

ARM_BIN=build_arm/bin
X86_BIN=build_x86/bin

NICSERVER_FILES="${ARM_BIN}/nicserver scripts/runme_benchmarks_bf1.sh scripts/nicserver_load.sh"
CLIENT_FILES="${X86_BIN}/client scripts/client_load.sh"

scp ${NICSERVER_FILES} ${NICSERVER_USER}@${NICSERVER_ADDR}:${NICSERVER_REMOTE_DIR}
scp ${CLIENT_FILES} ${CLIENT_USER}@${CLIENT_ADDR}:${CLIENT_REMOTE_DIR}
