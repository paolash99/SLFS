#include "../basic.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <iostream>
#include <memory>
#include <thread>
#include <sstream>
#include <cstring>
#include <ctime>
#include <functional>
#include <random>

#include <fmt/core.h>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>

#include <absl/random/random.h>
#include <absl/random/zipf_distribution.h>

#include <algorithm>
#include <iostream>
#include <fstream>
#include <memory>
#include <array>
#include <list>
#include <thread>
#include <vector>
#include <random>
#include <chrono>


int constexpr alignment = 512;

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
    std::cout << fmt::format("{0} avg={1:.3f} sd={2:.3f}", memo, mean, std::sqrt(var));
    for (auto && [time, count] : dist)
        std::cout << fmt::format("{0} {1}: {2}", memo, time, count);

    return {dist, mean, std::sqrt(var)};
}


auto iotest (int const times, int const bufsize,
             std::vector<int> rwdist,
             std::function<std::string(void)> genname,
             std::function<int(int)> genpos, std::string const = "")
    -> std::pair<std::list<double>, std::chrono::nanoseconds>
{
    std::list<double> records;
    std::mt19937 engine(19937);
    std::uniform_int_distribution<> dist(0, rwdist.size()-1);

    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < times; i++)
    {
        struct stat statbuf;

        std::string fullpath = genname();

        int fd = open(fullpath.c_str(),  O_CREAT | O_RDWR | O_DIRECT, 0666); // int fd = open(fullpath.c_str(), O_CREAT | O_RDWR, 0666); // O_SYNC  |
        //int fd = open(fullpath.c_str(), O_RDWR, 0666); // int fd = open(fullpath.c_str(), O_CREAT | O_RDWR, 0666); // O_SYNC  |
        if (fd < 0)
            BOOST_LOG_TRIVIAL(error) << fullpath << " error getting fd: " << std::strerror(errno) << "\n";
        SCOPE_DEFER([fd]{ close(fd); });

        void *buffer = std::aligned_alloc(alignment, bufsize);
        SCOPE_DEFER([buffer] { std::free(buffer); });

        fstat(fd, &statbuf);

        //auto size = statbuf.st_size;

//        continue;
//        BOOST_LOG_TRIVIAL(info) << "open with size: " << size << "\n";

        lseek(fd, genpos(i), SEEK_SET);

        records.push_back(
            record([&]() {
                       if (!rwdist.at(dist(engine)))
                       {
                           int bytes_written = read(fd, buffer, bufsize);
                           if (bytes_written < 0)
                               BOOST_LOG_TRIVIAL(error) << "Error with read: " << std::strerror(errno);
                           else if (bytes_written == 0)
                               BOOST_LOG_TRIVIAL(warning) << "Read 0 bytes: " << fullpath;
                       }
                       else
                       {
                           int bytes_written = write(fd, buffer, bufsize);
                           if (bytes_written <= 0)
                               BOOST_LOG_TRIVIAL(error) << "Error with write: " << std::strerror(errno);
                       }
                   }));
    }

    auto end = std::chrono::high_resolution_clock::now();
    return {records, end - start};
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
        ("basename",     po::value<std::string>(),                 "oneof [fill, 50-50, 95-5, 100-0, 0-100]; format: read-write")
        ("uniform-dist",  po::bool_switch(),                        "use uniform distribution")
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

    int const total_times   = vm["total-times"].as<int>();
    int const bufsize       = vm["bufsize"].as<int>();
    double const zipf_alpha = vm["zipf-alpha"].as<double>();
    int const file_range    = vm["file-range"].as<int>();
    bool const use_uniform  = vm["uniform-dist"].as<bool>();

    std::string const test_name  = vm["test-name"].as<std::string>();
    std::string const resultfile = vm["result"].as<std::string>();
    std::string const basename   = vm["basename"].as<std::string>();
    std::string const alphalist  = "0123456789abcdef";

    absl::zipf_distribution namedist(15, zipf_alpha);
    std::uniform_int_distribution<> uniformdist(0, 15);

    std::function<std::string(void)> selected;
    if (use_uniform)
        selected = [&engine, &uniformdist, basename, alphalist]() {
            std::string name;
            for (int i=0; i<4; i++)
                name += alphalist[uniformdist(engine)];

            return basename + name;
        };
    else
        selected = [&engine, &namedist, basename, alphalist]() {
            std::string name;
            for (int i=0; i<4; i++)
                name += alphalist[namedist(engine)];
            return basename + name;
        };

    int counter = 0;
    auto allname =
        [&counter, basename] () {
            std::string result = fmt::format("{:04x}", counter);
            counter++;
            return basename + result;
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
                    "");
        });
        break;

    default:
        BOOST_LOG_TRIVIAL(error) << "unknown test name << " << test_name;
    }

    return EXIT_SUCCESS;
}
