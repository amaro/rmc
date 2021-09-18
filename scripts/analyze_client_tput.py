#!/usr/bin/python3

import argparse
import statistics
import numpy as np

def print_summary(numnodes, durations):
    mean = round(statistics.mean(durations), 2)
    median = round(statistics.median(durations), 2)
    stats = "{},{},{}".format(numnodes, mean, median)
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

def parse_data(filename):
    with open(filename) as outfile:
        lines = outfile.readlines()

    # data[numnodes] = [list with ops per sec]
    data = {}

    for line in lines:
        if "numnodes" in line:
            numnodes = line.strip().split()[0]
            numnodes = numnodes.split("=")[1]
            if numnodes not in data:
                data[numnodes] = []
        elif "Ops per sec" in line:
            tput = float(line.strip().split("=")[1])
            data[numnodes].append(tput)

    return data


def validate(data):
    size = -1
    for key in data:
        if size == -1:
            size = len(data[key])
            continue

        if size != len(data[key]):
            print("Error, mismatch in data[key] lengths")
            print(data)
            exit(1)


def summary_analysis(args):
    data = parse_data(args.outfile)

    validate(data)
    print("numnodes,meantput,mediantput")
    for numnodes in data:
        print_summary(numnodes, data[numnodes])

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('outfile', help='client_tput.sh output file')
    cmdargs = parser.parse_args()

    summary_analysis(cmdargs)
