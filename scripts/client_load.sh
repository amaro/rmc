#!/bin/bash

set -x
if [[ $# -ne 2 ]]; then
    echo "Requires 2 parameters; first is nicserver IP, second is output file"
	exit 2
fi

IP=$1

sudo sh -c "echo -1 > /proc/sys/kernel/sched_rt_runtime_us"

rm $2
# min is 100K req/s; max is 1.5M req/s
for i in 10.000 6.667 5.000 4.000 3.333 2.857 2.500 2.222 2.000 1.818 \
            1.667 1.538 1.429 1.333 1.250 1.176 1.111 1.053 1.000 0.952 \
            0.909 0.870 0.833 0.800 0.769 0.741 0.714 0.690 0.667
do
	sudo MLX5_SINGLE_THREADED=1 taskset -c 7 chrt -f 99 ./client -s ${IP} -o out --mode load --load ${i} >> $2
	sleep 5
done
