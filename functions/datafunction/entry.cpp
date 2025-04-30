
#include "worker.hpp"
#include "storage-conf.hpp"
#include "storage-conf-cass.hpp"
#include "storage-conf-swift.hpp"
#include "storage-conf-ssbd-backend.hpp"
#include "proxy-command.hpp"
#include "create-request.hpp"

#include <slsfs.hpp>

#include <oneapi/tbb/concurrent_hash_map.h>
#include <oneapi/tbb/concurrent_queue.h>

#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/asio.hpp>

#include <iostream>
#include <memory>
#include <thread>
#include <sstream>
#include <map>
#include <ctime>

namespace slsfsdf
{

using boost::asio::ip::tcp;
using json = slsfs::base::json;

int do_datafunction_with_proxy (
    boost::asio::io_context &ioc,
    std::ostream &ow_out,
    json const& input,
    std::shared_ptr<slsfsdf::storage_conf> conf)
{
    /* example json config
    {
        "type": "wakeup",
        "proxyhost": "192.168.1.1",
        "proxyport": "12000",
        "storagetype": "ssbd",
        "storageconfig": {
            "hosts": [{
                "host": "192.168.2.1",
                "port": "12000"
            }]
        }
    }
    */

    slsfsdf::server::queue_map queue_map;
    tcp::resolver resolver(ioc);
    slsfsdf::server::proxy_set proxys;

    bool const enable_cache = input["caching"].get<bool>();
    int  const cache_size = input["cachesize"].get<int>();
    std::string const cache_policy = input["cache-policy"].get<std::string>();
    std::string const proxyhost = input["proxyhost"].get<std::string>();
    std::string const proxyport = input["proxyport"].get<std::string>();
    slsfs::log::log("connect to {}:{}", proxyhost, proxyport);

    auto proxy_command_ptr = std::make_shared<slsfsdf::server::proxy_command>(
        ioc, conf, queue_map, proxys, 2000, enable_cache, cache_size, cache_policy);

    auto function_timeout = std::make_shared<boost::asio::steady_timer>(ioc);
    {
        using namespace std::chrono_literals;
        function_timeout->expires_from_now(298s);
        function_timeout->async_wait(
            [proxy_command_ptr, function_timeout] (boost::system::error_code ec) {
                slsfs::log::log<slsfs::log::level::info>("time to die. closing connection. ec={}", ec.message());
                proxy_command_ptr->close();
            });
    }

    boost::asio::ip::tcp::endpoint proxy = *resolver.resolve(proxyhost, proxyport);
    proxy_command_ptr->start_connect(proxy);
    proxys.emplace(proxy, proxy_command_ptr);

    json output;
    output["original-request"] = input;

    return 0;
}

int do_datafunction_direct (
    boost::asio::io_context &ioc,
    std::ostream &ow_out,
    json const& input,
    std::shared_ptr<slsfsdf::storage_conf> conf)
{
    /* example json config
    {
        "type": "write" | "read",
        "name": "base64encodedfilename",
        "pos": 0,
        "size": 0,
        "data": "blob",
    }
    return:
    {
        "data": "blob"
    }
    */

    std::string   const type    = input["type"].get<std::string>();
    std::string         name    = input["name"].get<std::string>();
    slsfs::uuid::uuid const key = slsfs::uuid::decode_base64(name);
    std::uint32_t const pos     = input["pos"].get<std::uint32_t>();
    std::uint32_t const size    = input["size"].get<std::uint32_t>();

    slsfs::pack::packet_pointer ptr = nullptr;

    switch (slsfs::sswitch::hash(type))
    {
    using namespace slsfs::sswitch;
    case "write"_:
    {
        std::string data = input["data"].get<std::string>();
        std::string buf;
        slsfs::base64::decode (data, std::back_inserter(buf));

        ptr = client_request::create_write(key, pos, buf);
        break;
    }
    case "read"_:
        ptr = client_request::create_read(key, pos, size);
        break;
    default:
        ow_out << "{}";
    }

    if (conf->use_async())
    {
        slsfs::jsre::request_parser<slsfs::base::byte> input {ptr};
        conf->start_perform(
            input,
            [ptr, input, conf, &ow_out, &ioc]
            (slsfs::base::buf buf) {
                json output_json;
                output_json["data"] = slsfs::base64::encode(buf.begin(), buf.end());
                ow_out << output_json;
                conf->close();
                ioc.stop();
            });
    }
    else
    {
        slsfs::jsre::request_parser<slsfs::base::byte> input {ptr};
        slsfs::base::buf buf = conf->perform(input);

        json output_json;
        output_json["data"] = slsfs::base64::encode(buf.begin(), buf.end());
        ow_out << output_json;
        conf->close();
    }
    return 0;
}

int do_datafunction(std::ostream &ow_out)
{
    json input;
    try
    {
        boost::asio::io_context ioc;

        // cannot use '\n' because '\n' does not flush the stream
        ow_out << "{\"nya\": \"halo\"}" << std::endl;
        std::cin >> input;

#ifdef AS_ACTIONLOOP
        input = input["value"];
#endif
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

        int returnvalue = 0;
        switch (slsfs::sswitch::hash(input["launch"].get<std::string>()))
        {
            using namespace slsfs::sswitch;
        case "direct"_:
            returnvalue = do_datafunction_direct(ioc, ow_out, input, conf);
            break;
        case "server"_:
            returnvalue = do_datafunction_with_proxy(ioc, ow_out, input, conf);
            break;
        default:
            slsfs::log::log("unsupported launch method: '{}'", input["launch"].get<std::string>());
        }
        std::vector<std::thread> threadpool;
        unsigned int const worker = std::min<unsigned int>(4, std::thread::hardware_concurrency());
        threadpool.reserve(worker);
        for (unsigned int i = 0; i < worker; i++)
            threadpool.emplace_back([&ioc] { ioc.run(); });

        for (std::thread& th : threadpool)
            th.join();

        return returnvalue;
    }
    catch (boost::exception const & e)
    {
        slsfs::log::log("boost exception thrown {}", boost::diagnostic_information(e));
        std::cerr << "input json " << input << std::endl;
        std::cerr << "do function exception: " << boost::diagnostic_information(e) << std::endl;
    }
    catch (std::exception const & e)
    {
        slsfs::log::log("std exception thrown {}", e.what());
        std::cerr << "input json " << input << std::endl;
        std::cerr << "do function exception: " << e.what() << std::endl;
    }

    return -1;
}

} // namespace slsfsdf

int main(int argc, char *argv[])
{
    std::string name = fmt::format("DF:{0:4d}", slsfs::uuid::gen_rand_number());
    char const* name_cstr = name.c_str();
    slsfs::log::init(name_cstr);
    slsfs::log::log("data function start");

#ifdef AS_ACTIONLOOP
    namespace io = boost::iostreams;

    io::stream_buffer<io::file_descriptor_sink> fpstream(3, io::close_handle);
    std::ostream ow_out {&fpstream};
    while (true)
    {
        slsfs::log::log<slsfs::log::level::info>("starting as action loop");
        int error = slsfsdf::do_datafunction(ow_out);
        if (error != 0)
            return error;
    }
    return 0;
#else
    slsfs::log::log<slsfs::log::level::info>("starting as normal");
    return slsfsdf::do_datafunction(std::cout);
    //slsfs::log::push_logs();
#endif
}
