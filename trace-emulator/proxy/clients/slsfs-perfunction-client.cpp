#include "../basic.hpp"
#include "../serializer.hpp"
#include "../uuid.hpp"
#include "../base64-conv.hpp"
#include "../trigger.hpp"

#include <fmt/core.h>
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <absl/random/random.h>
#include <absl/random/zipf_distribution.h>
#include <zk/client.hpp>
#include <zk/results.hpp>
#include <zookeeper/zookeeper.h>
#include <nlohmann/json.hpp>

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

template<typename Next>
void send_one_request (int count,
                       boost::asio::io_context &io,
                       std::vector<int> rwdist,
                       std::function<slsfs::pack::key_t(void)> genname,
                       std::string const &data,
                       std::list<double> &result,
                       Next next)
{
    if (count < 0)
    {
        std::invoke(next);
        return;
    }

    nlohmann::json request = R"(
          {
            "type": "",
            "launch": "direct",
            "name": "",
            "pos": 0,
            "data": "",
            "size": 0,
            "blocksize":  4096,
            "storagetype": "ssbd",
            "storageconfig": {
                "hosts": [
                    {"host": "192.168.0.66",  "port": "12000"},
                    {"host": "192.168.0.111", "port": "12000"},
                    {"host": "192.168.0.45",  "port": "12000"},
                    {"host": "192.168.0.147", "port": "12000"},
                    {"host": "192.168.0.98",  "port": "12000"},
                    {"host": "192.168.0.159", "port": "12000"},
                    {"host": "192.168.0.175", "port": "12000"},
                    {"host": "192.168.0.75",  "port": "12000"},
                    {"host": "192.168.0.158", "port": "12000"}
                ],
                "replication_size": 3
            }
          }
        )"_json;

    std::uniform_int_distribution<> dist(0, rwdist.size()-1);
    std::mt19937 engine(std::random_device{}());

    auto key = genname();
    bool is_read_request = (rwdist.at(dist(engine)) == 0);

    request["type"] = is_read_request? "read" : "write";
    request["name"] = (static_cast<slsfs::uuid::uuid*>(std::addressof(key))->encode_base64());
    if (is_read_request)
        request["data"] = "";
    else
        request["data"] = slsfs::base64::encode(data.begin(), data.end());
    request["size"] = data.size();

    auto const start = std::chrono::high_resolution_clock::now();

    static std::random_device rd;
    static std::mt19937 rng(rd());
    static std::uniform_int_distribution<> distx(0, 15);

    std::string const url = fmt::format("https://ow-ctrl/api/v1/namespaces/_/actions/slsfs-datafunction-{}?blocking=true&result=false", distx(rng));
    slsfs::trigger::make_trigger (io, url)
        ->register_on_read(
            [count, &io, rwdist, genname, &data, &result, start, next]
            (std::shared_ptr<slsfs::http::response<slsfs::http::string_body>> res) {
                auto const end = std::chrono::high_resolution_clock::now();
                auto relativetime = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                result.push_back(relativetime);

                try
                {
                    auto resp = nlohmann::json::parse(res->body());

                    std::string from = resp["response"]["result"]["data"].get<std::string>(), stringresp;
                    slsfs::base64::decode(from, std::back_inserter(stringresp));

                    BOOST_LOG_TRIVIAL(debug) << "response: " << resp["activationId"] << " result: " << stringresp;
                    send_one_request(count-1, io, rwdist, genname, data, result, next);
                }
                catch (...)
                {
                    // stop sending because of error
                    BOOST_LOG_TRIVIAL(error) << "response: " << res->body();
                    std::invoke(next);
                }
            })
        .start_post(request.dump());
}

auto iotest (int const times, int const bufsize,
             std::vector<int> rwdist,
             std::function<slsfs::pack::key_t(void)> genname,
             [[maybe_unused]] std::function<int(int)> genpos,
             int clients,
             std::string const memo = "")
    -> std::pair<std::list<double>, std::chrono::nanoseconds>
{
    std::vector<boost::asio::io_context> io_context_list(clients);
    std::vector<std::list<double>> records(clients);

    std::string buf(bufsize, 'A');

    for (int i = 0; i < clients; i++)
        send_one_request (times,
                          io_context_list.at(i),
                          rwdist,
                          genname,
                          buf,
                          records.at(i),
                          [&io_context_list, i] () {
                              io_context_list.at(i).stop();
                          });

    std::vector<std::thread> pool;
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < clients; i++)
        pool.emplace_back(
            [&io_context_list, i] () {
                io_context_list.at(i).run();
            });

    for (std::thread& th : pool)
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
        ("total-times",   po::value<int>()->default_value(10000),   "each client run # total times")
        ("total-clients", po::value<int>()->default_value(1),       "# of clients")
        ("bufsize",       po::value<int>()->default_value(4096),    "Size of the read/write buffer")
        ("zipf-alpha",    po::value<double>()->default_value(1.2),  "set the alpha value of zipf dist")
        ("file-range",    po::value<int>()->default_value(256*256), "set the total different number of files")
        ("test-name",     po::value<std::string>(),                 "oneof [fill, 50-50, 95-5, 100-0, 0-100]; format: read-write")
        ("uniform-dist",  po::bool_switch(),                        "use uniform distribution")
        ("result",        po::value<std::string>()->default_value("/dev/null"), "save result to this file");

    po::positional_options_description pos_po;
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv)
              .options(desc)
              .positional(pos_po).run(), vm);
    po::notify(vm);

    std::mt19937 engine(19937);

    int const total_times   = vm["total-times"].as<int>();
    int const total_clients = vm["total-clients"].as<int>();
    int const bufsize       = vm["bufsize"].as<int>();
    int const file_range    = vm["file-range"].as<int>();
    bool const use_uniform  = vm["uniform-dist"].as<bool>();
    double const zipf_alpha = vm["zipf-alpha"].as<double>();

    std::string const test_name  = vm["test-name"].as<std::string>();
    std::string const resultfile = vm["result"].as<std::string>();

    absl::zipf_distribution namedist(file_range, zipf_alpha);
    std::uniform_int_distribution<> uniformdist(0, file_range);

    std::function<slsfs::pack::key_t(void)> selected;
    if (use_uniform)
        selected = [&engine, &uniformdist]() {
            slsfs::pack::key_t key;
            for (std::size_t i = 0; i < key.size(); i++)
                key.at(i) = static_cast<slsfs::pack::unit_t>(uniformdist(engine));
            return key;
        };
    else
        selected = [&engine, &namedist]() {
            int n = namedist(engine);
            slsfs::pack::unit_t n1 = n / 256;
            slsfs::pack::unit_t n2 = n % 256;
            return slsfs::pack::key_t {
                n1, n2, 7, 8, 7, 8, 7, 8,
                7, 8, 7, 8, 7, 8, 7, 8,
                7, 8, 7, 8, 7, 8, 7, 8,
                7, 8, 7, 8, 7, 8, n1, n2
            };
        };

    int counter = 0;
    auto allname =
        [&counter] () {
            int n = counter++;
            slsfs::pack::unit_t n1 = n / 256;
            slsfs::pack::unit_t n2 = n % 256;
            return slsfs::pack::key_t {
                n1, n2, 7, 8, 7, 8, 7, 8,
                7, 8, 7, 8, 7, 8, 7, 8,
                7, 8, 7, 8, 7, 8, 7, 8,
                7, 8, 7, 8, 7, 8, n1, n2
            };
        };

    absl::zipf_distribution singledist(255, zipf_alpha);
    auto anyname = [&engine, &singledist]() {
            slsfs::pack::key_t t{};
            for (slsfs::pack::unit_t& n : t)
                n = singledist(engine);
            return t;
        };

    using namespace slsfs::basic::sswitcher;
    switch (slsfs::basic::sswitcher::hash(test_name))
    {
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
                    total_clients,
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
                    total_clients,
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
                    total_clients,
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
                    total_clients,
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
                    total_clients,
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
                    total_clients,
                    "");
        });
        break;

    default:
        BOOST_LOG_TRIVIAL(error) << "unknown test name << " << test_name;
    }

    return EXIT_SUCCESS;
}
