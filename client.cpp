#include "cxxopts.h"
#include "rmcclient.h"
#include "rmc.h"

int main(int argc, char* argv[])
{
    cxxopts::Options opts("client", "Client for RDMA benchmarks");

    opts.add_options()
        ("s,server", "Server address", cxxopts::value<std::string>())
        ("p,port", "Server port", cxxopts::value<std::string>()->default_value("30000"))
        ("h,help", "Print usage")
    ;

    try {
        RMCClient client;
        auto result = opts.parse(argc, argv);

        if (result.count("help"))
            die(opts.help());

        std::string server = result["server"].as<std::string>();
        std::string port = result["port"].as<std::string>();

        client.connect_to_server(server, port);

        RMC rmc = "hello world\n";
        RMCId id = client.get_id(rmc);
        client.call(id);

        client.disconnect();
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        die(opts.help());
    }
}
