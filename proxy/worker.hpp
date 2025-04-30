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

    template <typename T>
    concept IsLauncher = requires(T l) {
        { l.on_worker_reschedule(std::declval<launcher::job_ptr>()) } -> std::convertible_to<void>;
        { l.on_worker_close(std::declval<worker_ptr>(), std::declval<pack::packet_pointer>()) } -> std::convertible_to<void>;
        { l.on_worker_finished_a_job(std::declval<worker *>(), std::declval<launcher::job_ptr>()) } -> std::convertible_to<void>;
        { l.on_worker_finished_opening_a_file(std::declval<pack::key_t>(), std::declval<bool>()) } -> std::convertible_to<void>;
        { l.on_worker_finished_renaming_file(std::declval<pack::key_t>()) } -> std::convertible_to<void>;
    };

    using worker_id = std::size_t;

    class worker : public std::enable_shared_from_this<worker>
    {
        net::io_context &io_context_;
        tcp::socket socket_;
        socket_writer::socket_writer<pack::packet, std::vector<pack::unit_t>> writer_;
        std::atomic<bool> valid_ = true;

        launcher::job_map started_jobs_;

        boost::signals2::signal<void(launcher::job_ptr)> on_worker_reschedule_;
        boost::signals2::signal<void(worker_ptr, pack::packet_pointer)> on_worker_close_;
        boost::signals2::signal<void(worker *, launcher::job_ptr)> on_worker_finished_a_job_;
        boost::signals2::signal<void(pack::key_t, bool)> on_worker_finished_opening_a_file;
        boost::signals2::signal<void(pack::key_t)> on_worker_finished_renaming_file;
        boost::asio::ip::tcp::endpoint endpoint;

    public:
        basic::time_point started_ = basic::now();
        uuid::uuid const id_ = uuid::gen_uuid();
        worker_id const worker_id_ = uuid::hash(id_);

    public:
        template <typename Launcher>
            requires IsLauncher<Launcher>
        worker(net::io_context &io, tcp::socket socket, Launcher &l) : io_context_{io},
                                                                       socket_{std::move(socket)},
                                                                       writer_{io, socket_}
        {
            on_worker_reschedule_.connect([&l](launcher::job_ptr job)
                                          { l.on_worker_reschedule(job); });
            on_worker_close_.connect([&l](worker_ptr p, pack::packet_pointer t)
                                     { l.on_worker_close(p, t); });
            on_worker_finished_a_job_.connect([&l](worker *p, launcher::job_ptr job)
                                              { l.on_worker_finished_a_job(p, job); });
            on_worker_finished_opening_a_file.connect([&l](pack::key_t s, bool success)
                                                      { l.on_worker_finished_opening_a_file(s, success); });
            on_worker_finished_renaming_file.connect([&l](pack::key_t s)
                                                     { l.on_worker_finished_renaming_file(s); });
        }

        bool is_valid() { return valid_.load(); }
        void soft_close() { valid_.store(false); }
        int pending_jobs() { return started_jobs_.size(); }

        void set_endpoint(boost::asio::ip::tcp::endpoint new_endpoint)
        {
            endpoint = new_endpoint;
        }

        void close(pack::packet_pointer to_transfer = nullptr)
        {
            valid_.store(false);

            boost::system::error_code ec;
            socket_.shutdown(tcp::socket::shutdown_both, ec);

            on_worker_close_(shared_from_this(), to_transfer);

            for (auto unsafe_iterator = started_jobs_.begin(); unsafe_iterator != started_jobs_.end(); ++unsafe_iterator)
                on_worker_reschedule_(unsafe_iterator->second); // job
            // BOOST_LOG_TRIVIAL(info) << "worker [" << id_.short_hash() << "] closed. Reschedule " << started_jobs_.size() << " jobs";
        }

        void start_read_header()
        {
            BOOST_LOG_TRIVIAL(trace) << "worker start_read_header";
            auto read_buf = std::make_shared<std::array<pack::unit_t, pack::packet_header::bytesize>>();
            net::async_read(
                socket_,
                net::buffer(read_buf->data(), read_buf->size()),
                [self = shared_from_this(), read_buf](boost::system::error_code ec, std::size_t /*length*/)
                {
                    if (ec)
                    {
                        if (ec != boost::asio::error::eof)
                            BOOST_LOG_TRIVIAL(error) << "worker start_read_header err: " << ec.message();
                        self->close();
                        return;
                    }

                    pack::packet_pointer pack = std::make_shared<pack::packet>();
                    pack->header.parse(read_buf->data());

                    switch (pack->header.type)
                    {
                    case pack::msg_t::worker_dereg:
                        self->start_read_body(pack);
                        BOOST_LOG_TRIVIAL(trace) << "worker get worker_dereg" << pack->header;
                        break;

                    case pack::msg_t::worker_response:
                    case slsfs::pack::msg_t::open_file_success:
                    case slsfs::pack::msg_t::open_file_not_found:
                    case slsfs::pack::msg_t::rename_success:
                    case slsfs::pack::msg_t::rename_fail:
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
                    case slsfs::pack::msg_t::proxy_send_ok:
                    case slsfs::pack::msg_t::proxy_send_error:
                    case slsfs::pack::msg_t::proxy_send_ip:
                    case pack::msg_t::err:
                    case pack::msg_t::put:
                    case pack::msg_t::get:
                    case pack::msg_t::cache_transfer:
                    case pack::msg_t::worker_reg:
                    case pack::msg_t::worker_push_request:
                    case pack::msg_t::trigger:
                    case pack::msg_t::trigger_reject:
                        BOOST_LOG_TRIVIAL(error) << "worker receive a strange packet " << pack->header;
                        self->start_read_header();
                        break;
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
                [self = shared_from_this(), read_buf, pack](boost::system::error_code ec, std::size_t length)
                {
                    if (ec)
                    {
                        BOOST_LOG_TRIVIAL(error) << "worker start_read_body: " << ec.message();
                        return;
                    }

                    pack->data.parse(length, read_buf->data());
                    if (pack->header.type == pack::msg_t::worker_dereg)
                    {
                        self->close(pack);
                        return;
                    }

                    if (pack->header.type == pack::msg_t::open_file_success)
                    {
                        pack::key_t key;
                        std::memcpy(&key, pack->data.buf.data(), sizeof(key));
                        self->on_worker_finished_opening_a_file(key, true);

                        // Direct communication ip/port
                        boost::asio::ip::address_v4::bytes_type bytes = self->endpoint.address().to_v4().to_bytes();
                        std::uint16_t port = pack::hton(self->endpoint.port());
                        pack->data.buf.resize(sizeof(bytes) + sizeof(port));

                        std::memcpy(pack->data.buf.data(), &bytes, sizeof(bytes));
                        std::memcpy(pack->data.buf.data() + sizeof(bytes), &port, sizeof(port));
                    }
                    else if (pack->header.type == pack::msg_t::open_file_not_found)
                    {
                        pack::key_t key;
                        std::memcpy(&key, pack->data.buf.data(), sizeof(key));

                        self->on_worker_finished_opening_a_file(key, false);
                        pack->data.buf = slsfs::base::to_buf("Error: File not found");
                    }
                    else if (pack->header.type == pack::msg_t::rename_success)
                    {
                        pack::key_t key;
                        std::memcpy(&key, pack->data.buf.data(), sizeof(key));

                        self->on_worker_finished_renaming_file(key);
                        pack->data.buf = slsfs::base::to_buf("OK");
                    }
                    else if (pack->header.type == pack::msg_t::rename_fail)
                    {
                        pack::key_t key;
                        std::memcpy(&key, pack->data.buf.data(), sizeof(key));
                        self->on_worker_finished_renaming_file(key);
                        pack->data.buf = slsfs::base::to_buf("Error: Invalid argumnts");
                    }

                    BOOST_LOG_TRIVIAL(trace) << "worker start self->registered_job_";
                    self->on_worker_response(pack);
                    self->start_read_header();
                });
        }

        void on_worker_ack(pack::packet_pointer pack)
        {
            net::post(
                [self = shared_from_this(), pack]()
                {
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
                [self = shared_from_this(), pack]()
                {
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
            if (job->pack_->header.type == pack::msg_t::proxy_send_ip)
            {
                // Direct communication ip/port
                boost::asio::ip::address_v4::bytes_type bytes = endpoint.address().to_v4().to_bytes();
                std::uint16_t port = pack::hton(endpoint.port());
                job->pack_->data.buf.resize(sizeof(bytes) + sizeof(port));
                std::memcpy(job->pack_->data.buf.data(), &bytes, sizeof(bytes));
                std::memcpy(job->pack_->data.buf.data() + sizeof(bytes), std::addressof(port), sizeof(port));

                net::post(
                    [self = shared_from_this(), job]()
                    {
                        job->state_ = launcher::job::state::finished;
                        job->on_completion_(job->pack_);
                        self->on_worker_finished_a_job_(self.get(), job);
                    });
            }
            else if (job->pack_->header.type == pack::msg_t::proxy_send_error || job->pack_->header.type == pack::msg_t::proxy_send_ok)
            {
                net::post(
                    [self = shared_from_this(), job]()
                    {
                        job->state_ = launcher::job::state::finished;
                        job->on_completion_(job->pack_);
                        self->on_worker_finished_a_job_(self.get(), job);
                    });
            }
            else
            {
                started_jobs_.emplace(job->pack_->header, job);
                auto next = std::make_shared<socket_writer::boost_callback>(
                    [self = shared_from_this(), job](boost::system::error_code ec, std::size_t /*length*/)
                    {
                        if (ec)
                        {
                            BOOST_LOG_TRIVIAL(error) << "worker start write error: " << ec.message();
                            self->close();
                        }
                        else
                            BOOST_LOG_TRIVIAL(trace) << "worker wrote msg";
                    });

                writer_.start_write_socket(job->pack_, next);
            }

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
                [self = shared_from_this(), pack](boost::system::error_code ec, std::size_t /*length*/)
                {
                    if (ec)
                    {
                        BOOST_LOG_TRIVIAL(error) << "worker start write error: " << ec.message();
                        self->close();
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
