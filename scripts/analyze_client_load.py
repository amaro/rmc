#!/usr/bin/python3

import argparse
import statistics
import numpy as np

def is_float(s):
    try:
        float(s)
        return True
    except ValueError:
        return False

def print_summary(bufsize, durations):
    mean = round(statistics.mean(durations), 3)
    median = round(statistics.median(durations), 3)
    stats = "{},{},{}".format(bufsize, mean, median)
    print(stats)

#def get_cdf(num_bins, arr):
#    counts, bins = np.histogram(arr, bins=num_bins)
#    pdf = counts / float(counts.sum())
#    cdf = []
#    for idx, val in enumerate(pdf):
#        if idx == 0:
#            cdf.append(pdf[idx])
#        else:
#            cdf.append(cdf[idx - 1] + pdf[idx])
#
#    cdf.insert(0, 0)
#    return bins, cdf

#def print_hist(bsize, psize, durations):
#    num_bins = 20
#    bins, cdf = get_cdf(num_bins, durations)
#    print("bsize={}, psize={}\nbin,cdf".format(bsize, psize))
#    for idx, binn in enumerate(bins):
#        print("{},{}".format(binn, cdf[idx]))
#
#    print("\n")


def add_metric(table, load, numnodes, metric):
    if load not in table:
        table[load] = {}

    assert numnodes not in table[load]
    table[load][numnodes] = metric

def print_metric(table):
    print("load,1,2,4,8,16")
    for load in table:
        print(load, end='')
        for node in [1,2,4,8,16]:
            if node not in table[load]:
                print(",", end='')
                continue

            print(",{:.2f}".format(table[load][node]), end='')

        print("")

def parse_data(filename):
    with open(filename) as outfile:
        lines = outfile.readlines()

    data = []
    get_rtts = False
    skip_experiment = False
    print("load,num_nodes,median,99")

    # median = {'load': {1: YY, 2: XX,..,16: ZZ}}
    medians = {}
    pct99s = {}

    for line in lines:
        line = line.strip()
        if "num_nodes" in line:
            load = float(line.split()[0].split("=")[1])
            numnodes = int(line.split()[1].split("=")[1])
            skip_experiment = False
            data = []
        elif "start rtts" in line:
            get_rtts = True
        elif "end rtts" in line:
            get_rtts = False
            print("{},{},".format(load, numnodes), end='')
            if not skip_experiment and data:
                median = np.percentile(data, 50)
                pct99 = np.percentile(data, 99)
                print("{:.2f},{:.2f}".format(median, pct99))
                add_metric(medians, load, numnodes, median)
                add_metric(pct99s, load, numnodes, pct99)
            else:
                print("0,0")
        elif get_rtts:
            try:
                rtt = float(line)
            except ValueError:
                skip_experiment = True
                get_rtts = False
                continue
            
            if rtt <= 0:
                skip_experiment = True
                get_rtts = False
                continue
            
            data.append(rtt)

    print("MEDIANS")
    print_metric(medians)
    print("99pct")
    print_metric(pct99s)

def summary_analysis(args):
    parse_data(args.outfile)

def distrib_analysis(args):
    data = parse_data(args.outfile)

    for bsize in data:
        for psize in data[bsize]:
            print_hist(bsize, psize, data[bsize][psize])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('outfile', help='client load output file')
    cmdargs = parser.parse_args()

    summary_analysis(cmdargs)
