#!/bin/bash

if [[ $# -ne 3 ]]; then
    echo "Requires 3 parameters; nicserver IP, threads, outputfile"
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
	for rep in {1..5}
	do
		echo "numnodes=${numnodes} rep=${rep}" | tee -a ${output}
		sudo MLX5_SINGLE_THREADED=1 taskset -c ${cpus} chrt -f 99 ./client -s ${ip} \
			--mode maxinflight --param ${numnodes} -t ${threads} | tee -a ${output}
		sleep 20
	done
done
