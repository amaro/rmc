#!/bin/bash

set -x
if [[ $# -ne 2 ]]; then
    echo "Requires 2 parameters; first is numqps, second is linked list nodes"
	exit 2
fi

sudo MLX5_SCATTER_TO_CQE=1 MLX5_SINGLE_THREADED=1 MLX5_POST_SEND_PREFER_BF=1 \
    taskset -c 15 chrt -f 99 ./nicserver -s 10.10.0.1 -n $2 -q $1
