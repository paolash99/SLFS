
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>

#include <iostream>
#include <memory>
#include <thread>
#include <sstream>
#include <map>
#include <ctime>

int main(int argc, char *argv[])
{
    //std::uint32_t version = std::time(nullptr);//const auto p1 = std::chrono::system_clock::now();;
    //SCOPE_DEFER([] { slsfs::log::push_logs(); });

    namespace io = boost::iostreams;

    io::stream_buffer<io::file_descriptor_sink> fpstream(3, io::close_handle);
    std::ostream ow_out {&fpstream};
    while (true)
    {
        //std::cerr << "starting as action loop" << std::endl;
        //records.push_back(record([&](){ slsfsdf::do_datafunction(ow_out); }));
        ow_out << "{\"hello\": \"world\"}" << std::endl;
        //int error = slsfsdf::do_datafunction(ow_out);
        //if (error != 0)
        //    return error;
    }
    return 0;
}
