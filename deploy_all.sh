#!/bin/bash

. deploy.sh

NICSERVER_FILES="scripts/runme_benchmarks_bf1.sh scripts/nicserver_*.sh scripts/utils.sh"
HOSTSERVER_FILES="scripts/hostserver_load.sh"
CLIENT_FILES="scripts/client_*.sh scripts/utils.sh scripts/analyze_client_*.py"

set -x
scp ${NICSERVER_FILES} ${NICSERVER_USER}@${NICSERVER_ADDR}:${NICSERVER_REMOTE_DIR}
scp ${CLIENT_FILES} ${CLIENT_USER}@${CLIENT_ADDR}:${CLIENT_REMOTE_DIR}
cp ${NICSERVER_FILES} ${HOSTSERVER_FILES} ${X86_BIN}
