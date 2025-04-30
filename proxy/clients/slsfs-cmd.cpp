#include "../basic.hpp"
#include "../serializer.hpp"
#include "../json-replacement.hpp"
#include "../uuid.hpp"
#include "clientlib.hpp"
#include "clientlib-client-pool.hpp"
#include "clientlib-direct-client.hpp"

#include <fmt/core.h>
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <absl/random/random.h>
#include <absl/random/zipf_distribution.h>
#include <zk/client.hpp>
#include <zk/results.hpp>
#include <zookeeper/zookeeper.h>

#include <algorithm>
#include <iostream>
#include <array>
#include <memory>
#include <array>
#include <list>
#include <thread>
#include <vector>
#include <random>
#include <chrono>

void run(slsfs::client::client &slsfs_client, slsfs::client::direct_client &slsfs_direct_client)
{
    while (true)
    {
        std::string cmd, arg1, arg2, arg3;
        std::cout << "cmd> ";
        std::cin >> cmd >> arg1;
        bool direct = false;
        bool open = false;

        slsfs::pack::packet_pointer request = nullptr;

        switch (slsfs::basic::sswitcher::hash(cmd))
        {
            using namespace slsfs::basic::sswitcher;
        case "init"_:
            request = slsfs::client::packet_create::init(arg1);
            break;
        case "ls"_:
            request = slsfs::client::packet_create::ls(arg1);
            break;
        case "mkdir"_:
            request = slsfs::client::packet_create::mkdir(arg1);
            break;
        case "addfile"_:
            request = slsfs::client::packet_create::add_file_v2(arg1);
            break;
        case "deletefile"_:
            request = slsfs::client::packet_create::delete_file(arg1);
            break;
        case "rename"_:
            std::cin >> arg2;
            request = slsfs::client::packet_create::rename_file(arg1, arg2);
            break;
        case "write"_:
            std::cin >> arg2;
            std::cin >> arg3;
            request = slsfs::client::packet_create::write_v2(arg1, arg2, std::stoi(arg3));
            direct = true;
            break;
        case "read"_:
            std::cin >> arg2;
            request = slsfs::client::packet_create::read_v2(arg1, std::stoi(arg2));
            direct = true;
            break;
        case "open"_:
            request = slsfs::client::packet_create::open(arg1);
            open = true;
            break;
        case "close"_:
            request = slsfs::client::packet_create::close(arg1);
            break;
        case "getstatdir"_:
            request = slsfs::client::packet_create::get_stat_dir(arg1);
            break;
        case "getstatfile"_:
            request = slsfs::client::packet_create::get_stat_file(arg1);
            break;
        case "writeaddfile"_:
            std::cin >> arg2;
            request = slsfs::client::packet_create::create_and_write_file_single_request(arg1, arg2);
            break;
        default:
            BOOST_LOG_TRIVIAL(error) << "unknown cmd '" << cmd << "'";
            return;
        }
        if (open)
        {
            std::string resp = slsfs_client.send(request);
            boost::asio::ip::address_v4::bytes_type host;
            std::memcpy(&host, resp.data(), sizeof(host));
            boost::asio::ip::address address = boost::asio::ip::make_address_v4(host);
            std::uint16_t port = 0;
            std::memcpy(&port, resp.data() + sizeof(host), sizeof(port));
            port = slsfs::pack::hton(port);
            boost::asio::ip::tcp::endpoint endpoint(address, port);
            slsfs_direct_client.add_endpoint(endpoint, request);
        }
        else
        {
            if (direct)
            {
                std::cout << slsfs_direct_client.send(request) << "\n";
            }
            else
            {
                std::cout << slsfs_client.send(request) << "\n";
            }
        }
    }
}

int main(int argc, char *argv[])
{
    slsfs::basic::init_log();
    std::string verbosity_values;

    namespace po = boost::program_options;
    po::options_description desc{"Options"};
    desc.add_options()("help,h", "Print this help messages")("verbose,v", po::value<std::string>(&verbosity_values)->implicit_value(""), "log verbosity")("direct", po::bool_switch(), "enable direct client")("zookeeper", po::value<std::string>()->default_value("zk://zookeeper-1:2181"), "zookeeper host");

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

    // bool const enable_direct = vm["direct"].as<bool>();

    boost::asio::io_context io_context;

    boost::asio::signal_set listener(io_context, SIGINT, SIGTERM);
    listener.async_wait(
        [&io_context](boost::system::error_code const &, int signal_number)
        {
            BOOST_LOG_TRIVIAL(debug) << "Stopping... sig=" << signal_number;
            io_context.stop();
        });

    // if (enable_direct)
    // {
    //     slsfs::client::client_pool<slsfs::client::direct_client> pool{10, vm["zookeeper"].as<std::string>()};
    //     run(pool);
    // }
    // else
    // {
    //     slsfs::client::client slsfs_client{io_context, vm["zookeeper"].as<std::string>()};
    //     run(slsfs_client);

    // }

    slsfs::client::client slsfs_client{io_context, vm["zookeeper"].as<std::string>()};
    slsfs::client::direct_client slsfs_direct_client{io_context, vm["zookeeper"].as<std::string>()};
    run(slsfs_client, slsfs_direct_client);

    return EXIT_SUCCESS;
}
