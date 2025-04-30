#include "emulation_node.hpp"

#define BOOST_NO_CXX11_SCOPED_ENUMS
#include <boost/filesystem.hpp>
#undef BOOST_NO_CXX11_SCOPED_ENUMS

#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <cmath>

using namespace std;
class ceph_emulation_node : public emulation_node
{
    private:
        string active_dir;
    public:
        ceph_emulation_node(string ceph_path) {
            active_dir = ceph_path;
        };

        int read_request(string filename, double bytes, promise<string> && result){
            ifstream file;

            string parent_dir = filename.substr(0,3);
            string child_dir = filename.substr(3,1);
            string name = filename.substr(4);

            string path = active_dir + "/" + parent_dir + "/" + child_dir + "/" + name;
            
            auto const start = chrono::high_resolution_clock::now();
            file.open(path.c_str());

            if (!file.is_open()){
                result.set_value(", file not found: " + path);
                return 1;
            }

            for (size_t i = 0; i < bytes-1; i++)
            {
                if (file.eof()){
                    result.set_value("read beyond file limits");
                    return 1;
                }
                
                char data;
                file.read(&data, sizeof(char));
            }
            
            file.close();
            
            auto const now = chrono::high_resolution_clock::now();
            auto relativetime = chrono::duration_cast<std::chrono::microseconds>(now - start).count();
            auto throughput = (bytes * 0.000001) / (relativetime * 0.000001);
        
            result.set_value("," + to_string(relativetime) + "," + to_string(lround(bytes)) + "," + "read" + "," + to_string(throughput));
            return 0;
        }
        
        int write_request(string filename, double bytes, promise<string> && result) {
            ofstream file;

            string parent_dir = filename.substr(0,3);
            string child_dir = filename.substr(3,1);
            string name = filename.substr(4);

            boost::filesystem::create_directories(active_dir + "/" + parent_dir + "/" + child_dir);

            auto const start = chrono::high_resolution_clock::now();
            file.open(active_dir + "/" + parent_dir + "/" + child_dir + "/" + name);
            
            if (!file.is_open()){
                result.set_value(", could not open file for write");
                return 1;
            }
            
            file.write("0", bytes - 1);
            file.write("\n",1);
            file.close();

            auto const now = chrono::high_resolution_clock::now();
            auto relativetime = chrono::duration_cast<std::chrono::microseconds>(now - start).count();
            auto throughput = (bytes * 0.000001) / (relativetime * 0.000001);
            
            result.set_value("," + to_string(relativetime) + "," + to_string(lround(bytes)) + "," + "write" + "," + to_string(throughput));
            return 0;
        }
};

