#!/bin/bash

if [[ $# -ne 3 ]]; then
    echo "Requires numqps, workload, threads"
    exit 2
fi

numqps=$1
workload=$2
threads=$3

if [[ ${workload} = "rdma_readll" ]] || [[ ${workload} = "rdma_readll_locked" ]] || [[ ${workload} = "rdma_randomwrite" ]]; then
    binary=./nicserver
    workload=${workload#"rdma_"}
elif [[ ${workload} = "dram_readll" ]] || [[ ${workload} = "dram_readll_locked" ]] || [[ ${workload} = "dram_randomwrite" ]]; then
    binary=./nicserver_dram
    workload=${workload#"dram_"}
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
        taskset -c ${cpus} chrt -f 99 ${binary} -s 10.10.1.1 -q $1 -w $2 -t $3
    sleep 10
}

for load in ${load}
do
    for numnodes in 1 2 4 8 16
    do
    	cmd ${numqps} ${workload} ${threads}
    done
done
