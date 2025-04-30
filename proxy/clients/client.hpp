#include "../basic.hpp"
#include "../serializer.hpp"
#include "../json-replacement.hpp"
#include "../uuid.hpp"
#include "../scope_exit.hpp"
#include "clientlib.hpp"
#include "clientlib-direct-client.hpp"

#include <fmt/core.h>
#include <boost/asio.hpp>
#include <boost/program_options.hpp>
#include <boost/filesystem.hpp>
#include <zk/client.hpp>
#include <zk/results.hpp>
#include <zookeeper/zookeeper.h>
#include <thread>
#include <chrono>
#include <map>
#include <limits>
#include <fstream>
#include <ctime>
#include <iomanip>

class Client
{
public:
    struct OperationMetrics
    {
        std::chrono::nanoseconds min_latency = std::chrono::nanoseconds::max();
        std::chrono::nanoseconds max_latency = std::chrono::nanoseconds::min();
        std::chrono::nanoseconds total_latency = std::chrono::nanoseconds::zero();
        double total_latency_squared = 0.0;
        uint64_t successful_count = 0;
        uint64_t unsuccessful_count = 0;
    };

    Client(std::string const &zookeeperIp)
        : start_time(std::chrono::high_resolution_clock::now()),
          io_context(),
          slsfs_client(io_context, zookeeperIp),
          slsfs_direct_client(io_context, zookeeperIp)
    {
    }

    std::map<std::string, OperationMetrics> metrics;
    OperationMetrics overall_metrics;
    std::chrono::high_resolution_clock::time_point start_time;

    template <typename Func>
    int execute_operation(const std::string &operation_name, Func &&func)
    {
        auto start = std::chrono::high_resolution_clock::now();
        int result = func();
        auto end = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

        if (result == 1)
        {
            update_metrics(operation_name, duration, true);
        }
        else
        {
            update_metrics(operation_name, duration, false);
        }

        return result;
    }

    void update_metrics(const std::string &operation_name, std::chrono::nanoseconds duration, bool success)
    {
        auto &op_metrics = metrics[operation_name];
        auto &overall = overall_metrics;
        double num_duration = static_cast<double>(duration.count()) / 1000000.0;

        if (success)
        {
            op_metrics.min_latency = std::min(op_metrics.min_latency, duration);
            op_metrics.max_latency = std::max(op_metrics.max_latency, duration);
            op_metrics.total_latency += duration;
            op_metrics.total_latency_squared += num_duration * num_duration;
            op_metrics.successful_count++;

            overall.min_latency = std::min(overall.min_latency, duration);
            overall.max_latency = std::max(overall.max_latency, duration);
            overall.total_latency += duration;
            overall.total_latency_squared += num_duration * num_duration;
            overall.successful_count++;
        }
        else
        {
            op_metrics.unsuccessful_count++;
            overall.unsuccessful_count++;
        }
    }

    int mkdir(std::string const &directory)
    {
        return execute_operation("mkdir", [&]()
                                 {
            slsfs::pack::packet_pointer request = slsfs::client::packet_create::mkdir(directory);
            try
            {
                std::string resp = slsfs_client.send(request);
                auto end = std::find(resp.begin(), resp.end(), '\0');
                std::string final_resp(resp.begin(), end);
                return final_resp == "OK"? 1 : -1;
            }
            catch (boost::exception const &e)
            {
                return -1;
            } });
    }

    int add_file(std::string const &file_path)
    {
        return execute_operation("add_file", [&]()
                                 {
            slsfs::pack::packet_pointer request = slsfs::client::packet_create::add_file_v2(file_path);
            try
            {
                std::string resp = slsfs_client.send(request);
                auto end = std::find(resp.begin(), resp.end(), '\0');
                std::string final_resp(resp.begin(), end);
                return final_resp == "OK"? 1 : -1;
            }
            catch (boost::exception const &e)
            {
                return -1;
            } });
    }

    int delete_file(std::string const &file_path)
    {
        return execute_operation("delete_file", [&]()
                                 {
            slsfs::pack::packet_pointer request = slsfs::client::packet_create::delete_file(file_path);
            try
            {
                std::string resp = slsfs_client.send(request);
                auto end = std::find(resp.begin(), resp.end(), '\0');
                std::string final_resp(resp.begin(), end);
                return final_resp == "OK"? 1 : -1;
            }
            catch (boost::exception const &e)
            {
                return -1;
            } });
    }

    int open_file(std::string const &file_path)
    {
        return execute_operation("open_file", [&]()
                                 {
            slsfs::pack::packet_pointer request = slsfs::client::packet_create::open(file_path);
            try
            {
                std::string resp = slsfs_client.send(request);
                if(resp.rfind("ERROR", 0) == 0) {
                    return -1;
                }
                else {      
                    boost::asio::ip::address_v4::bytes_type host;
                    std::memcpy(&host, resp.data(), sizeof(host));
                    boost::asio::ip::address address = boost::asio::ip::make_address_v4(host);
                    std::uint16_t port = 0;
                    std::memcpy(&port, resp.data() + sizeof(host), sizeof(port));
                    port =  slsfs::pack::hton(port);
                    boost::asio::ip::tcp::endpoint endpoint(address, port); 
                    slsfs_direct_client.add_endpoint(endpoint, request);
                    return 1;
                }
            }
            catch (boost::exception const &e)
            {
                return -1;
            } });
    }

    int close_file(std::string const &file_path)
    {
        return execute_operation("close_file", [&]()
                                 {
            slsfs::pack::packet_pointer request = slsfs::client::packet_create::close(file_path);
            try
            {
                std::string resp = slsfs_client.send(request);
                auto end = std::find(resp.begin(), resp.end(), '\0');
                std::string final_resp(resp.begin(), end);
                return final_resp == "OK"? 1 : -1;
            }
            catch (boost::exception const &e)
            {
                return -1;
            } });
    }

    int ls(std::string const &path)
    {
        return execute_operation("ls", [&]()
                                 {
            slsfs::pack::packet_pointer request = slsfs::client::packet_create::ls(path);
            try
            {
                std::string resp = slsfs_client.send(request);
                return resp.find("Error") == std::string::npos ? 1 : -1;
            }
            catch (boost::exception const &e)
            {
                return -1;
            } });
    }

    int get_stat_file(std::string const &file_path)
    {
        return execute_operation("get_stat_file", [&]()
                                 {
            slsfs::pack::packet_pointer request = slsfs::client::packet_create::get_stat_file(file_path);
            try
            {
                std::string resp = slsfs_client.send(request);
                return resp.find("Error") == std::string::npos ? 1 : -1;
            }
            catch (boost::exception const &e)
            {
                return -1;
            } });
    }

    int get_stat_dir(std::string const &dir_path)
    {
        return execute_operation("get_stat_dir", [&]()
                                 {
            slsfs::pack::packet_pointer request = slsfs::client::packet_create::get_stat_dir(dir_path);
            try
            {
                std::string resp = slsfs_client.send(request);
                return resp.find("Error") == std::string::npos ? 1 : -1;
            }
            catch (boost::exception const &e)
            {
                return -1;
            } });
    }

    int init(std::string const &dir_path)
    {
        return execute_operation("init", [&]()
                                 {
            slsfs::pack::packet_pointer request = slsfs::client::packet_create::init(dir_path);
            try
            {
                std::string resp = slsfs_client.send(request);
                auto end = std::find(resp.begin(), resp.end(), '\0');
                std::string final_resp(resp.begin(), end);
                return final_resp == "OK"? 1 : -1;
            }
            catch (boost::exception const &e)
            {
                return -1;
            } });
    }

    int rename_file(std::string const &file_path, std::string const &new_file_path)
    {
        return execute_operation("rename_file", [&]()
                                 {
            slsfs::pack::packet_pointer request = nullptr;
            std::string parent_path = slsfs::client::packet_create::get_parent_path(file_path);
            slsfs::pack::key_t parent_key = slsfs::uuid::get_uuid(parent_path);
            slsfs::pack::key_t new_parent_key = slsfs::uuid::get_uuid(slsfs::client::packet_create::get_parent_path(new_file_path));

            bool same_proxy = slsfs_client.compare_proxies_by_key(new_parent_key, parent_key);
            if (!same_proxy)
            {
                request = slsfs::client::packet_create::lock(parent_path);
                try
                {
                    slsfs_client.send(request);
                }
                catch (boost::exception const &e)
                {
                    return -1;
                }
            }
            request = slsfs::client::packet_create::rename_file(file_path, new_file_path);
            try
            {
                slsfs_client.send(request);
                // Run the unlock process in a background thread
                std::thread([parent_path, this]()
                        {
                            slsfs::pack::packet_pointer request = slsfs::client::packet_create::unlock(parent_path);
                            std::string result = slsfs_client.send(request);
                            while (result != "OK") {
                                result = slsfs_client.send(request);
                            
                            } 
                        }).detach(); // Detach the thread to run in the background

                return 1;
            }
            catch (boost::exception const &e)
            {
                // Run the unlock process in a background thread
                std::thread([parent_path, this]()
                        {
                            slsfs::pack::packet_pointer request = slsfs::client::packet_create::unlock(parent_path);
                            std::string result = slsfs_client.send(request);
                            while (result != "OK") {
                                result = slsfs_client.send(request);
                            
                            } 
                        }).detach(); // Detach the thread to run in the background

                return -1;
            } });
    }

    int read(std::string const &fileName, std::uint32_t const size)
    {
        return execute_operation("read", [&]()
                                 {
            slsfs::pack::packet_pointer request = slsfs::client::packet_create::read_v2(fileName, size);
            try
            {
                std::string resp = slsfs_direct_client.send(request);
                return resp.find("Error") == std::string::npos ? 1 : -1;
            }
            catch (boost::exception const &e)
            {
                return -1;
            } });
    }

    void reset_latencies()
    {
        // Reset per-operation metrics
        for (auto &entry : metrics)
        {
            OperationMetrics &op_metrics = entry.second;
            op_metrics.min_latency = std::chrono::nanoseconds::max();
            op_metrics.max_latency = std::chrono::nanoseconds::min();
            op_metrics.total_latency = std::chrono::nanoseconds::zero();
            op_metrics.total_latency_squared = 0.0;
            op_metrics.successful_count = 0;
            op_metrics.unsuccessful_count = 0;
        }

        // Reset overall metrics
        overall_metrics.min_latency = std::chrono::nanoseconds::max();
        overall_metrics.max_latency = std::chrono::nanoseconds::min();
        overall_metrics.total_latency = std::chrono::nanoseconds::zero();
        overall_metrics.total_latency_squared = 0.0;
        overall_metrics.successful_count = 0;
        overall_metrics.unsuccessful_count = 0;
    }

    int direct_read(std::string const &file_path, std::uint32_t const size)
    {
        return execute_operation("direct_read", [&]()
                                 {
                                    try{
                                        slsfs::pack::packet_pointer request = slsfs::client::packet_create::open(file_path);
                                        std::string resp = slsfs_client.send(request);
                                        if(resp.rfind("ERROR", 0) == 0) {
                                            return -1;
                                        }
                                        else {      
                                            boost::asio::ip::address_v4::bytes_type host;
                                            std::memcpy(&host, resp.data(), sizeof(host));
                                            boost::asio::ip::address address = boost::asio::ip::make_address_v4(host);
                                            std::uint16_t port = 0;
                                            std::memcpy(&port, resp.data() + sizeof(host), sizeof(port));
                                            port = slsfs::pack::hton(port);
                                            boost::asio::ip::tcp::endpoint endpoint(address, port);
                                            try
                                            {
                                                slsfs_direct_client.add_endpoint(endpoint, request);
                                            }
                                            catch (boost::exception &e)
                                            {
                                                //do nothing
                                            }
                                            
                                        }
                                        request = slsfs::client::packet_create::read_v2(file_path, size);
                                        std::string resp2 = slsfs_direct_client.send(request);
                                        if(resp2.rfind("ERROR", 0) == 0) {
                                            return -1;
                                        }  
                                        request = slsfs::client::packet_create::close(file_path);
                                        std::string resp3 = slsfs_client.send(request);
                                        auto end = std::find(resp3.begin(), resp3.end(), '\0');
                                        std::string final_resp(resp3.begin(), end);
                                        return final_resp.rfind("ERROR", 0) == 0 ? -1 : 1;
                                    }
                                    catch (boost::exception const &e)
                                     {
                                         return -1;
                                     } });
    }

    template <typename BufContainer>
    int direct_write(std::string const &file_path, BufContainer const &buffer, std::uint32_t const position)
    {
        return execute_operation("direct_write", [&]()
                                 {
                                    try{
                                        slsfs::pack::packet_pointer request = slsfs::client::packet_create::open(file_path);
                                        std::string resp = slsfs_client.send(request);
                                        if(resp.rfind("ERROR", 0) == 0) {
                                            return -1;
                                        }
                                        else {      
                                            boost::asio::ip::address_v4::bytes_type host;
                                            std::memcpy(&host, resp.data(), sizeof(host));
                                            boost::asio::ip::address address = boost::asio::ip::make_address_v4(host);
                                            std::uint16_t port = 0;
                                            std::memcpy(&port, resp.data() + sizeof(host), sizeof(port));
                                            port = slsfs::pack::hton(port);
                                            boost::asio::ip::tcp::endpoint endpoint(address, port);
                                            try
                                            {
                                                slsfs_direct_client.add_endpoint(endpoint, request);
                                            }
                                            catch (boost::exception &e)
                                            {
                                                //do nothing
                                            }
                                            
                                        }
                                        request = slsfs::client::packet_create::write_v2(file_path, buffer, position);
                                        std::string resp2 = slsfs_direct_client.send(request);
                                        if(resp2.rfind("ERROR", 0) == 0) {
                                            return -1;
                                        }  
                                        request = slsfs::client::packet_create::close(file_path);
                                        std::string resp3 = slsfs_client.send(request);
                                        auto end = std::find(resp3.begin(), resp3.end(), '\0');
                                        std::string final_resp(resp3.begin(), end);
                                        return final_resp == "OK"? 1 : -1;
                                    }
                                    catch (boost::exception const &e)
                                     {
                                         return -1;
                                     } });
    }

    template <typename BufContainer>
    int write(std::string const &fileName, BufContainer const &buffer, std::uint32_t const position)
    {
        return execute_operation("write", [&]()
                                 {
            slsfs::pack::packet_pointer request = slsfs::client::packet_create::write_v2(fileName, buffer, position);
            try
            {
                std::string resp = slsfs_direct_client.send(request);
                auto end = std::find(resp.begin(), resp.end(), '\0');
                std::string final_resp(resp.begin(), end);
                return final_resp == "OK"? 1 : -1;
            }
            catch (boost::exception const &e)
            {
                return -1;
            } });
    }

    template <typename BufContainer>
    int create_and_write_file_single_request(std::string const &fileName, BufContainer const &buffer)
    {
        return execute_operation("create_and_write_file", [&]()
                                 {
            slsfs::pack::packet_pointer request = slsfs::client::packet_create::create_and_write_file_single_request(fileName, buffer);
            try
            {
                std::string resp = slsfs_client.send(request);
                auto end = std::find(resp.begin(), resp.end(), '\0');
                std::string final_resp(resp.begin(), end);
                return final_resp == "OK"? 1 : -1;
            }
            catch (boost::exception const &e)
            {
                return -1;
            } });
    }

    double get_total_latency()
    {
        return get_total_latency_per_operation(overall_metrics);
    }

    double get_total_latency_squared()
    {
        return get_total_latency_squared_per_operation(overall_metrics);
    }

    void print_metrics()
    {
        fmt::print("Overall Metrics:\n");
        print_operation_metrics("Overall", overall_metrics);

        fmt::print("\nPer-Operation Metrics:\n");
        for (const auto &[op_name, op_metrics] : metrics)
        {
            print_operation_metrics(op_name, op_metrics);
        }
    }

    void write_metrics_to_file()
    {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);

        std::stringstream ss;
        ss << std::put_time(std::localtime(&in_time_t), "%Y%m%d-%H%M%S");

        const char *home_dir = getenv("HOME");
        if (home_dir == nullptr)
        {
            fmt::print("Error: Unable to get home directory.\n");
            return;
        }

        boost::filesystem::path file_path(home_dir);
        file_path /= fmt::format("reports-{}.txt", ss.str());

        std::ofstream file(file_path.string());
        if (!file.is_open())
        {
            fmt::print("Failed to open file for writing metrics: {}\n", file_path.string());
            return;
        }

        file << "Overall Metrics:\n";
        write_operation_metrics(file, "Overall", overall_metrics);

        file << "\nPer-Operation Metrics:\n";
        for (const auto &[op_name, op_metrics] : metrics)
        {
            write_operation_metrics(file, op_name, op_metrics);
        }

        file.close();
        fmt::print("Metrics written to file: {}\n", file_path.string());
    }

private:
    boost::asio::io_context io_context;
    slsfs::client::client slsfs_client;
    slsfs::client::direct_client slsfs_direct_client;

    double get_total_latency_squared_per_operation(const OperationMetrics &metrics)
    {
        if (metrics.successful_count > 0)
        {
            return metrics.total_latency_squared;
        }
        else
        {
            return -1.0;
        }
    }

    double get_total_latency_per_operation(const OperationMetrics &metrics)
    {
        if (metrics.successful_count > 0)
        {
            return metrics.total_latency.count() / 1000000.0;
        }
        else
        {
            return -1.0;
        }
    }

    void print_operation_metrics(const std::string &op_name, const OperationMetrics &metrics)
    {
        fmt::print("  {}:\n", op_name);
        fmt::print("    Min Latency: {} ns\n", metrics.min_latency.count());
        fmt::print("    Max Latency: {} ns\n", metrics.max_latency.count());

        auto now = std::chrono::high_resolution_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::duration<double>>(now - start_time);
        double total_seconds = total_duration.count();

        uint64_t total_operations = metrics.successful_count + metrics.unsuccessful_count;
        double throughput = total_operations / total_seconds;
        double successful_throughput = metrics.successful_count / total_seconds;

        if (metrics.successful_count > 0)
        {
            double avg_latency = static_cast<double>(metrics.total_latency.count()) / metrics.successful_count;
            fmt::print("    Avg Latency: {:.2f} ns\n", avg_latency);
        }
        else
        {
            fmt::print("    Avg Latency: N/A\n");
        }

        fmt::print("    Throughput: {:.2f} ops/s\n", throughput);
        fmt::print("    Successful Throughput: {:.2f} ops/s\n", successful_throughput);
        fmt::print("    Successful Operations: {}\n", metrics.successful_count);
        fmt::print("    Unsuccessful Operations: {}\n", metrics.unsuccessful_count);
    }

    void write_operation_metrics(std::ofstream &file, const std::string &op_name, const OperationMetrics &metrics)
    {
        file << "  " << op_name << ":\n";
        file << "    Min Latency: " << metrics.min_latency.count() << " ns\n";
        file << "    Max Latency: " << metrics.max_latency.count() << " ns\n";

        auto now = std::chrono::high_resolution_clock::now();
        auto total_duration = std::chrono::duration_cast<std::chrono::duration<double>>(now - start_time);
        double total_seconds = total_duration.count();

        uint64_t total_operations = metrics.successful_count + metrics.unsuccessful_count;
        double throughput = total_operations / total_seconds;
        double successful_throughput = metrics.successful_count / total_seconds;

        if (metrics.successful_count > 0)
        {
            double avg_latency = static_cast<double>(metrics.total_latency.count()) / metrics.successful_count;
            file << "    Avg Latency: " << avg_latency << " ns\n";
        }
        else
        {
            file << "    Avg Latency: N/A\n";
        }

        file << "    Throughput: " << throughput << " ops/s\n";
        file << "    Successful Throughput: " << successful_throughput << " ops/s\n";
        file << "    Successful Operations: " << metrics.successful_count << "\n";
        file << "    Unsuccessful Operations: " << metrics.unsuccessful_count << "\n";
    }
};