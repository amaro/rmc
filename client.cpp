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

    std::string server;
    std::string port;
    RMCClient client;

    try {
        auto result = opts.parse(argc, argv);

        if (result.count("help"))
            die(opts.help());

        server = result["server"].as<std::string>();
        port = result["port"].as<std::string>();
    } catch (const std::exception &e) {
        std::cerr << e.what() << "\n";
        die(opts.help());
    }

    client.connect(server, port);

    const char *prog = R"(void hello() { printf("hello world\n"); })";

    RMC rmc(prog);
    RMCId id = client.get_rmc_id(rmc);
    std::cout << "got id=" << id << "\n";
    client.call_rmc(id);
    client.last_cmd();
}
