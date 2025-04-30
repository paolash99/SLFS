#pragma once

#ifndef WORKER_HPP__
#define WORKER_HPP__

#include <slsfs.hpp>

namespace slsfsdf
{

using boost::asio::ip::tcp;

class worker : public std::enable_shared_from_this<worker>
{
    boost::asio::io_context& io_;
    boost::asio::io_context::strand write_io_strand_;
    std::function<void(slsfs::pack::packet_pointer)> job_;
    boost::asio::steady_timer recv_deadline_;

public:
    template<typename Func>
    worker(boost::asio::io_context& io, tcp::socket& s, Func job):
        io_{io}, write_io_strand_{io}, recv_deadline_{io} {}

    void start_job(slsfs::pack::packet_pointer pack)
    {
        boost::asio::post(
            io_,
            [self=this->shared_from_this(), pack] {
                slsfs::log::log<slsfs::log::level::debug>("Start Job");
                //std::invoke(self->job_, pack);
                //self->start_write(pack);
            });
    }
};

} // namespace slsfsdf
#endif // WORKER_HPP__
