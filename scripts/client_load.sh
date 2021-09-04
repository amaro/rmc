#!/bin/bash

if [[ $# -ne 3 ]]; then
    echo "Requires 3 parameters; ip, threads, output"
    exit 2
fi

ip=$1
threads=$2
output=$3

. utils.sh

pre_experiment_setup
define_cpus ${threads}

for numnodes in 1 2 4 8 16
do
    for load in 20.0 10.0 6.667 5.0 4.0 3.333 2.857 2.500 2.222 2.000 1.818 \
                1.667 1.538 1.429 1.333 1.250 1.176 1.111 1.053 1.000
    do
        echo "load=${load} num_nodes=${numnodes}" | tee -a ${output}
        sudo MLX5_SINGLE_THREADED=1 taskset -c ${cpus} chrt -f 99 ./client -s ${ip} -o out \
            --mode load --load ${load} --param ${numnodes} -t ${threads} | tee -a ${output}
        sleep 20
    done
done
