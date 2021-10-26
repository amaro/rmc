#!/bin/bash

if [[ $# -ne 3 ]]; then
    echo "Requires numqps, workload, threads"
    exit 2
fi

numqps=$1
workload=$2
threads=$3

if [[ ${workload} = "dram_hash" ]]; then
    binary=./nicserver_dram_hash
    workload=${workload#"dram_"}
elif [[ ${workload} = "rdma_hash" ]]; then
    binary=./nicserver_rdma_hash
    workload=${workload#"rdma_"}
else
    echo "workload=${workload} not supported in this script"
    exit 2
fi

. utils.sh
pre_experiment_setup
define_cpus ${threads}
define_load

set -x
cmd() {
    sudo MLX5_SCATTER_TO_CQE=1 MLX5_SINGLE_THREADED=1 \
        MLX_MR_ALLOC_TYPE=CONTIG MLX_MR_MIN_LOG2_CONTIG_BSIZE=16 \
        taskset -c ${cpus} chrt -f 99 ${binary} -s 10.10.1.1 -q $1 -w $2 -t $3
    sleep 10
}

for load in ${load}
do
    cmd ${numqps} ${workload} ${threads}
done
