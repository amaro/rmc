#!/bin/bash

set -x
if [[ $# -ne 2 ]]; then
    echo "Requires 2 parameters; first is nicserver IP, second is output file"
	exit 2
fi

IP=$1

rm $2
for i in 6 5 4 3 2 1.5 1.4 1.3 1.2 1.1 1
do
	MLX5_SINGLE_THREADED=1 taskset -c 1 ./client -s ${IP} -o out --mode load --load ${i} >> $2
	sleep 6
done
