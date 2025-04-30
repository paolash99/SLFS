#include "emulation_node.hpp"

#define BOOST_NO_CXX11_SCOPED_ENUMS
#include <boost/filesystem.hpp>
#undef BOOST_NO_CXX11_SCOPED_ENUMS

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>

#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <cmath>
#include <memory>

int constexpr alignment = 512;

using namespace std;

class direct_ceph_emulation_node : public emulation_node
{
    private:
        string active_dir;
    public:
        direct_ceph_emulation_node(string ceph_path) {
            active_dir = ceph_path;
        };

        int read_request(string filename, double bytes, shared_ptr<promise<string>> result){
            string parent_dir = filename.substr(0,3);
            string child_dir = filename.substr(3,1);
            string name = filename.substr(4);

            string path = active_dir + "/" + parent_dir + "/" + child_dir + "/" + name;

            auto const start = chrono::high_resolution_clock::now();
            int fd = open(path.c_str(),  O_RDWR | O_DIRECT, 0666); // int fd = open(fullpath.c_str(), O_CREAT | O_RDWR, 0666); // O_SYNC  |
            if (fd < 0){
                std::cerr << "error getting fd: " << std::strerror(errno) << "\n";
                return 1;
            }

            lseek(fd, 0, SEEK_SET);

            void *buffer = aligned_alloc(alignment, bytes);
            [[maybe_unused]]
            int bytes_read = read(fd, buffer, bytes);

            free(buffer);
            close(fd);

            auto const now = chrono::high_resolution_clock::now();
            auto relativetime = chrono::duration_cast<std::chrono::microseconds>(now - start).count();
            auto throughput = (bytes * 0.000001) / (relativetime * 0.000001);

            result->set_value("," + to_string(relativetime) + "," + to_string(lround(bytes)) + "," + "read" + "," + to_string(throughput));
            return 0;
        }

        int write_request(string filename, double bytes, shared_ptr<promise<string>> result) {

            string parent_dir = filename.substr(0,3);
            string child_dir = filename.substr(3,1);
            string name = filename.substr(4);

            string path = active_dir + "/" + parent_dir + "/" + child_dir + "/" + name;

            boost::filesystem::create_directories(active_dir + "/" + parent_dir + "/" + child_dir);

            auto const start = chrono::high_resolution_clock::now();
            int fd = open(path.c_str(),  O_CREAT | O_RDWR | O_DIRECT, 0666); // int fd = open(fullpath.c_str(), O_CREAT | O_RDWR, 0666); // O_SYNC  |
            if (fd < 0){
                std::cerr << "error getting fd: " << std::strerror(errno) << "\n";
                return 1;
            }

            lseek(fd, 0, SEEK_SET);

            void *buffer = aligned_alloc(alignment, bytes);
            [[maybe_unused]]
            int bytes_written = write(fd, buffer, bytes);

            free(buffer);
            close(fd);

            auto const now = chrono::high_resolution_clock::now();
            auto relativetime = chrono::duration_cast<std::chrono::microseconds>(now - start).count();
            auto throughput = (bytes * 0.000001) / (relativetime * 0.000001);

            result->set_value("," + to_string(relativetime) + "," + to_string(lround(bytes)) + "," + "write" + "," + to_string(throughput));
            return 0;
        }
};
