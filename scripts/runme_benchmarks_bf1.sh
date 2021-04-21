#!/bin/bash

# script assumes CPUs 0 and 1 are left to the kernel, and 2-15 should be isolated
if [ "$EUID" -ne 0 ]
  then echo "Please run as root"
  exit
fi

systemctl stop irqbalance.service

for i in /proc/irq/*/smp_affinity; do
        echo 3 > $i
done

echo -1 > /proc/sys/kernel/sched_rt_runtime_us

for (( c=2; c<=15; c++ ))
do
	echo 0 > /sys/devices/system/cpu/cpu$c/online
done

for (( c=2; c<=15; c++ ))
do
	echo 1 > /sys/devices/system/cpu/cpu$c/online
done
setcap cap_sys_nice=ep nicserver
