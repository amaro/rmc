#!/bin/bash

if [[ $# -ne 3 ]]; then
    echo "Requires numqps, workload, threads"
    exit 2
fi

numqps=$1
workload=$2
threads=$3

if [[ ${workload} = "rdma_readll" ]] || [[ ${workload} = "rdma_readll_lock" ]] || [[ ${workload} = "rdma_writerandom" ]]; then
    binary=./nicserver
    workload=${workload#"rdma_"}
elif [[ ${workload} = "dram_readll" ]] || [[ ${workload} = "dram_readll_lock" ]] || [[ ${workload} = "dram_writerandom" ]]; then
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
    sudo taskset -c ${cpus} chrt -f 99 ${binary} -s 10.10.1.1 -q $1 -w $2 -t $3
    sleep 10
}

for load in ${load}
do
    for numnodes in 1 2 4 8 16
    do
    	cmd ${numqps} ${workload} ${threads}
    done
done
