#include "basic.hpp"
#include "serializer.hpp"
#include "trigger.hpp"
#include "launcher.hpp"
#include "zookeeper.hpp"
#include "socket-writer.hpp"
#include "uuid.hpp"
#include "server.hpp"

#include <boost/program_options.hpp>
#include <boost/log/trivial.hpp>
#include <boost/asio.hpp>
#include <boost/signals2.hpp>
#include <boost/format.hpp>

#include <oneapi/tbb/concurrent_unordered_map.h>
#include <oneapi/tbb/concurrent_queue.h>

#include <algorithm>
#include <iostream>

#include <memory>
#include <array>
#include <list>
#include <thread>
#include <vector>
#include <fstream>
#include <string>
#include <regex>

int main(int argc, char *argv[])
{
    using namespace slsfs::server;

    std::string verbosity_values;
    namespace po = boost::program_options;
    po::options_description desc{"Options"};
    desc.add_options()("help,h", "Print this help messages")("listen,l", po::value<unsigned short>()->default_value(12000), "listen on this port")("verbose,v", po::value<std::string>(&verbosity_values)->implicit_value(""), "log verbosity")("new-cluster", po::bool_switch(), "create new system (clear zookeeper entries)")("thread", po::value<int>()->default_value(std::thread::hardware_concurrency()), "# of thread")("server-id", po::value<double>()->default_value(-1.0), "server id position in ring [0-1]")("announce", po::value<std::string>(), "announce this ip address for other proxy to connect")("report", po::value<std::string>()->default_value("/dev/null"), "path to save report every seconds")("policy-filetoworker", po::value<std::string>(), "file to worker policy name")("policy-filetoworker-args", po::value<std::string>()->default_value(""), "file to worker policy name extra args")("policy-launch", po::value<std::string>(), "launch policy name")("policy-launch-args", po::value<std::string>()->default_value(""), "launch policy name extra args")("policy-keepalive", po::value<std::string>(), "keepalive policy name")("policy-keepalive-args", po::value<std::string>()->default_value(""), "keepalive policy name extra args")("enable-direct-connection", po::bool_switch(), "enable direct connection")("enable-cache", po::bool_switch(), "enable cache (default=false)")("enable-log-history", po::bool_switch(), "enable log history (default=false)")("cache-size", po::value<int>()->default_value(100), "cache size (MB)")("cache-policy", po::value<std::string>()->default_value(""), "cache policy: [LRU]")("worker-config", po::value<std::string>(), "worker config json file path to use")("shutdown", po::value<int>()->default_value(0), "set a timer for automatic shutdown (second)")("max-function-count", po::value<int>()->default_value(0), "marks the max random function name to use")("blocksize", po::value<int>()->default_value(4096), "worker config blocksize");
    po::positional_options_description pos_po;
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv)
                  .options(desc)
                  .positional(pos_po)
                  .run(),
              vm);
    po::notify(vm);

    if (vm.count("help"))
    {
        BOOST_LOG_TRIVIAL(info) << desc;
        return EXIT_SUCCESS;
    }

    if (vm.count("verbosity"))
        verbosity_values += "v";

    int const verbosity = verbosity_values.size();
    boost::log::trivial::severity_level const level = boost::log::trivial::info;
    slsfs::basic::init_log(static_cast<boost::log::trivial::severity_level>(level - static_cast<boost::log::trivial::severity_level>(verbosity)));
    BOOST_LOG_TRIVIAL(debug) << "set verbosity=" << verbosity;

    int const worker = vm["thread"].as<int>();
    net::io_context ioc{worker};

    unsigned short const port = vm["listen"].as<unsigned short>();
    std::string const announce = vm["announce"].as<std::string>();
    std::string const save_report = vm["report"].as<std::string>();
    int const blocksize = vm["blocksize"].as<int>();
    bool const enable_ddf = vm["enable-direct-connection"].as<bool>();
    bool const enable_cache = vm["enable-cache"].as<bool>();
    bool const enable_hist = vm["enable-log-history"].as<bool>();
    int const cache_size = vm["cache-size"].as<int>() * 1024 * 1024;
    int const shutdown = vm["shutdown"].as<int>();
    std::string const cache_policy = vm["cache-policy"].as<std::string>();
    bool const init_cluster = vm["new-cluster"].as<bool>();
    double const server_id_location = vm["server-id"].as<double>();

    slsfs::uuid::uuid server_id{};
    // server_id = slsfs::uuid::gen_uuid_static_seed(server_id_location);
    if (0 <= server_id_location && server_id_location <= 1) // set the id according to fix location
    {
        BOOST_LOG_TRIVIAL(info) << "Within limits 0 and 1";
        server_id.front() = server_id_location * 255;
    }
    else
    {
        server_id = slsfs::uuid::gen_uuid_static_seed(announce);
        BOOST_LOG_TRIVIAL(info) << "Generated server id = " << server_id;
    }
    BOOST_LOG_TRIVIAL(info) << "server_id" << server_id;

    std::string worker_config;
    {
        std::ifstream worker_configuration(vm["worker-config"].as<std::string>());

        if (!worker_configuration.is_open())
        {
            BOOST_LOG_TRIVIAL(fatal) << "config " << vm["worker-config"].as<std::string>() << "not found";
            return 1;
        }

        std::stringstream template_config;
        template_config << worker_configuration.rdbuf();

        BOOST_LOG_TRIVIAL(trace) << "content of config: " << template_config.str();
        worker_config = (boost::format(template_config.str()) % announce % port % blocksize % (enable_cache ? "true" : "false") % cache_size % cache_policy).str();
    }

    tcp_server server{ioc, port, server_id, announce, enable_ddf, save_report, enable_hist};

    set_policy_filetoworker(server, vm["policy-filetoworker"].as<std::string>(), vm["policy-filetoworker-args"].as<std::string>());
    set_policy_launch(server, vm["policy-launch"].as<std::string>(), vm["policy-launch-args"].as<std::string>());
    set_policy_keepalive(server, vm["policy-keepalive"].as<std::string>(), vm["policy-keepalive-args"].as<std::string>());
    server.set_worker_config(worker_config, vm["max-function-count"].as<int>());

    server.start_accept();
    BOOST_LOG_TRIVIAL(info) << server_id << " listen on " << port;

    int liveliness = 1;
    std::vector<char> announce_buf;
    fmt::format_to(std::back_inserter(announce_buf), "{}:{}:{}", announce, port, liveliness);

    slsfs::zookeeper::zookeeper zoo{ioc, server.launcher(), server_id, announce_buf};
    if (init_cluster)
    {
        BOOST_LOG_TRIVIAL(info) << "init cluster + init zookeeper";
        zoo.reset();
    }
    else
    {
        BOOST_LOG_TRIVIAL(info) << "start_setup";
        zoo.start_setup();
    }
    if (shutdown)
    {
        auto timer = std::make_shared<boost::asio::steady_timer>(ioc);
        using namespace std::chrono_literals;
        timer->expires_from_now(shutdown * 1s);
        timer->async_wait(
            [timer, &zoo](boost::system::error_code /*error*/)
            {
                zoo.shutdown();
            });
    }

    net::signal_set listener(ioc, SIGINT, SIGTERM);
    listener.async_wait(
        [&ioc, &zoo](boost::system::error_code const &, int signal_number)
        {
            BOOST_LOG_TRIVIAL(info) << "Stopping... sig=" << signal_number;
            zoo.shutdown();
            ioc.stop();
        });

    std::vector<std::jthread> worker_threads;
    worker_threads.reserve(worker);
    for (int i = 1; i < worker; i++)
        worker_threads.emplace_back([&ioc]
                                    { ioc.run(); });
    ioc.run();

    // The destructor may not run in main(); join manually
    for (std::jthread &th : worker_threads)
        th.join();

    return EXIT_SUCCESS;
}
