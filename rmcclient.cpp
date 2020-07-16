#include "cxxopts.h"
#include "client.h"

int main(int argc, char* argv[])
{
    cxxopts::Options opts("client", "Client for RDMA benchmarks");

    opts.add_options()
        ("s,server", "Server address", cxxopts::value<std::string>())
        ("p,port", "Server port", cxxopts::value<std::string>()->default_value("30000"))
        ("h,help", "Print usage")
    ;

    try {
        RDMAClient client;
        auto result = opts.parse(argc, argv);

        if (result.count("help"))
            die(opts.help());

        std::string server = result["server"].as<std::string>();
        std::string port = result["port"].as<std::string>();

        client.connect_to_server(server, port);

        client.disconnect();
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        die(opts.help());
    }
}
