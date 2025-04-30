#include "basic.hpp"
#include "leveldb-serializer.hpp"

#include <boost/asio.hpp>

#include <fmt/core.h>

#include <algorithm>
#include <iostream>

#include <memory>
#include <array>
#include <list>
#include <thread>
#include <vector>
#include <random>
#include <chrono>

using boost::asio::ip::tcp;

template<typename Function, typename ... Args>
auto record(Function &&f, Args &&... args) -> long int
{
    auto const start = std::chrono::high_resolution_clock::now();
    std::invoke(f, std::forward<Args>(args)...);
    auto const now = std::chrono::high_resolution_clock::now();
    auto relativetime = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count();
    return relativetime;
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
    BOOST_LOG_TRIVIAL(debug) << fmt::format("{0} avg={1:.3f} sd={2:.3f}", memo, mean, std::sqrt(var));
    for (auto && [time, count] : dist)
        BOOST_LOG_TRIVIAL(debug) << fmt::format("{0} {1}: {2}", memo, time, count);
}

auto genversion() -> std::uint32_t
{
    std::uint64_t v = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return static_cast<std::uint32_t>(v >> 6);
}

void write(tcp::socket &s, int pos, std::vector<slsfs::leveldb_pack::unit_t>& buf)
{
    std::uint32_t version = genversion();
    version = slsfs::leveldb_pack::hton(version);

    { // send merge_request_commit;
        slsfs::leveldb_pack::packet_pointer ptr = std::make_shared<slsfs::leveldb_pack::packet>();
        ptr->header.type = slsfs::leveldb_pack::msg_t::two_pc_prepare;
        ptr->header.uuid = slsfs::leveldb_pack::key_t{
            7, 8, 7, 8, 7, 8, 7, 8,
            7, 8, 7, 8, 7, 8, 7, 8,
            7, 8, 7, 8, 7, 8, 7, 8,
            7, 8, 7, 8, 7, 8, 7, 9
        };

        ptr->header.blockid = pos / 4096;
        ptr->header.position = pos % 4096;

        ptr->data.buf = std::vector<slsfs::leveldb_pack::unit_t> (sizeof(version));
        std::memcpy(ptr->data.buf.data(), &version, sizeof(version));

        auto buf = ptr->serialize();
        boost::asio::write(s, boost::asio::buffer(buf->data(), buf->size()));
    } // send merge_request_commit;

    { // read resp
        BOOST_LOG_TRIVIAL(trace) << "reading two_pc_prepare resp ";
        slsfs::leveldb_pack::packet_pointer resp = std::make_shared<slsfs::leveldb_pack::packet>();
        std::vector<slsfs::leveldb_pack::unit_t> headerbuf(slsfs::leveldb_pack::packet_header::bytesize);

        boost::asio::read(s, boost::asio::buffer(headerbuf.data(), headerbuf.size()));
        resp->header.parse(headerbuf.data());

        BOOST_LOG_TRIVIAL(trace) << "two_pc_prepare resp read " << resp->header;

        std::vector<slsfs::leveldb_pack::unit_t> bodybuf(resp->header.datasize);

        boost::asio::read(s, boost::asio::buffer(bodybuf.data(), bodybuf.size()));
        resp->data.parse(resp->header.datasize, bodybuf.data());
    } // read resp

    { // send merge_execute_commit;
        BOOST_LOG_TRIVIAL(trace) << "send two_pc_commit_execute ";
        slsfs::leveldb_pack::packet_pointer ptr = std::make_shared<slsfs::leveldb_pack::packet>();
        ptr->header.type = slsfs::leveldb_pack::msg_t::two_pc_commit_execute;
        ptr->header.uuid = slsfs::leveldb_pack::key_t{
            7, 8, 7, 8, 7, 8, 7, 8,
            7, 8, 7, 8, 7, 8, 7, 8,
            7, 8, 7, 8, 7, 8, 7, 8,
            7, 8, 7, 8, 7, 8, 7, 9
        };

        ptr->header.blockid = pos / 4096;
        ptr->header.position = pos % 4096;

        ptr->data.buf = std::vector<slsfs::leveldb_pack::unit_t> (sizeof(version)+buf.size());
        std::memcpy(ptr->data.buf.data(), &version, sizeof(version));

        auto buf = ptr->serialize();
        boost::asio::write(s, boost::asio::buffer(buf->data(), buf->size()));
    } // send merge_execute_commit;

    { // read resp
        slsfs::leveldb_pack::packet_pointer resp = std::make_shared<slsfs::leveldb_pack::packet>();
        std::vector<slsfs::leveldb_pack::unit_t> headerbuf(slsfs::leveldb_pack::packet_header::bytesize);
        boost::asio::read(s, boost::asio::buffer(headerbuf.data(), headerbuf.size()));

        resp->header.parse(headerbuf.data());
    } // read resp
}

void read(tcp::socket &s, int pos)
{
    { // send get
        slsfs::leveldb_pack::packet_pointer ptr = std::make_shared<slsfs::leveldb_pack::packet>();
        ptr->header.type = slsfs::leveldb_pack::msg_t::get;
        ptr->header.uuid = slsfs::leveldb_pack::key_t{
            7, 8, 7, 8, 7, 8, 7, 8,
            7, 8, 7, 8, 7, 8, 7, 8,
            7, 8, 7, 8, 7, 8, 7, 8,
            7, 8, 7, 8, 7, 8, 7, 9
        };

        ptr->header.blockid = pos / 4096;
        ptr->header.position = pos % 4096;
        ptr->header.datasize = 2;
        auto buf = ptr->serialize_header();
        boost::asio::write(s, boost::asio::buffer(buf->data(), buf->size()));
    } // send merge_execute_commit;

    { // read resp
        slsfs::leveldb_pack::packet_pointer resp = std::make_shared<slsfs::leveldb_pack::packet>();
        std::vector<slsfs::leveldb_pack::unit_t> headerbuf(slsfs::leveldb_pack::packet_header::bytesize);
        boost::asio::read(s, boost::asio::buffer(headerbuf.data(), headerbuf.size()));

        resp->header.parse(headerbuf.data());
        BOOST_LOG_TRIVIAL(debug) << "header " << resp->header << "\n";

        std::string bodybuf(resp->header.datasize, 0);
        boost::asio::read(s, boost::asio::buffer(bodybuf.data(), bodybuf.size()));
    } // read resp
}

int main()
{
    ssbd::basic::init_log();
    boost::asio::io_context io_context;
    ssbd::tcp::socket s(io_context);
    ssbd::tcp::resolver resolver(io_context);
    boost::asio::connect(s, resolver.resolve("localhost", "12000"));

    std::vector<slsfs::leveldb_pack::unit_t> buf(4096);
    //using ulli = unsigned long long int;

    std::list<double> records;
    for (int i=0; i<10000; i++)
        records.push_back(record([&](){ read(s, i); }));
    stats(records.begin(), records.end(), "read");

    records.clear();
    for (int i=0; i<100000; i++)
        records.push_back(record([&](){ write(s, i, buf); }));
    stats(records.begin(), records.end(), "write");

    return EXIT_SUCCESS;
}
