#!/bin/bash

set -x

if [ "$EUID" -ne 0 ]
  then echo "Please run as root"
  exit
fi

if [[ $# -ne 2 ]]; then
    echo "Requires numqps, workload"
	exit 2
fi

numqps=$1
workload=$2

cmd() {
    sudo MLX5_SCATTER_TO_CQE=1 MLX5_SINGLE_THREADED=1 \
       	MLX_MR_ALLOC_TYPE=CONTIG MLX_MR_MIN_LOG2_CONTIG_BSIZE=16 \
        MLX_QP_ALLOC_TYPE=HUGE MLX_CQ_ALLOC_TYPE=HUGE \
        taskset -c 7 chrt -f 99 ./nicserver -s 10.10.1.1 -q $1 -w $2
    sleep 10
}

for numnodes in 1 2 4 8
do
	for rep in {1..5}
	do
		cmd ${numqps} ${workload}
	done
done
