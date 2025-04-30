#include "trace_parser.hpp"
#include "ceph_emulation_node.cpp"
#include "direct_ceph_emulation_node.cpp"
#include "slsfs_emulation_node.cpp"

#include "slsfs_direct_emulation_node.hpp"
#include "emulation_node.hpp"

#include <boost/asio.hpp>
#include <boost/log/trivial.hpp>
#include <cds/container/treiber_stack.h>

#include <iostream>
#include <fstream>
#include <iterator>
#include <string>
#include <sstream>
#include <queue>
#include <vector>
#include <thread>
#include <future>
#include <chrono>
#include <stack>
#include <cstdlib>
#include <csignal>

using namespace std;

void signalHandler( int signum ) {
   cout << "Interrupt signal (" << signum << ") received.\n";
   exit(signum);
}

void create_missing_files_slsfs(string missing_file_path, int nr_rows_to_read) {
    BOOST_LOG_TRIVIAL(info) << "Writing missing files for SLSFS";
    slsfs_emulation_node client;

    fstream missing_files (missing_file_path, ios::in);
    if (missing_files.is_open()) {
        for (int i = 0; i < nr_rows_to_read; i++) {
            string row;
            if (getline(missing_files, row)) {
                stringstream stream;
                stream.str(row);
                string filename;
                string size;
                auto ret_val = make_shared<promise<string>>();
                if (getline(stream, filename, ' ')) {
                    getline(stream, size, '\n');
                    client.write_request(filename, stod(size), ret_val);
                    BOOST_LOG_TRIVIAL(debug) << ret_val->get_future().get();
                }
            } else {
                throw runtime_error("create_missing_files_slsfs: not enough rows to read");
            }
        }
    }
    missing_files.close();
}

void submit_jobs(stack<trace_row>& current_jobs, queue<string>& report,
                 [[maybe_unused]] string trace_dir, [[maybe_unused]] boost::asio::io_context& io) {
    vector<thread> emulation_nodes;
    vector<future<string>> return_values;

    while (!current_jobs.empty()) {
        // if (current_jobs.size() > 1) {
        //     BOOST_LOG_TRIVIAL(debug) << "CONCURRENT REQUESTS" << endl;
        // }

        trace_row job = current_jobs.top();
        auto ret_val = std::make_shared<promise<string>>();

        // memleak
        emulation_node * node = new slsfs_emulation_node();
        //emulation_node * node = new slsfs_direct_emulation_node(io);
        //emulation_node * node = new direct_ceph_emulation_node(trace_dir);
        // cout << job.blob_name << " " << (job.access_type == read_op ? "read" : "write") << " " << job.blob_bytes << "\n";

        return_values.push_back(ret_val->get_future());

        if (job.access_type == read_op)
            emulation_nodes.push_back(move(thread(&emulation_node::read_request, node, job.blob_name, job.blob_bytes, ret_val)));
        else
            emulation_nodes.push_back(move(thread(&emulation_node::write_request, node, job.blob_name, job.blob_bytes, ret_val)));

        current_jobs.pop();
    }

    // Waiting for threads to execute
    for (auto & th : emulation_nodes) {
        if (th.joinable())
            th.join();
    }
    // Getting return values
    auto const timestamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    for (auto & val : return_values) {
        report.push(to_string(timestamp) + val.get());
    }
}

int main(int argc, char const *argv[])
{
    signal(SIGINT, signalHandler);

    if (argc < 4) {
        printf("usage: ./trace_emulator <tracefile path> <num of rows to read> <trace_dir> <out_path> <missing_files_path> <missing_files_nr_rows> <real_time boolean>\n");
    }

    boost::asio::io_context ioc;
    boost::asio::steady_timer timer(ioc, std::chrono::hours(24)); // to keep ioc alive for 3 second
    timer.async_wait([](boost::system::error_code) {});
    std::vector<std::jthread> threads;
    for (int i = 0; i < 8100; i++)
        threads.emplace_back([&ioc] { ioc.run(); });

    bool real_time = false;
    if (string(argv[7]) == "true")
        real_time = true;

    BOOST_LOG_TRIVIAL(info) << "Trace emulator initializing with the following configuration:";
    BOOST_LOG_TRIVIAL(info) << "   Trace: " << argv[1] << ", number of rows: " << stoi(argv[2])
    << " real time emulation: " << real_time;


    // comment this line if not targetting SLSFS emulation
    if (stoi(argv[6]) != 0)
        create_missing_files_slsfs(argv[5], stoi(argv[6]));

    trace_parser parser(argv[1], stoi(argv[2]));
    stack<trace_row> current_jobs;

    ofstream report;
    report.open("report.csv");
    report << "Timestamp,duration_us,bytes,access_type,throughput_mbs" << endl;

    queue<string> report_rows;

    long io_nr = 0, cent = parser.trace_size() / 100;
    auto const start = chrono::high_resolution_clock::now();
    for (auto row = parser.trace_begin(); row != parser.trace_end(); row++)
    {
        if (cent && io_nr % cent == 0)
            BOOST_LOG_TRIVIAL(info) << "progress: " << io_nr << "/" << parser.trace_size();
        if (current_jobs.size() != 0) {
            if (row->timestamp == current_jobs.top().timestamp)
                current_jobs.push(*row);
            else {
                io_nr += current_jobs.size();
                long time_to_wait = row->timestamp - current_jobs.top().timestamp;

                if (real_time){
                    boost::asio::post(ioc, [&, current_jobs]() mutable {submit_jobs(current_jobs, report_rows, argv[3], ioc); });
                    std::stack<trace_row> ().swap(current_jobs);
                }
                else
                    submit_jobs(current_jobs, report_rows, argv[3], ioc);

                if (real_time)
                    this_thread::sleep_for(std::chrono::milliseconds(time_to_wait));
                current_jobs.push(*row);
            }
        }
        else {
            current_jobs.push(*row);
        }
    }

    io_nr += current_jobs.size();
    submit_jobs(current_jobs, report_rows, argv[3], ioc);

    auto const now = chrono::high_resolution_clock::now();
    double total_runtime = chrono::duration_cast<std::chrono::milliseconds>(now - start).count() / 1000.0;
    double iops = io_nr / total_runtime;

    BOOST_LOG_TRIVIAL(info) << "Total IO operations = " << io_nr;
    BOOST_LOG_TRIVIAL(info) << "IOPS = " << iops;
    BOOST_LOG_TRIVIAL(info) << "Runtime = " << total_runtime;

    BOOST_LOG_TRIVIAL(info) << "Writing report...";

    // Writing report
    while (report_rows.size() > 0) {
        report << report_rows.front() << endl;
        report_rows.pop();
    }
    report.close();
    timer.cancel();

    BOOST_LOG_TRIVIAL(info) << "Done writing report...";

    return 0;
}
