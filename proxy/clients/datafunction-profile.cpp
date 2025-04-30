#include "../basic.hpp"
#include "../serializer.hpp"
#include "../json-replacement.hpp"
#include "../uuid.hpp"
#include "../server.hpp"

#include <fmt/core.h>
#include <boost/asio.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <absl/random/random.h>
#include <absl/random/zipf_distribution.h>


#include <algorithm>
#include <iostream>
#include <array>
#include <memory>
#include <list>
#include <thread>
#include <vector>
#include <random>
#include <chrono>

using boost::asio::ip::tcp;

template<typename Function>
auto record(Function &&f) -> long int
{
    //std::chrono::high_resolution_clock::time_point;
    auto const start = std::chrono::high_resolution_clock::now();
    std::invoke(f);
    auto const now = std::chrono::high_resolution_clock::now();
    auto relativetime = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count();
    return relativetime;
}

template<typename Iterator>
auto stats (Iterator start, Iterator end, std::string const memo = "")
    -> std::tuple<std::map<int, int>, int, int>
{
    int const size = std::distance(start, end);

    double sum = std::accumulate(start, end, 0.0);
    double mean = sum / size, var = 0;

    std::vector<typename std::iterator_traits<Iterator>::value_type> copied;
    std::copy (start, end, std::back_inserter(copied));
    std::sort(copied.begin(), copied.end());

    std::map<int, int> dist;
    for (auto const & element : copied)
    {
        dist[(element)/1000000]++;
        var += std::pow(element - mean, 2);
    }

    var /= size;
    BOOST_LOG_TRIVIAL(info) << fmt::format("{0} avg={1:.3f} sd={2:.3f} med={3:.3f}", memo, mean, std::sqrt(var), 1.0 * copied.at(copied.size()/2));
    for (auto && [time, count] : dist)
        BOOST_LOG_TRIVIAL(info) << fmt::format("{0} {1}: {2}", memo, time, count);

    return {dist, mean, std::sqrt(var)};
}

template<typename Next>
void start_in_sequence (
    slsfs::server::tcp_server& server,
    std::atomic<int>& left,
    int bufsize,
    oneapi::tbb::concurrent_vector<std::chrono::nanoseconds>& result,
    std::atomic<int>& concurrent_counter,
    oneapi::tbb::concurrent_vector<int>& concurrent_counter_result,
    Next next)
{
    if (left.load() <= 0)
    {
        std::invoke(next, result);
        return;
    }

    std::string const buf (bufsize, 'A');
    slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();

    ptr->header.type = slsfs::pack::msg_t::trigger;

    thread_local static std::mt19937 engine(19937);
    thread_local static std::uniform_int_distribution<std::uint8_t> dist(0, 255);

    ptr->header.key = slsfs::pack::key_t{dist(engine), dist(engine), dist(engine), dist(engine)};

    slsfs::jsre::request r;
    r.type = slsfs::jsre::type_t::file;
    if (true /* debug write */)
    {
        BOOST_LOG_TRIVIAL(trace) << "r.operation = slsfs::jsre::operation_t::write;";
        r.operation = slsfs::jsre::operation_t::write;
    }
    else
    {
        BOOST_LOG_TRIVIAL(trace) << "r.operation = slsfs::jsre::operation_t::read;";
        r.operation = slsfs::jsre::operation_t::read;
    }

    r.position = 0;
    r.size = buf.size();
    r.to_network_format();

    if (r.operation == slsfs::jsre::operation_t::read)
    {
        ptr->data.buf.resize(sizeof (r));
        std::memcpy(ptr->data.buf.data(), &r, sizeof (r));
    }
    else
    {
        ptr->data.buf.resize(sizeof (r) + buf.size());
        std::memcpy(ptr->data.buf.data(), &r, sizeof (r));
        std::memcpy(ptr->data.buf.data() + sizeof (r), buf.data(), buf.size());
    }

    ptr->header.gen();
    auto start = std::chrono::high_resolution_clock::now();

    concurrent_counter++;
    server.launcher().start_trigger_post(
        ptr->data.buf, ptr,
        [ptr, start, &server, &left, bufsize, &result, &concurrent_counter, &concurrent_counter_result, next]
        (slsfs::pack::packet_pointer) {
            auto end = std::chrono::high_resolution_clock::now();
            concurrent_counter_result.push_back(concurrent_counter.load());
            concurrent_counter--;

            start_in_sequence(server, left, bufsize, result, concurrent_counter, concurrent_counter_result, next);

            result.push_back(end - start);
            left--;

            BOOST_LOG_TRIVIAL(trace) << "finished in " << end - start;
        });
}


int main(int argc, char *argv[])
{
    slsfs::basic::init_log();
#ifdef NDEBUG
    boost::log::core::get()->set_filter(
        boost::log::trivial::severity >= boost::log::trivial::info);
#else
    boost::log::core::get()->set_filter(
        boost::log::trivial::severity >= boost::log::trivial::trace);
#endif // NDEBUG

    namespace po = boost::program_options;
    po::options_description desc{"Options"};
    desc.add_options()
        ("help,h", "Print this help messages")
        ("total-request", po::value<int>()->default_value(10000),     "how much total request in this test")
        ("total-df",      po::value<int>()->default_value(1),         "# of datafunction")
        ("concurrent-executer", po::value<int>()->default_value(1),   "# of concurrent executer")
        ("bufsize",       po::value<int>()->default_value(4096),      "Size of the read/write buffer")
        ("file-range",    po::value<int>()->default_value(256),       "set the total different number of files")
        ("port,p",        po::value<unsigned short>()->default_value(12000), "listen on this port")
        ("announce",      po::value<std::string>(),                   "announce this ip address for other proxy to connect")
        ("worker-config", po::value<std::string>(),                   "path to worker config")
        ("result",        po::value<std::string>()->default_value("/dev/null"), "save result to this file");

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

    std::mt19937 engine(19937);

    std::atomic<int> total_request = vm["total-request"].as<int>();
    int total_df                   = vm["total-df"].as<int>();
    int bufsize                    = vm["bufsize"].as<int>();
    int concurrent_executer        = vm["concurrent-executer"].as<int>();
    unsigned short port            = vm["port"].as<unsigned short>();
    std::string const announce     = vm["announce"].as<std::string>();
    std::string const resultfile   = vm["result"].as<std::string>();
    std::string       worker_config= vm["worker-config"].as<std::string>();

    {
        std::ifstream worker_configuration(worker_config);

        if (!worker_configuration.is_open())
        {
            BOOST_LOG_TRIVIAL(fatal) << "config " <<  vm["worker-config"].as<std::string>() << "not found";
            return 1;
        }

        std::stringstream template_config;
        template_config << worker_configuration.rdbuf();
        worker_config = (boost::format(template_config.str()) % announce % port % 4096).str();
    }


    //absl::zipf_distribution namedist(times.load(), zipf_alpha);
    std::uniform_int_distribution<> uniformdist(0, 256);

    boost::asio::io_context ioc;
    slsfs::uuid::uuid server_id = slsfs::uuid::gen_uuid_static_seed(announce);
    slsfs::server::tcp_server server{ioc, port, server_id, announce, false, resultfile, false};

    slsfs::server::set_policy_filetoworker(server, "active-load-balance", "");
    slsfs::server::set_policy_launch      (server, "worker-launch-fix-pool", fmt::format("64:{}", total_df));
    slsfs::server::set_policy_keepalive   (server, "moving-interval-global", "5:60000:1000:50");

    server.set_worker_config(worker_config, 15);
    server.start_accept();

    std::vector<std::thread> ths;
    ths.emplace_back([&ioc] { ioc.run(); });

    oneapi::tbb::concurrent_vector<std::chrono::nanoseconds> result;

    std::atomic<bool> called = false;
    auto start = std::chrono::high_resolution_clock::now();

    std::atomic<int> concurrent_counter = 0;
    oneapi::tbb::concurrent_vector<int> concurrent_counter_result;

    for (int i = 0; i < concurrent_executer; i++)
        start_in_sequence(
            server, total_request, bufsize, result, concurrent_counter, concurrent_counter_result,
            [&ioc, start, total_request=total_request.load(), &server, &concurrent_counter, &concurrent_counter_result, &called]
            (oneapi::tbb::concurrent_vector<std::chrono::nanoseconds> &result) {
                auto end = std::chrono::high_resolution_clock::now();
                if (called)
                    return;

                called = true;

                oneapi::tbb::concurrent_vector<std::uint64_t> v;
                for (std::chrono::nanoseconds t : result)
                    v.push_back(t.count());
                stats(v.begin(), v.end(), fmt::format("write result"));

                slsfs::launcher::policy::print (server.launcher().fileid_to_worker());

                std::chrono::nanoseconds fulltime = (end - start);
                std::chrono::nanoseconds totaltime = fulltime - result.front();
                BOOST_LOG_TRIVIAL(info) << "fulltime: " << fulltime;
                BOOST_LOG_TRIVIAL(info) << "totaltime: " << totaltime;
                BOOST_LOG_TRIVIAL(info) << "iops: " << (v.size()-1) / (totaltime.count() / 1000000000.0);

                ioc.stop();
            });

    for (std::thread& th : ths)
        th.join();

    return EXIT_SUCCESS;
}
