#!/bin/bash

cleanup () {
    exit 0
}

trap cleanup SIGINT SIGTERM

if [[ $# -ne 2 ]]; then
    echo "Requires 2 parameters: num_qps, workload"
    exit 2
fi

qps=$1
workload=$2
binary=./hostserver
numa_mem=1
numa_cpu=8

if [[ ${workload} != "readll" ]] && [[ ${workload} != "hash" ]] && [[ ${workload} != "writerandom" ]]; then
    echo "workload=${workload} not supported in this script"
    exit 2
fi

. utils.sh
pre_experiment_setup

set -x

while true
do
    sudo numactl --physcpubind ${numa_cpu} --membind ${numa_mem} chrt -f 99 ${binary} -q ${qps} -w ${workload}
done
