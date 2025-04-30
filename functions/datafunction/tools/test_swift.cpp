#include <slsfs/storage-swiftkv-minio.hpp>
#include <slsfs/basetypes.hpp>

#include <fstream>
#include <iostream>
#include <string>
#include <iostream>
#include <chrono>

#include <filesystem>

using namespace std;


int main(int argc, char const *argv[])
{
   slsfs::storage::swiftkv swift_client = slsfs::storage::swiftkv("moc-kvstore");

   slsfs::pack::key_t key = slsfs::pack::key_t{
       7, 8, 7, 8, 7, 8, 7, 8,
       7, 8, 7, 8, 7, 8, 7, 8,
       7, 8, 7, 8, 7, 8, 7, 8,
       7, 8, 7, 8, 7, 8, 7, 8};

    std::string data(1000, 'a');

    ofstream report;
    report.open("swift_direct_perf.csv");
    report << "op, " << "duration_us" << endl;
    cout << "file open" << endl;;


    for (int i = 0; i < 100; i++)
    {
        auto const start = chrono::high_resolution_clock::now();
        swift_client.write_key(key, data.size(), slsfs::base::to_buf(data), 0, 0);

        auto const now = chrono::high_resolution_clock::now();
        auto relativetime = chrono::duration_cast<std::chrono::microseconds>(now - start).count();
        cout << "write, " << relativetime << endl;
        report << "write, " << relativetime << endl;
    }

    for (int i = 0; i < 100; i++)
    {
        auto const start = chrono::high_resolution_clock::now();
        swift_client.read_key(key, data.size(), 0, data.size());

        auto const now = chrono::high_resolution_clock::now();
        auto relativetime = chrono::duration_cast<std::chrono::microseconds>(now - start).count();
        cout << "read, " << relativetime << endl;
        report << "read, " << relativetime << endl;
    }

    report.close();

    // cout << slsfs::base::to_string(swift_client.read_key(key, data.size(), 0, data.size())) << endl;
    // swift_client.get_list_key(key);

    return 0;
}
