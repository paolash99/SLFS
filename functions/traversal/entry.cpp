
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

void do_datafunction(std::ostream &ow_out)
try
{
    boost::asio::io_context ioc;

    json input;

    // cannot use '\n' because '\n' does not flush the stream
    ow_out << "{\"nya\": \"halo\"}" << std::endl;
    std::cin >> input;

#ifdef AS_ACTIONLOOP
    input = input["value"];
#endif

    std::vector<std::thread> threadpool;
    unsigned int const worker = std::min<unsigned int>(4, std::thread::hardware_concurrency());
    threadpool.reserve(worker);
    for (unsigned int i = 0; i < worker; i++)
        threadpool.emplace_back([&ioc] { ioc.run(); });

    for (std::thread& th : threadpool)
        th.join();
}
catch (std::exception const & e)
{
    slsfs::log::log(std::string("exception thrown ") + e.what());
    std::cerr << "do function ecxception: " << e.what() << std::endl;
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
        slsfs::log::log("starting as action loop");
        slsfsdf::do_datafunction(ow_out);
    }
    return 0;
#else
    std::cerr << "starting as normal" << std::endl;
    slsfsdf::do_datafunction(std::cout);
    return 0;
    //slsfs::log::push_logs();
#endif
}
