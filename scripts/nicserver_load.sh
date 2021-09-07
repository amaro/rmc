#!/bin/bash

if [[ $# -ne 3 ]]; then
    echo "Requires numqps, workload, threads"
    exit 2
fi

numqps=$1
workload=$2
threads=$3

. utils.sh

pre_experiment_setup
define_cpus ${threads}

cmd() {
    sudo MLX5_SCATTER_TO_CQE=1 MLX5_SINGLE_THREADED=1 \
        MLX_MR_ALLOC_TYPE=CONTIG MLX_MR_MIN_LOG2_CONTIG_BSIZE=16 \
        taskset -c ${cpus} chrt -f 99 ./nicserver -s 10.10.1.1 -q $1 -w $2 -t $3
    sleep 10
}

for load in 10 5 3.333333 2.5 2 1.666667 1.428571 1.25 1.111111 1 \
            0.833333 0.714286 0.625000 0.555556 0.500000 0.454545 0.416667 0.384615 \
            0.357143 0.333333 0.312500 0.294118 0.277778 0.263158 0.250000 0.238095 \
            0.227273 0.217391 0.208333 0.200000 0.192308 0.185185 0.178571 0.172414 \
            0.166667 0.161290 0.156250 0.151515 0.147059 0.142857 0.138889 0.135135 \
            0.131579 0.128205 0.125000 0.121951 0.119048 0.116279 0.113636 0.111111 \
            0.108696 0.106383 0.104167 0.102041 0.100000 0.098039 0.096154 0.094340 \
            0.092593 0.090909
do
    for numnodes in 1 2 4 8 16
    do
    	cmd ${numqps} ${workload} ${threads}
    done
done
