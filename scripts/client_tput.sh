#!/bin/bash

if [ "$EUID" -ne 0 ]
  then echo "Please run as root"
  exit
fi

if [[ $# -ne 3 ]]; then
    echo "Requires 3 parameters; nicserver IP, threads, outputfile"
	exit 2
fi

ip=$1
threads=$2
output=$3

sudo sh -c "echo -1 > /proc/sys/kernel/sched_rt_runtime_us"

if [[ ${threads} = "1" ]]; then
    cpus="7"
elif [[ ${threads} = "2" ]]; then
    cpus="6,7"
elif [[ ${threads} = "3" ]]; then
    cpus="5,6,7"
elif [[ ${threads} = "4" ]]; then
    cpus="4,5,6,7"
else
    echo "threads can only equal 1, 2, 3, 4"
    exit 2
fi

for numnodes in 1 2 4 8
do
	for rep in {1..5}
	do
		echo "numnodes=${numnodes} rep=${rep}" | tee -a ${output}
		sudo MLX5_SINGLE_THREADED=1 taskset -c ${cpus} chrt -f 99 ./client -s ${ip} \
			--mode maxinflight --param ${numnodes} -t ${threads} | tee -a ${output}
		sleep 20
	done
done
