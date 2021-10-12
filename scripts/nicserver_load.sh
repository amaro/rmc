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
    sudo MLX5_SCATTER_TO_CQE=1 MLX5_SINGLE_THREADED=1 \
        MLX_MR_ALLOC_TYPE=CONTIG MLX_MR_MIN_LOG2_CONTIG_BSIZE=16 \
        taskset -c ${cpus} chrt -f 99 ./nicserver -s 10.10.1.1 -q $1 -w $2 -t $3
    sleep 10
}

for load in 20 10 5 3.333333333 2.5 2 1.666666667 1.428571429 1.25 1.111111111 1 0.9090909091 \
            0.8333333333 0.7692307692 0.7142857143 0.6666666667 0.625 0.5882352941 0.5555555556 \
            0.5263157895 0.5 0.4761904762 0.4545454545 0.4347826087 0.4166666667 0.4 0.3846153846 \
            0.3703703704 0.3571428571 0.3448275862 0.3333333333 0.3225806452 0.3125 0.303030303 \
            0.2941176471 0.2857142857 0.2777777778 0.2702702703 0.2631578947 0.2564102564 0.25 \
            0.243902439 0.2380952381 0.2325581395 0.2272727273 0.2222222222 0.2173913043 \
            0.2127659574 0.2083333333 0.2040816327 0.2 0.1960784314 0.1923076923 0.1886792453 \
            0.1851851852 0.1818181818 0.1785714286 0.1754385965 0.1724137931 0.1694915254 \
            0.1666666667 0.1639344262 0.1612903226 0.1587301587 0.15625 0.1538461538 0.1515151515 \
            0.1492537313 0.1470588235 0.1449275362 0.1428571429 0.1408450704 0.1388888889 \
            0.1369863014 0.1351351351 0.1333333333 0.1315789474 0.1298701299 0.1282051282 \
            0.1265822785 0.125 0.1234567901 0.1219512195 0.1204819277 0.119047619 0.1176470588 \
            0.1162790698 0.1149425287 0.1136363636 0.1123595506 0.1111111111 0.1098901099 \
            0.1086956522 0.1075268817 0.1063829787 0.1052631579 0.1041666667 0.1030927835 \
            0.1020408163 0.101010101 0.1 0.09900990099 0.09803921569 0.09708737864
do
    for numnodes in 1 2 4 8 16
    do
    	cmd ${numqps} ${workload} ${threads}
    done
done
