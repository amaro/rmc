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
    sudo taskset -c ${cpus} chrt -f 99 ./nicserver -s 10.10.1.1 -q $1 -w $2 -t $3
    sleep 10
}

for numnodes in 1 2 4 8 16
do
	for rep in {1..5}
	do
		cmd ${numqps} ${workload} ${threads}
	done
done
