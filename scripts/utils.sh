
pre_experiment_setup() {
    if [ "$EUID" -ne 0 ]; then
        echo "Please run as root"
        exit 2
    fi

    sudo sh -c "echo -1 > /proc/sys/kernel/sched_rt_runtime_us"
}

define_cpus() {
    threads=$1
    if [[ ${threads} = "1" ]]; then
        cpus="7"
    elif [[ ${threads} = "2" ]]; then
        cpus="6,7"
    elif [[ ${threads} = "3" ]]; then
        cpus="5-7"
    elif [[ ${threads} = "4" ]]; then
        cpus="4-7"
    elif [[ ${threads} = "5" ]]; then
        cpus="3-7"
    elif [[ ${threads} = "6" ]]; then
        cpus="2-7"
    elif [[ ${threads} = "7" ]]; then
        cpus="1-7"
    else
        echo "threads can only equal 1, 2, 3, 4, 5, 6, 7"
        exit 2
    fi
}
