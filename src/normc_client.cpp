#include <fstream>
#include "utils/cxxopts.h"
#include "utils/utils.h"
#include "utils/logger.h"
#include "onesidedclient.h"
#include "rmc.h"

const int NUM_REPS = 100;
const std::vector<int> BUFF_SIZES = {1024, 2048, 4096, 8192, 16384, 32768, 65536, 131072, 262144, 524288};

void print_durations(std::ofstream &stream, int bufsize, const std::vector<long long> &durations)
{
    stream << "bufsize=" << bufsize << "\n";
    for (const long long &d: durations)
        stream << d << "\n";
}

void benchmark(std::string server, unsigned int port, std::string ofile)
{
    OneSidedClient client(1);
    std::vector<long long> durations(NUM_REPS);
    std::ofstream stream(ofile, std::ofstream::out);

    client.connect(server, port);

    // warm up
    const int &bufsize = BUFF_SIZES[0];
    for (size_t rep = 0; rep < NUM_REPS; ++rep)
        client.readhost(0, bufsize);

    // real thing
    for (size_t bufidx = 0; bufidx < BUFF_SIZES.size(); ++bufidx) {
        const int &bufsize = BUFF_SIZES[bufidx];
        for (size_t rep = 0; rep < NUM_REPS; ++rep) {
            time_point start = time_start();

            // similar to RMCWorker::Execute()
            client.readhost(0, bufsize);
            char *result_buffer = client.get_rdma_buffer();
            std::string res(result_buffer[0], bufsize);
            size_t hash = std::hash<std::string>{}(res);

            durations[rep] = time_end(start);

            LOG("hash=" << hash);
        }

        print_durations(stream, bufsize, durations);
    }
}

int main(int argc, char* argv[])
{
    /* server is a regular hostserver */
    cxxopts::Options opts("client", "NORMC client");

    opts.add_options()
        ("s,server", "hostserver address", cxxopts::value<std::string>())
        ("p,port", "hostserver port", cxxopts::value<std::string>()->default_value("30000"))
        ("o,output", "output file", cxxopts::value<std::string>())
        ("h,help", "Print usage")
    ;

    std::string server, ofile;
    unsigned int port;

    try {
        auto result = opts.parse(argc, argv);

        if (result.count("help"))
            die(opts.help());

        server = result["server"].as<std::string>();
        port = result["port"].as<int>();
        ofile = result["output"].as<std::string>();
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        die(opts.help());
    }

    benchmark(server, port, ofile);
}
