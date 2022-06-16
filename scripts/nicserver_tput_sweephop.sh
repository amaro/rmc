#!/bin/bash

if [[ $# -ne 3 ]]; then
    echo "Requires numqps, threads"
    exit 2
fi

numqps=$1
threads=$2

. utils.sh

pre_experiment_setup
define_cpus ${threads}

cmd() {
    sudo taskset -c ${cpus} chrt -f 99 ./nicserver -s 10.10.1.1 -q $1 -t $2
    sleep 10
}

for numnodes in 1 2 4 8 16
do
	for rep in {1..5}
	do
		cmd ${numqps} ${threads}
	done
done
