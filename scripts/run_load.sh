#!/bin/bash

set -x
if [[ $# -ne 2 ]]; then
    echo "Requires 2 parameters; first is nicserver IP, second is output file"
	exit 2
fi

IP=$1

rm $2
for i in 8 4 2 1.5 1.4 1.3 1.2 1.1 1
do
	MLX5_SINGLE_THREADED=1 sudo taskset -c 7 chrt -f 99 ./client -s ${IP} -o out --mode load --load ${i} >> $2
	sleep 6
done
