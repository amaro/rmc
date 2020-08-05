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

def parse_data(filename):
    with open(filename) as outfile:
        lines = outfile.readlines()

    # data[bufsize] = [list with durations]
    data = {}

    for line in lines:
        if "bufsize" in line:
            bufsize = line.strip().split("=")[1]
            data[bufsize] = []
        elif is_float(line):
            data[bufsize].append(float(line))
        else:
            print("unrecognized string={}".format(line))
            quit()

    return data


def summary_analysis(args):
    data = parse_data(args.outfile)

    print("bufsize,mean,median")
    for bufsize in data:
        print_summary(bufsize, data[bufsize])

def distrib_analysis(args):
    data = parse_data(args.outfile)

    for bsize in data:
        for psize in data[bsize]:
            print_hist(bsize, psize, data[bsize][psize])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('outfile', help='client output file')
    cmdargs = parser.parse_args()

    summary_analysis(cmdargs)
