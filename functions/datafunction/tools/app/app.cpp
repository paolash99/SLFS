
//#include <slsfs.hpp>

#include <iostream>
#include <memory>
#include <sstream>
#include <thread>

int main(int argc, char *argv[])
{
    std::cout << "{\"hello\": \"world\"}" << std::endl;
    return 0;
//    char const * name = "app";
//    slsfs::log::init(name);
//    slsfs::log::logstring("app start");
//
//    using json = slsfs::base::json;
//    json input;
//    std::cin >> input;
//
//    json output;
//    SCOPE_DEFER([&output]{
//                    std::cout << output.dump() << "\n";
//                    std::cerr << output.dump() << "\n";
//                });
//    output["response"] = "ok";
//
//    auto const data = input["data"].get<std::string>();
//    using namespace std::chrono_literals;
//
//    for (int i = 0; i < 100; i++)
//    {
//        std::string const target = "/restaurant/burgerking-" + std::to_string(i) + ".txt";
//        char const* path = target.c_str();
//        slsfs::log::logstring<slsfs::log::level::info>("write /r/b");
//        slsfs::write(path, data.data(), data.size(), 0, nullptr);
//        slsfs::log::logstring<slsfs::log::level::info>("write /r/b");
//
//        {
//            std::array<char, 512> readbuf;
//
//            slsfs::log::logstring<slsfs::log::level::info>("read /r/b");
//            std::size_t readsize = slsfs::read(path, readbuf.data(), readbuf.size(), 0, nullptr);
//            slsfs::log::logstring<slsfs::log::level::info>("read /r/b finish");
//
//            std::string readfile;
//            std::copy_n(readbuf.begin(), readsize, std::back_inserter(readfile));
//            output["read"] = readfile;
//        }
//    }
//
//    return 0;
}
