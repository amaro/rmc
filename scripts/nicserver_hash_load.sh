#!/bin/bash

if [[ $# -ne 3 ]]; then
    echo "Requires numqps, threads, backend"
    exit 2
fi

numqps=$1
threads=$2
backend=$3

if [[ ${backend} = "rdma" ]]; then
    binary=./nicserver_rdma_hash
elif [[ ${workload} = "rdma" ]]; then
    binary=./nicserver_dram_hash
else
    echo "backend=${backend} not supported in this script"
    exit 2
fi

. utils.sh
pre_experiment_setup
define_cpus ${threads}
define_load

set -x
cmd() {
    sudo taskset -c ${cpus} chrt -f 99 ${binary} -s 10.10.1.1 -q $1 -t $2
    sleep 10
}

for load in ${load}
do
    cmd ${numqps} ${workload} ${threads}
done
