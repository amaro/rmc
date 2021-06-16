#!/bin/bash

set -x
if [[ $# -ne 2 ]]; then
    echo "Requires 2 parameters; first is numqps, second is linked list nodes"
	exit 2
fi

sudo MLX5_SCATTER_TO_CQE=1 MLX5_SINGLE_THREADED=1 MLX5_POST_SEND_PREFER_BF=1 \
    MLX_MR_ALLOC_TYPE=CONTIG MLX_MR_MIN_LOG2_CONTIG_BSIZE=16 \
    MLX_QP_ALLOC_TYPE=HUGE MLX_CQ_ALLOC_TYPE=HUGE \
    taskset -c 15 chrt -f 99 ./nicserver -s 10.10.0.1 -n $2 -q $1
