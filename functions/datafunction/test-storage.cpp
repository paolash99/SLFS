
#include "worker.hpp"
#include "storage-conf.hpp"
#include "storage-conf-cass.hpp"
#include "storage-conf-swift.hpp"
#include "storage-conf-ssbd-backend.hpp"
#include "proxy-command.hpp"
#include "uuid.hpp"
#include "create-request.hpp"

#include <slsfs.hpp>

#include <oneapi/tbb/concurrent_hash_map.h>
#include <oneapi/tbb/concurrent_queue.h>

#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/asio.hpp>

#include <Poco/SHA2Engine.h>
#include <Poco/DigestStream.h>

#include <iostream>
#include <memory>
#include <thread>
#include <sstream>
#include <map>
#include <ctime>

int constexpr requestsize = 4096;

auto get_uuid(std::string const& buffer) -> slsfs::pack::key_t
{
    slsfs::pack::key_t k;
    Poco::SHA2Engine eng;
    Poco::DigestOutputStream outstr(eng);
    outstr << buffer;
    outstr.flush();

    Poco::DigestEngine::Digest const& digest = eng.digest();
    std::memcpy(k.data(), digest.data(), digest.size());
    return k;
}

template<typename Iterator>
void stats(Iterator start, Iterator end, std::string const memo = "")
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
    std::cout << fmt::format("{0} avg={1:.3f} sd={2:.3f}", memo, mean, std::sqrt(var)) << "\n";
    for (auto && [time, count] : dist)
        std::cout << fmt::format("{0} {1}: {2}", memo, time, count) << "\n";
}

namespace slsfsdf
{

using boost::asio::ip::tcp;

void start_send_request(std::shared_ptr<slsfsdf::storage_conf> conf,
                        slsfs::pack::packet_pointer ptr,
                        std::atomic<int> &outstanding_requests,
                        oneapi::tbb::concurrent_vector<std::uint64_t> &result)
{
    slsfs::jsre::request_parser<slsfs::base::byte> input {ptr};

    auto const start = std::chrono::system_clock::now();
    conf->start_perform(
        input,
        [input, start, conf, &result, &outstanding_requests]
        (slsfs::base::buf buf) {
            auto const end = std::chrono::system_clock::now();
            auto relativetime = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            result.push_back(relativetime);

            slsfs::log::log<slsfs::log::level::info>("request finished");
            --outstanding_requests;
        });
}


template<typename NextFunc>
void start_send_request_sequence (std::shared_ptr<slsfsdf::storage_conf> conf,
                                  slsfs::pack::packet_pointer ptr,
                                  NextFunc && next)
{
    slsfs::jsre::request_parser<slsfs::base::byte> input {ptr};

    auto const start = std::chrono::system_clock::now();
    conf->start_perform(
        input,
        [input, start, conf, next]
        (slsfs::base::buf buf) {
            auto const end = std::chrono::system_clock::now();
            auto relativetime = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();

            std::invoke(next, relativetime);
        });
}

template<typename Finish>
void send_one_request (int left_request,
                       std::shared_ptr<slsfsdf::storage_conf> conf,
                       oneapi::tbb::concurrent_vector<std::uint64_t>& result_vector,
                       Finish && on_finish)
{
    static std::string buf(4096, 'A');
    static std::mt19937 engine (19937);
    std::uniform_int_distribution<std::uint8_t> dist{0, 255};

    auto ptr = client_request::create_write(slsfs::pack::key_t{dist(engine)}, 0, buf);

    start_send_request_sequence (
        conf, ptr,
        [left_request=left_request-1, conf, &result_vector, &on_finish]
        (std::uint64_t duration) {
            result_vector.push_back(duration);
            slsfs::log::log<slsfs::log::level::info>("send_one_request left: {}", left_request);
            if (left_request == 0)
            {
                std::invoke (on_finish, conf, result_vector);
                return;
            }
            else if (left_request > 0)
                send_one_request (left_request, conf, result_vector, on_finish);
        });
}

int do_datafunction (int const single_requests)
{
    int const concurrent_clients = 1;
    boost::asio::io_context ioc;
    tcp::resolver resolver(ioc);

    boost::asio::steady_timer timer(ioc);
    timer.expires_from_now(std::chrono::seconds(2));
    timer.async_wait([] (boost::system::error_code) {});

    using json = slsfs::base::json;
    json input;
    std::cin >> input;

    std::vector<std::thread> workers;
    unsigned int const worker = std::min<unsigned int>(4, std::thread::hardware_concurrency());
    workers.reserve(worker);
    for (unsigned int i = 0; i < worker; i++)
        workers.emplace_back([&ioc] { ioc.run(); });

    std::shared_ptr<slsfsdf::storage_conf> conf = nullptr;

    std::string const storagetype = input["storagetype"].get<std::string>();
    switch (slsfs::sswitch::hash(storagetype))
    {
        using namespace slsfs::sswitch;
    case "ssbd"_:
        conf = std::make_shared<slsfsdf::storage_conf_ssbd_backend>(ioc);
        break;
    case "cassandra"_:
        conf = std::make_shared<slsfsdf::storage_conf_cass>();
        break;
    case "swift"_:
        conf = std::make_shared<slsfsdf::storage_conf_swift>();
        break;
    }

    conf->init(input["storageconfig"]);

    slsfs::log::log<slsfs::log::level::info>("starting");
    oneapi::tbb::concurrent_vector<std::uint64_t> result_vector;

    if (conf->use_async())
    {
        auto const start = std::chrono::steady_clock::now();
        for (int i = 0; i < concurrent_clients; i++)
        {
            slsfs::log::log<slsfs::log::level::info>("starting client {}", i);
            send_one_request(
                single_requests,
                conf,
                result_vector,
                [start, single_requests]
                (std::shared_ptr<slsfsdf::storage_conf> conf,
                 oneapi::tbb::concurrent_vector<std::uint64_t>& result) {
                    auto const end = std::chrono::steady_clock::now();
                    std::uint64_t relativetime = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
                    stats(result.begin(), result.end());
                    std::cout << " time: " << relativetime << "\n";
                    std::cout << " iops: " << std::fixed << single_requests / (relativetime / 1000.0) << "\n";
                    std::cout << " throughput: " << std::fixed << single_requests * requestsize / (relativetime / 1000.0) << " Bps\n";
                    conf->close();
                    std::exit(0);
                });
        }
    }
    else
    {
        auto const start = std::chrono::system_clock::now();
        oneapi::tbb::concurrent_vector<std::uint64_t> result_vector;
        for (int i = 0; i < single_requests; i++)
        {
            static std::string buf(4096, 'A');
            auto ptr = client_request::create_write(slsfs::pack::key_t{1}, 0, buf);

            auto const single_start = std::chrono::system_clock::now();
            conf->perform(ptr);
            auto const single_end = std::chrono::system_clock::now();

            auto relativetime = std::chrono::duration_cast<std::chrono::nanoseconds>(single_end - single_start).count();
            result_vector.push_back(relativetime);
        }

        auto const end = std::chrono::system_clock::now();
        auto relativetime = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        std::cout << " time: " << relativetime << "\n";
        std::cout << " iops: " << std::fixed << 100000.0 / (relativetime / 1000000000.0) << "\n";
        std::cout << " throughput: " << std::fixed << 100000.0 * requestsize / (relativetime / 1000000000.0) << " bps\n";

        stats(result_vector.begin(), result_vector.end());
    }

    for (std::thread& th : workers)
        th.join();

    return 0;
}

} // namespace slsfsdf

int main(int argc, char *argv[])
{
    std::string name = fmt::format("DF:{0:4d}", slsfs::uuid::gen_rand_number());
    char const* name_cstr = name.c_str();
    slsfs::log::init(name_cstr);

    return slsfsdf::do_datafunction(std::stoi(argv[1]));
}
