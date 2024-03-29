#!/bin/bash

if [[ $# -ne 4 ]]; then
    echo "Requires 4 parameters; ip, threads, output, workload"
    exit 2
fi

ip=$1
threads=$2
output=$3
workload=$4

binary=./client

if [[ ${workload} != "kvstore" ]]; then
    echo "workload=${workload} not supported in this script"
    exit 2
fi

. utils.sh
pre_experiment_setup
define_cpus ${threads}
define_load

set -x
for load in ${load}
do
    echo "load=${load}" | tee -a ${output}
    sudo taskset -c ${cpus} chrt -f 99 ${binary} -s ${ip} -o out \
        --mode load --load ${load} -t ${threads} --rmc ${workload} | tee -a ${output}
    sleep 25
done
