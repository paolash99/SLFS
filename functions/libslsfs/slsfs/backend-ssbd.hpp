#pragma once

#ifndef SLSFS_BACKEND_SSBD_HPP__
#define SLSFS_BACKEND_SSBD_HPP__

#include "storage.hpp"
#include "basetypes.hpp"
#include "scope-exit.hpp"
#include "leveldb-serializer.hpp"
#include "debuglog.hpp"
#include "socket-writer.hpp"

#include <oneapi/tbb/concurrent_hash_map.h>

#include <boost/exception/all.hpp>
#include <boost/exception/error_info.hpp>
#include <boost/signals2.hpp>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>

namespace slsfs::backend
{

namespace detail
{

class job
{
    boost::signals2::signal<void(leveldb_pack::packet_pointer)> next_;
    leveldb_pack::packet_pointer original_request_ = nullptr;
    std::chrono::system_clock::time_point registered_ = std::chrono::system_clock::now();
    std::chrono::system_clock::time_point started_;
    std::chrono::system_clock::time_point ended_;

public:
    job () = default;

    template<typename Next>
    job (Next && callable) { next_.connect(callable); }

    template<typename Next>
    job (leveldb_pack::packet_pointer original_request, Next && callable):
        original_request_{original_request} {
        next_.connect(callable);
    }

    void run (leveldb_pack::packet_pointer resp)
    {
        ended_ = std::chrono::system_clock::now();
        next_(resp);
    }

    void cancel()
    {
        leveldb_pack::packet_pointer resp = std::make_shared<leveldb_pack::packet>();
        resp->header = original_request_->header;
        resp->header.type = leveldb_pack::msg_t::err;
        run(resp);
    }

    void mark_started() { started_ = std::chrono::system_clock::now(); }

    void print ()
    {
        log::log("job stat: waittime: {}, execute time: {}",
                 started_ - registered_, ended_ - started_);
    }
};

using job_ptr = std::shared_ptr<job>;
} // namespace detail


class ssbd
{
    boost::asio::io_context& io_context_;
    boost::asio::ip::tcp::socket socket_;
    boost::asio::ip::tcp::endpoint const endpoint_;
    using jobmap =
        oneapi::tbb::concurrent_hash_map<
            leveldb_pack::packet_header,
            detail::job_ptr,
            leveldb_pack::packet_header_key_hash_compare>;

    std::once_flag read_started_flag_;
    jobmap outstanding_jobs_;
    using jobmap_accessor = decltype(outstanding_jobs_)::accessor;
    socket_writer::socket_writer<leveldb_pack::packet, std::vector<leveldb_pack::unit_t>> writer_;

    void start_read_loop() {
        std::call_once(read_started_flag_, [this] { start_read_one(); });
    }

    void start_read_one()
    {
        leveldb_pack::packet_pointer resp = std::make_shared<leveldb_pack::packet>();
        auto headerbuf = std::make_shared<std::vector<leveldb_pack::unit_t>> (leveldb_pack::packet_header::bytesize);
        log::log("backend ssbd start_read_one called headersize: {}", leveldb_pack::packet_header::bytesize);
        boost::asio::async_read(
            socket_,
            boost::asio::buffer(headerbuf->data(), headerbuf->size()),
            [this, resp, headerbuf]
            (boost::system::error_code const& ec, std::size_t transferred_size) {
                if (ec)
                {
                    log::log<log::level::error>("ssbd backend: {} have boost error: {} on start_read_one() header {}",
                             boost::lexical_cast<std::string>(endpoint_), ec.message(), resp->header.print());
                    return;
                }

                assert(headerbuf->size() == transferred_size);
                resp->header.parse(headerbuf->data());
                start_read_one_body(resp);
            });
    }

    void start_read_one_body(leveldb_pack::packet_pointer resp)
    {
        auto bodybuf = std::make_shared<base::buf>(resp->header.datasize, 0);

        log::log("async_read read body called readsize={}", bodybuf->size());
        boost::asio::async_read(
            socket_,
            boost::asio::buffer(bodybuf->data(), bodybuf->size()),
            [this, resp, bodybuf] (boost::system::error_code const& ec, std::size_t transferred_size) {
                assert(bodybuf->size() == transferred_size);

                if (ec)
                {
                    log::log<log::level::error>("ssbd backend: {} have boost error: {} on start_read_one() -> body header {}", boost::lexical_cast<std::string>(endpoint_), ec.message(), resp->header.print());
                    return;
                }

                resp->data.parse(transferred_size, bodybuf->data());
                jobmap_accessor it;
                [[maybe_unused]]
                bool found = outstanding_jobs_.find(it, resp->header);
                assert(found);

                log::log("async read body executing with header {}", resp->header.print());
                it->second->run(resp);
                //it->second->print();

                outstanding_jobs_.erase(it);
                start_read_one();
            });
    }

public:
    ssbd(boost::asio::io_context& io, std::string const& host, std::string const& port):
        io_context_{io}, socket_(io),
        endpoint_{boost::asio::ip::make_address_v4(host), static_cast<std::uint16_t>(std::stoi(port))},
        writer_{io, socket_} {}

    using handler     = std::function<void(base::buf)>;
    using handler_ptr = std::shared_ptr<handler>;

    void connect()
    {
        log::log("connect to {}", boost::lexical_cast<std::string>(endpoint_));

        try {
            socket_.connect(endpoint_);
        } catch (boost::exception & e) {
            using host_endpoint = boost::error_info<struct ssbd_connect_host, boost::asio::ip::tcp::endpoint>;
            e << host_endpoint{endpoint_};
            throw;
        }
    }

    void close()
    {
        socket_.close();
        for (auto &job : outstanding_jobs_)
            job.second->cancel();
    }

    void start_send_request (leveldb_pack::packet_pointer request,
                             std::function<void(leveldb_pack::packet_pointer)> on_response)
    {
        log::log("ssbd backend start_send_request: {}", request->header.print());
        detail::job_ptr newjob = std::make_shared<detail::job>(request, on_response);

        [[maybe_unused]]
        bool ok = outstanding_jobs_.emplace(request->header, newjob);
        assert(ok);

        auto next = std::make_shared<socket_writer::boost_callback>(
            [this, request, newjob]
            (boost::system::error_code const& ec, std::size_t) {
                newjob->mark_started();
                if (ec)
                {
                    log::log<log::level::error>("error {} on {}; start_send_request: {}",
                             ec.message(), boost::lexical_cast<std::string>(endpoint_), request->header.print());
                    newjob->cancel();
                    return;
                }
                start_read_loop();
            });

        switch (request->header.type)
        {
        case leveldb_pack::msg_t::get:
            // read request use header size as data size to read
            writer_.start_write_socket(request, next, request->serialize_header());
            break;
        default:
            writer_.start_write_socket(request, next);
        }
    }
};

} // namespace slsfs::backend


#endif // SLSFS_BACKEND_SSBD_HPP__
