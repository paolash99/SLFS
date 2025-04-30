#pragma once
#ifndef WORKER_HPP__
#define WORKER_HPP__

#include "basic.hpp"
#include "uuid.hpp"
#include "socket-writer.hpp"
#include "launcher-job.hpp"

#include <boost/signals2.hpp>

#include <concepts>

namespace slsfs::df
{
// Represents a worker datafunction that can perform a read or write on multiple files.
class worker;
using worker_ptr = std::shared_ptr<worker>;

template<typename T>
concept IsLauncher = requires(T l)
{
    { l.on_worker_reschedule     (std::declval<launcher::job_ptr>())} -> std::convertible_to<void>;
    { l.on_worker_close          (std::declval<worker_ptr>())       } -> std::convertible_to<void>;
    { l.on_worker_finished_a_job (std::declval<worker*>(), std::declval<launcher::job_ptr>())} -> std::convertible_to<void>;
};

using worker_id = std::size_t;

class worker : public std::enable_shared_from_this<worker>
{
    net::io_context& io_context_;
    tcp::socket socket_;
    socket_writer::socket_writer<pack::packet, std::vector<pack::unit_t>> writer_;
    std::atomic<bool> valid_ = true;

    launcher::job_map started_jobs_;

    boost::signals2::signal<void (launcher::job_ptr)> on_worker_reschedule_;
    boost::signals2::signal<void (worker_ptr)> on_worker_close_;
    boost::signals2::signal<void (worker*, launcher::job_ptr)> on_worker_finished_a_job_;

public:
    basic::time_point started_ = basic::now();
    uuid::uuid const id_ = uuid::gen_uuid();
    worker_id  const worker_id_ = uuid::hash(id_);

public:
    template<typename Launcher> requires IsLauncher<Launcher>
    worker(net::io_context& io, tcp::socket socket, Launcher& l):
        io_context_{io},
        socket_{std::move(socket)},
        writer_{io, socket_}
        {
            on_worker_reschedule_    .connect([&l] (launcher::job_ptr job) { l.on_worker_reschedule(job); });
            on_worker_close_         .connect([&l] (worker_ptr p) { l.on_worker_close(p); });
            on_worker_finished_a_job_.connect([&l] (worker* p, launcher::job_ptr job) { l.on_worker_finished_a_job(p, job); });
        }

    bool is_valid() { return valid_.load(); }
    void soft_close() { valid_.store(false); }
    int  pending_jobs() { return started_jobs_.size(); }

    void close()
    {
        boost::system::error_code ec;
        valid_.store(false);
        socket_.shutdown(tcp::socket::shutdown_both, ec);
        BOOST_LOG_TRIVIAL(info) << "worker [" << id_ << "] closed";
        for (auto unsafe_iterator = started_jobs_.begin(); unsafe_iterator != started_jobs_.end(); ++unsafe_iterator)
            on_worker_reschedule_(unsafe_iterator->second); // job
        on_worker_close_(shared_from_this());
    }

    void start_read_header()
    {
        BOOST_LOG_TRIVIAL(trace) << "worker start_read_header";
        auto read_buf = std::make_shared<std::array<pack::unit_t, pack::packet_header::bytesize>>();
        net::async_read(
            socket_,
            net::buffer(read_buf->data(), read_buf->size()),
            [self=shared_from_this(), read_buf] (boost::system::error_code ec, std::size_t /*length*/) {
                if (ec)
                {
                    if (ec != boost::asio::error::eof)
                        BOOST_LOG_TRIVIAL(error) << "worker start_read_header err: " << ec.message();
                    self->close();
                }
                else
                {
                    pack::packet_pointer pack = std::make_shared<pack::packet>();
                    pack->header.parse(read_buf->data());

                    switch (pack->header.type)
                    {
                    case pack::msg_t::worker_dereg:
                        self->close();
                        BOOST_LOG_TRIVIAL(trace) << "worker get worker_dereg" << pack->header;
                        break;

                    case pack::msg_t::worker_response:
                        BOOST_LOG_TRIVIAL(trace) << "worker get resp " << pack->header;
                        self->start_read_body(pack);
                        break;

                    case pack::msg_t::ack:
                        BOOST_LOG_TRIVIAL(trace) << "worker get ack " << pack->header;
                        self->on_worker_ack(pack);
                        self->start_read_header();
                        break;

                    case pack::msg_t::set_timer:
                    case pack::msg_t::proxyjoin:
                    case pack::msg_t::err:
                    case pack::msg_t::put:
                    case pack::msg_t::get:
                    case pack::msg_t::worker_reg:
                    case pack::msg_t::worker_push_request:
                    case pack::msg_t::trigger:
                    case pack::msg_t::trigger_reject:
                    {
                        BOOST_LOG_TRIVIAL(error) << "worker packet error " << pack->header;
                        self->start_read_header();
                        break;
                    }
                    }
                }
            });
    }

    void start_read_body(pack::packet_pointer pack)
    {
        BOOST_LOG_TRIVIAL(trace) << "worker start_read_body";
        auto read_buf = std::make_shared<std::vector<pack::unit_t>>(pack->header.datasize);
        net::async_read(
            socket_,
            net::buffer(read_buf->data(), read_buf->size()),
            [self=shared_from_this(), read_buf, pack] (boost::system::error_code ec, std::size_t length) {
                if (ec)
                    BOOST_LOG_TRIVIAL(error) << "worker start_read_body: " << ec.message();
                else
                {
                    pack->data.parse(length, read_buf->data());
                    BOOST_LOG_TRIVIAL(trace) << "worker start self->registered_job_";
                    self->on_worker_response(pack);
                    self->start_read_header();
                }
            });
    }

    void on_worker_ack(pack::packet_pointer pack)
    {
        net::post(
            [self=shared_from_this(), pack] () {
                BOOST_LOG_TRIVIAL(trace) << "job " << pack->header << " get ack. cancel job timer";
                if (pack->empty())
                    return;

                launcher::job_map::accessor it;
                if (bool found = self->started_jobs_.find(it, pack->header); not found)
                    return;

                launcher::job_ptr job = it->second;

                if (!job)
                {
                    BOOST_LOG_TRIVIAL(error) << "get an unknown job: " << pack->header << ". Skip this request";
                    return;
                }

                job->state_ = launcher::job::state::started;
                job->timer_.cancel();
            });
    }

    void on_worker_response(pack::packet_pointer pack)
    {
        net::post(
            [self=shared_from_this(), pack] () {
                launcher::job_map::accessor it;
                if (bool found = self->started_jobs_.find(it, pack->header); not found)
                    return;

                launcher::job_ptr job = it->second;
                self->started_jobs_.erase(it);

                job->state_ = launcher::job::state::finished;
                job->on_completion_(pack);
                BOOST_LOG_TRIVIAL(trace) << "job " << job->pack_->header << " complete";
                self->on_worker_finished_a_job_(self.get(), job);
            });
    }

    void start_write(launcher::job_ptr job)
    {
        BOOST_LOG_TRIVIAL(trace) << "worker start_write";
        started_jobs_.emplace(job->pack_->header, job);

        auto next = std::make_shared<socket_writer::boost_callback>(
            [self=shared_from_this(), job] (boost::system::error_code ec, std::size_t /*length*/) {
                if (ec)
                {
                    BOOST_LOG_TRIVIAL(error) << "worker start write error: " << ec.message();
                    self->close();
                }
                else
                    BOOST_LOG_TRIVIAL(trace) << "worker wrote msg";
            });

        writer_.start_write_socket(job->pack_, next);

//        job->timer_.cancel();
//        using namespace std::chrono_literals;
//        auto timer = std::make_shared<boost::asio::steady_timer>(io_context_);
//        timer->expires_from_now(5ms);
//        timer->async_wait(
//            [self=shared_from_this(), timer, job] (boost::system::error_code error) {
//                switch (error.value())
//                {
//                case boost::system::errc::success: // timer timeout
//                {
//                    job->state_ = launcher::job::state::finished;
//                    job->on_completion_(job->pack_);
//
//                    break;
//                }
//                default:
//                    BOOST_LOG_TRIVIAL(error) << "getting error: " << error.message() << " on launcher start_execute_policy()";
//                }
//            });
    }

    void start_write(pack::packet_pointer pack)
    {
        BOOST_LOG_TRIVIAL(trace) << "worker start_write with " << pack->header;

        auto next = std::make_shared<socket_writer::boost_callback>(
            [self=shared_from_this(), pack] (boost::system::error_code ec, std::size_t /*length*/) {
                if (ec)
                {
                    BOOST_LOG_TRIVIAL(error) << "worker start write error: " << ec.message();
                    self->close();
                    self->socket_.shutdown(tcp::socket::shutdown_send, ec);
                }
                else
                    BOOST_LOG_TRIVIAL(trace) << "worker wrote msg";
            });

        writer_.start_write_socket(pack, next);
    }
};

using worker_ptr = std::shared_ptr<df::worker>;

} // namespace df

#endif // WORKER_HPP__
