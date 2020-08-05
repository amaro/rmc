#include <fstream>
#include "utils/cxxopts.h"
#include "onesidedclient.h"
#include "rmc.h"
#include "utils/utils.h"
#include "utils/logger.h"

const int NUM_REPS = 100;
const std::vector<int> BUFF_SIZES = {8, 32, 64, 128, 512, 2048, 4096, 8192, 16384, 32768};

void print_durations(std::ofstream &stream, int bufsize, const std::vector<long long> &durations)
{
    stream << "bufsize=" << bufsize << "\n";
    for (const long long &d: durations)
        stream << d << "\n";
}

void benchmark(std::string server, std::string port, std::string ofile)
{
    OneSidedClient client;
    std::vector<long long> durations(NUM_REPS);
    //long long duration;
    std::ofstream stream(ofile, std::ofstream::out);

    client.connect(server, port);

    // warm up
    const int &bufsize = BUFF_SIZES[0];
    for (size_t rep = 0; rep < NUM_REPS; ++rep)
        client.readhost(0, bufsize);

    // real thing
    //for (size_t bufidx = 0; bufidx < BUFF_SIZES.size(); ++bufidx) {
    //    const int &bufsize = BUFF_SIZES[bufidx];
    //    for (size_t rep = 0; rep < NUM_REPS; ++rep) {
    //        assert(!client.call_rmc(id, bufsize, duration));
    //        durations[rep] = duration;
    //    }

    //    print_durations(stream, bufsize, durations);
    //}
}

int main(int argc, char* argv[])
{
    cxxopts::Options opts("client", "NORMC client");

    opts.add_options()
        ("s,server", "normc_hostserver address", cxxopts::value<std::string>())
        ("p,port", "normc_hostserver port", cxxopts::value<std::string>()->default_value("30000"))
        ("o,output", "output file", cxxopts::value<std::string>())
        ("h,help", "Print usage")
    ;

    std::string server, port, ofile;

    try {
        auto result = opts.parse(argc, argv);

        if (result.count("help"))
            die(opts.help());

        server = result["server"].as<std::string>();
        port = result["port"].as<std::string>();
        ofile = result["output"].as<std::string>();
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        die(opts.help());
    }

    benchmark(server, port, ofile);
}
