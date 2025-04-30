#include "../basic.hpp"
#include "../serializer.hpp"
#include "../json-replacement.hpp"
#include "../uuid.hpp"

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

using boost::asio::ip::tcp;

constexpr bool enable_creation = false;

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
auto stats(Iterator start, Iterator end, std::string const memo = "")
    -> std::tuple<std::map<int, int>, int, int>
{
    int const size = std::distance(start, end);

    double sum = std::accumulate(start, end, 0.0);
    double mean = sum / size, var = 0;

    std::map<int, int> dist;
    for (; start != end; start++)
    {
        dist[(*start)/1000000]++;
        var += std::pow((*start) - mean, 2);
    }

    var /= size;
    BOOST_LOG_TRIVIAL(debug) << fmt::format("{0} avg={1:.3f} sd={2:.3f}", memo, mean, std::sqrt(var));
    for (auto && [time, count] : dist)
        BOOST_LOG_TRIVIAL(debug) << fmt::format("{0} {1}: {2}", memo, time, count);

    return {dist, mean, std::sqrt(var)};
}

auto get_uuid (boost::asio::io_context &io_context, std::string const& child) -> boost::asio::ip::tcp::resolver::results_type
{
    using namespace std::string_literals;

    zk::client client = zk::client::connect("zk://zookeeper-1:2181").get();

    zk::future<zk::get_result> resp = client.get("/slsfs/proxy/"s + child);
    zk::buffer const buf = resp.get().data();

    auto const colon = std::find(buf.begin(), buf.end(), ':');

    std::string host(std::distance(buf.begin(), colon), '\0');
    std::copy(buf.begin(), colon, host.begin());

    std::string port(std::distance(std::next(colon), buf.end()), '\0');
    std::copy(std::next(colon), buf.end(), port.begin());

    boost::asio::ip::tcp::resolver resolver(io_context);
    BOOST_LOG_TRIVIAL(debug) << "resolving: " << host << ":" << port ;
    return resolver.resolve(host, port);
}

auto setup_slsfs(boost::asio::io_context &io_context) -> std::pair<std::vector<slsfs::uuid::uuid>, std::vector<boost::asio::ip::tcp::resolver::results_type>>
{
    zk::client client = zk::client::connect("zk://zookeeper-1:2181").get();

    std::vector<slsfs::uuid::uuid> new_proxy_list;
    std::vector<boost::asio::ip::tcp::resolver::results_type> proxys;
    zk::future<zk::get_children_result> children = client.get_children("/slsfs/proxy");

    std::vector<std::string> list = children.get().children();

    std::transform (list.begin(), list.end(),
                    std::back_inserter(new_proxy_list),
                    slsfs::uuid::decode_base64);

    std::sort(new_proxy_list.begin(), new_proxy_list.end());

    for (auto proxy : new_proxy_list)
        proxys.push_back(get_uuid(io_context, proxy.encode_base64()));

    BOOST_LOG_TRIVIAL(debug) << "start with " << proxys.size()  << " proxies";
    return {new_proxy_list, proxys};
}

int pick_proxy(std::vector<slsfs::uuid::uuid> const& proxy_uuid, slsfs::pack::key_t& fileid)
{
    auto it = std::upper_bound (proxy_uuid.begin(), proxy_uuid.end(), fileid);
    if (it == proxy_uuid.end())
        it = proxy_uuid.begin();

    int d = std::distance(proxy_uuid.begin(), it);
    return d;
}

auto iotest (int const times, int const bufsize,
             std::vector<int> rwdist,
             std::function<slsfs::pack::key_t(void)> genname,
             std::function<int(int)> genpos,
             std::pair<std::vector<slsfs::uuid::uuid>, std::vector<boost::asio::ip::tcp::resolver::results_type>> const proxylistpair,
             std::string const memo = "")
    -> std::pair<std::list<double>, std::chrono::nanoseconds>
{
    auto&& [proxy_uuid, proxy_endpoint] = proxylistpair;

    std::vector<tcp::socket> proxy_sockets;
    std::vector<std::list<double>> records(proxy_endpoint.size());
    std::vector<boost::asio::io_context> io_context_list(proxy_endpoint.size());

    for (unsigned int i = 0; i < proxy_endpoint.size(); i++)
    {
        proxy_sockets.emplace_back(io_context_list.at(i));
        BOOST_LOG_TRIVIAL(debug) << "connecting ";
        boost::asio::connect(proxy_sockets.back(), proxy_endpoint.at(i));
    }

    std::string const buf(bufsize, 'A');

    std::uniform_int_distribution<> dist(0, rwdist.size()-1);
    std::mt19937 engine(std::random_device{}());

    for (int i = 0; i < times; i++)
    {
        slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();

        ptr->header.type = slsfs::pack::msg_t::trigger;
        ptr->header.key = genname();

        slsfs::jsre::request r;
        r.type = slsfs::jsre::type_t::file;
        if (rwdist.at(dist(engine)))
        {
            BOOST_LOG_TRIVIAL(trace) << "r.operation = slsfs::jsre::operation_t::read;";
            r.operation = slsfs::jsre::operation_t::read;
        }
        else
        {
            BOOST_LOG_TRIVIAL(trace) << "r.operation = slsfs::jsre::operation_t::write;";
            r.operation = slsfs::jsre::operation_t::write;
        }

        r.position = genpos(i);
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
        BOOST_LOG_TRIVIAL(debug) << "sending " << ptr->header;
        auto pbuf = ptr->serialize();
        int index = pick_proxy(proxy_uuid, ptr->header.key);

        tcp::socket& s = proxy_sockets.at(index);
        boost::asio::io_context& io = io_context_list.at(index);
        std::list<double>& record_list = records.at(index);

        boost::asio::post(
            io,
            [&s, &record_list, pbuf, op=r.operation] {
                record_list.push_back(record(
                    [&s, pbuf, op] () {
                        boost::asio::write(s, boost::asio::buffer(pbuf->data(), pbuf->size()));

                        slsfs::pack::packet_pointer resp = std::make_shared<slsfs::pack::packet>();
                        std::vector<slsfs::pack::unit_t> headerbuf(slsfs::pack::packet_header::bytesize);
                        boost::asio::read(s, boost::asio::buffer(headerbuf.data(), headerbuf.size()));

                        resp->header.parse(headerbuf.data());
                        BOOST_LOG_TRIVIAL(debug) << "write resp:" << resp->header;

                        std::string data(resp->header.datasize, '\0');
                        boost::asio::read(s, boost::asio::buffer(data.data(), data.size()));
                        BOOST_LOG_TRIVIAL(debug) << data ;
                    }));
            });
    }

    std::vector<std::thread> ths;

    auto start = std::chrono::high_resolution_clock::now();
    for (unsigned int i = 0; i < io_context_list.size(); i++)
        ths.emplace_back(
            [&io_context_list, i] () {
                io_context_list.at(i).run();
            });

    for (std::thread& th : ths)
        th.join();
    auto end = std::chrono::high_resolution_clock::now();


    std::list<double> v;
    for (std::list<double>& r : records)
        v.insert(v.end(), r.begin(), r.end());
    stats(v.begin(), v.end(), fmt::format("write {}", memo));

    return {v, end - start};
}

template<typename TestFunc>
void start_test(std::string const testname, boost::program_options::variables_map& vm, TestFunc && testfunc)
{
    int const total_times   = vm["total-times"].as<int>();
    int const total_clients = vm["total-clients"].as<int>();
    int const bufsize       = vm["bufsize"].as<int>();
    double const zipf_alpha = vm["zipf-alpha"].as<double>();
    int const file_range    = vm["file-range"].as<int>();
    std::string const resultfile = vm["result"].as<std::string>();
    std::string const result_filename = resultfile + "-" + testname + ".csv";

    std::vector<std::future<std::pair<std::list<double>, std::chrono::nanoseconds>>> results;
    std::list<double> fullstat;
    for (int i = 0; i < total_clients; i++)
        results.emplace_back(std::invoke(testfunc));

    boost::filesystem::remove(result_filename);
    std::ofstream out_csv {result_filename, std::ios_base::app};

    out_csv << std::fixed << testname;
    out_csv << ",summary,";

    for (int i = 0; i < total_clients; i++)
        out_csv << ",client" << i;

    out_csv << "\n";

    std::vector<std::list<double>> gathered_results;
    std::vector<std::chrono::nanoseconds> gathered_durations;

    BOOST_LOG_TRIVIAL(info) << "Start. save: " << result_filename;
    for (auto it = results.begin(); it != results.end(); ++it)
    {
        auto && [result, duration] = it->get();
        gathered_results.push_back(result);
        gathered_durations.push_back(duration);

        fullstat.insert(fullstat.end(), std::next(result.begin()), result.end());
    }

    auto [dist, avg, stdev] = stats(fullstat.begin(), fullstat.end());
    for (unsigned int row = 0; row < static_cast<unsigned int>(total_times); row++)
    {
        if (row <= gathered_durations.size() && row != 0)
            out_csv << *std::next(gathered_durations.begin(), row-1);

        switch (row)
        {
        case 0:
            out_csv << ",,";
            break;
        case 1:
            out_csv << ",avg," << avg;
            break;
        case 2:
            out_csv << ",stdev," << stdev;
            break;
        case 3:
            out_csv << ",bufsize," << bufsize;
            break;
        case 4:
            out_csv << ",zipf-alpha," << zipf_alpha;
            break;
        case 5:
            out_csv << ",file-range," << file_range;
            break;

        case 6:
            out_csv << ",,";
            break;

        case 7:
            out_csv << ",dist(ms) bucket,";
            break;

        default:
        {
            int distindex = static_cast<int>(row) - 8;
            if (0 <= distindex && static_cast<unsigned int>(distindex) < dist.size())
                out_csv << "," << std::next(dist.begin(), distindex)->first
                        << "," << std::next(dist.begin(), distindex)->second;
            else
                out_csv << ",,";
        }

        }

        for (std::list<double>& list : gathered_results)
        {
            auto it = list.begin();
            out_csv << "," << *std::next(it, row);
        }
        out_csv << "\n";
    }
    BOOST_LOG_TRIVIAL(info) << "written result to " << result_filename;
}

int main(int argc, char *argv[])
{
    slsfs::basic::init_log();
#ifdef NDEBUG
    boost::log::core::get()->set_filter(
        boost::log::trivial::severity >= boost::log::trivial::info);
    constexpr static ZooLogLevel loglevel = ZOO_LOG_LEVEL_ERROR;
#else
    boost::log::core::get()->set_filter(
        boost::log::trivial::severity >= boost::log::trivial::trace);
    constexpr static ZooLogLevel loglevel = ZOO_LOG_LEVEL_INFO;
#endif // NDEBUG
    ::zoo_set_debug_level(loglevel);

    namespace po = boost::program_options;
    po::options_description desc{"Options"};
    desc.add_options()
        ("help,h", "Print this help messages")
        ("total-times",   po::value<int>()->default_value(10000),     "each client run # total times")
        ("total-clients", po::value<int>()->default_value(1),         "# of clients")
        ("bufsize",       po::value<int>()->default_value(4096),      "Size of the read/write buffer")
        ("zipf-alpha",    po::value<double>()->default_value(1.2),    "set the alpha value of zipf dist")
        ("file-range",    po::value<int>()->default_value(256*256*4), "set the total different number of files")
        ("test-name",     po::value<std::string>(),                   "oneof [fill, 50-50, 95-5, 100-0, 0-100]; format: read-write")
        ("uniform-dist",  po::bool_switch(),                          "use uniform distribution")
        ("result",        po::value<std::string>()->default_value("/dev/null"), "save result to this file");

    po::positional_options_description pos_po;
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv)
              .options(desc)
              .positional(pos_po).run(), vm);
    po::notify(vm);

    std::mt19937 engine(19937);

    int const total_times   = vm["total-times"].as<int>();
    int const bufsize       = vm["bufsize"].as<int>();
    double const zipf_alpha = vm["zipf-alpha"].as<double>();
    int const file_range    = vm["file-range"].as<int>();
    bool const use_uniform  = vm["uniform-dist"].as<bool>();

    std::string const test_name  = vm["test-name"].as<std::string>();
    std::string const resultfile = vm["result"].as<std::string>();

    absl::zipf_distribution namedist(file_range, zipf_alpha);
    std::uniform_int_distribution<> uniformdist(0, file_range);

    std::function<slsfs::pack::key_t(void)> selected;
    if (use_uniform)
        selected = [&engine, &uniformdist]() {
            slsfs::pack::key_t key{};
            for (std::size_t i = 0; i < key.size(); i++)
                key.at(i) = static_cast<slsfs::pack::unit_t>(uniformdist(engine));
            return key;
        };
    else
        selected = [&engine, &namedist]() {
            int const pick = namedist(engine);
            slsfs::pack::key_t k{};
            std::memcpy(k.data(), &pick, sizeof(pick));
            return k;
        };

    int counter = 0;
    auto allname =
        [&counter] () {
            int pick = counter++;
            slsfs::pack::key_t k{};
            std::memcpy(k.data(), &pick, sizeof(pick));
            return k;
        };

    absl::zipf_distribution singledist(255, zipf_alpha);
    auto anyname = [&engine, &singledist]() {
            slsfs::pack::key_t t{};
            for (slsfs::pack::unit_t& n : t)
                n = singledist(engine);
            return t;
        };

    boost::asio::io_context io_context;
    auto&& proxylistpair = setup_slsfs(io_context);


    switch (slsfs::basic::sswitcher::hash(test_name))
    {
        using namespace slsfs::basic::sswitcher;
    case "fill"_:
        start_test(
            "fill",
            vm,
            [&]() {
                return std::async(
                    std::launch::async,
                    iotest, file_range, bufsize,
                    std::vector<int> {1},
                    allname,
                    [bufsize](int) { return 0; },
                    proxylistpair,
                    "");
        });
        break;
    case "create"_:
        start_test(
            "create",
            vm,
            [&]() {
                return std::async(
                    std::launch::async,
                    iotest, file_range, bufsize,
                    std::vector<int> {1},
                    anyname,
                    [bufsize](int) { return 0; },
                    proxylistpair,
                    "");
        });
        break;
    case "50-50"_:
        start_test(
            "50-50",
            vm,
            [&]() {
                return std::async(
                    std::launch::async,
                    iotest, total_times, bufsize,
                    std::vector<int> {0, 1},
                    selected,
                    [bufsize](int) { return 0; },
                    proxylistpair,
                    "");
        });
        break;
    case "95-5"_:
        start_test(
            "95-5",
            vm,
            [&]() {
                return std::async(
                    std::launch::async,
                    iotest, total_times, bufsize,
                    std::vector<int>{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
                    selected,
                    [bufsize](int) { return 0; },
                    proxylistpair,
                    "");
        });
        break;
    case "100-0"_:
        start_test(
            "100-0",
            vm,
            [&]() {
                return std::async(
                    std::launch::async,
                    iotest, total_times, bufsize,
                    std::vector<int> {0},
                    selected,
                    [bufsize](int) { return 0; },
                    proxylistpair,
                    "");
        });
        break;
    case "0-100"_:
        start_test(
            "0-100",
            vm,
            [&]() {
                return std::async(
                    std::launch::async,
                    iotest, total_times, bufsize,
                    std::vector<int> {1},
                    selected,
                    [bufsize](int) { return 0; },
                    proxylistpair,
                    "");
        });
        break;

    default:
        BOOST_LOG_TRIVIAL(error) << "unknown test name << " << test_name;
    }

    return EXIT_SUCCESS;
}
