#!/bin/bash

. deploy.sh

NICSERVER_FILES="scripts/runme_benchmarks_bf1.sh scripts/nicserver_load.sh
                 scripts/nicserver_tput_sweephop.sh scripts/utils.sh"
CLIENT_FILES="scripts/client_load.sh scripts/client_tput.sh scripts/utils.sh
              scripts/analyze_client_tput.py scripts/analyze_client_load.py"

scp ${NICSERVER_FILES} ${NICSERVER_USER}@${NICSERVER_ADDR}:${NICSERVER_REMOTE_DIR}
scp ${CLIENT_FILES} ${CLIENT_USER}@${CLIENT_ADDR}:${CLIENT_REMOTE_DIR}
