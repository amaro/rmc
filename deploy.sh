#!/bin/bash
CLIENT_ADDR=c8.millennium.berkeley.edu
CLIENT_USER=william
CLIENT_REMOTE_DIR=/home/william

NICSERVER_ADDR=192.168.100.2
NICSERVER_USER=root
NICSERVER_REMOTE_DIR=/root

ARM_BIN=build_arm/bin
X86_BIN=build_x86/bin

scp ${ARM_BIN}/nicserver ${NICSERVER_USER}@${NICSERVER_ADDR}:${NICSERVER_REMOTE_DIR}
scp ${X86_BIN}/client ${CLIENT_USER}@${CLIENT_ADDR}:${CLIENT_REMOTE_DIR}
