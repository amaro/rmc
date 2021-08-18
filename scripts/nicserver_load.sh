#!/bin/bash

set -x
if [[ $# -ne 1 ]]; then
    echo "Requires 1 parameters; first is numqps"
	exit 2
fi

sudo MLX5_SCATTER_TO_CQE=1 MLX5_SINGLE_THREADED=1 MLX5_POST_SEND_PREFER_BF=1 \
    MLX_MR_ALLOC_TYPE=CONTIG MLX_MR_MIN_LOG2_CONTIG_BSIZE=16 \
    MLX_QP_ALLOC_TYPE=HUGE MLX_CQ_ALLOC_TYPE=HUGE \
    taskset -c 7 chrt -f 99 ./nicserver -s 10.10.1.1 -q $1
