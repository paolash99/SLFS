#include "../basic.hpp"
#include "../serializer.hpp"
#include "../json-replacement.hpp"
#include "../uuid.hpp"
#include "../scope_exit.hpp"
#include "clientlib.hpp"
#include "clientlib-direct-client.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <fmt/core.h>
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <absl/random/random.h>
#include <absl/random/zipf_distribution.h>
#include <zk/client.hpp>
#include <zk/results.hpp>
#include <zookeeper/zookeeper.h>

#include <chrono>

void start_test(boost::program_options::variables_map &vm)
{
    int    const total_times     = vm["total-times"].as<int>();
    int    const bufsize         = vm["bufsize"].as<int>();
    int    const worker          = vm["total-clients"].as<int>();
    bool   const init            = vm["init"].as<bool>();
    bool   const enable_fsync    = vm["fsync"].as<bool>();
    std::string const test_path  = vm["test-path"].as<std::string>();

    std::atomic<int> counter = 0;
    BOOST_LOG_TRIVIAL(info) << "starting test (thread=" << worker << "); bufsize=(" << bufsize << ")";
    std::vector<std::jthread> pool;

    if (init)
    {
        for (int i = 0; i < worker; i++)
        {
            std::string const filename = fmt::format("{}/{}.txt", test_path, i);
            BOOST_LOG_TRIVIAL(info) << "writing: " << filename;
            std::string const buf(40 * 1024 * 1024, 'A');
            std::ofstream out(filename);
            out << buf;
        }
        return;
    }

    auto const start = std::chrono::system_clock::now();
    for (int i = 0; i < worker; i++)
        pool.emplace_back(
            [total_times, worker, bufsize, start, test_path, enable_fsync, clienti=i, &counter] () mutable {
                std::random_device rd;
                int const seed = rd();
                std::mt19937 engine(seed);
                std::uniform_int_distribution<int> rw_dist(0, 1), offset_dist(0, 4 * 1024 * 1024 / (4 * 1024) - 1);

                std::string const filename = fmt::format("{}/{}.txt", test_path, clienti);

                BOOST_LOG_TRIVIAL(info) << "thread id=" << clienti << " seed=" << seed;

                try
                {
                    int const fd = open(filename.c_str(),  O_CREAT | O_RDWR | O_DIRECT, 0666);
                    if (fd < 0)
                        BOOST_LOG_TRIVIAL(error) << filename << " error getting fd: " << std::strerror(errno) << "\n";
                    SCOPE_DEFER([fd]{ close(fd); });

                    int constexpr alignment = 512;
                    void *buffer = std::aligned_alloc(alignment, bufsize);
                    SCOPE_DEFER([buffer] { std::free(buffer); });

                    boost::asio::io_context io_context;
                    boost::asio::signal_set listener(io_context, SIGINT, SIGTERM);
                    listener.async_wait(
                        [&io_context] (boost::system::error_code const&, int signal_number) {
                            BOOST_LOG_TRIVIAL(info) << "Stopping... sig=" << signal_number;
                            io_context.stop();
                        });

                    std::string buf(bufsize, 'A');
                    for (int i = 0; i < total_times; i++)
                    {
                        std::uint32_t const position = offset_dist(engine);

                        lseek(fd, position, SEEK_SET);

                        if (rw_dist(engine))
                            write(fd, buffer, bufsize);
                        else
                            read(fd, buffer, bufsize);

                        if (enable_fsync)
                            fsync(fd);
                        counter++;
                    }
                    return;
                } catch (boost::exception const& e) {
                    BOOST_LOG_TRIVIAL(error) << "boost exception catched at client " << clienti << " " << boost::diagnostic_information(e);
                }
            });

    for (std::jthread& th : pool)
        th.join();

    auto const end = std::chrono::system_clock::now();
    double const duration_us = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();

    BOOST_LOG_TRIVIAL(info) << "Finish " << counter.load() << " requests in " << duration_us / 1000 << "ms";
    BOOST_LOG_TRIVIAL(info) << "Throughput = "
                            << counter.load() * bufsize / duration_us * 1000000 / 1000 << " KBps";
}


int main(int argc, char *argv[])
{
    std::string verbosity_values;
    namespace po = boost::program_options;
    po::options_description desc{"Options"};
    desc.add_options()
        ("help,h", "Print this help messages")
        ("verbose,v",       po::value<std::string>(&verbosity_values)->implicit_value(""),"log verbosity")
        ("total-times",     po::value<int>()->default_value(10000),     "each client run # total times")
        ("total-clients",   po::value<int>()->default_value(1),         "# of clients")
        ("bufsize",         po::value<int>()->default_value(4096),      "Size of the read/write buffer")
        ("init",            po::bool_switch(),                          "init the path")
        ("fsync",           po::bool_switch(),                          "use fsync")
        ("test-path",       po::value<std::string>()->default_value("./"), "the path to create 4 files to test");

    po::positional_options_description pos_po;
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv)
              .options(desc)
              .positional(pos_po).run(), vm);
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

    start_test(vm);
}
