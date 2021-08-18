#!/bin/bash

if [ "$EUID" -ne 0 ]
  then echo "Please run as root"
  exit
fi

if [[ $# -ne 2 ]]; then
    echo "Requires 2 parameters; first is nicserver IP, second is output file"
	exit 2
fi

ip=$1
output=$2

sudo sh -c "echo -1 > /proc/sys/kernel/sched_rt_runtime_us"

for numnodes in 1 2 4 8
do
	for rep in {1..5}
	do
		echo "numnodes=${numnodes} rep=${rep}" | tee -a ${output}
		sudo MLX5_SINGLE_THREADED=1 taskset -c 7 chrt -f 99 ./client -s ${ip} \
			-o out --mode maxinflight --param ${numnodes} | tee -a ${output}
		sleep 20
	done
done
