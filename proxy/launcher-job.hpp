#pragma once
#ifndef LAUNCHER_JOB_HPP__
#define LAUNCHER_JOB_HPP__

#include "basic.hpp"
#include "serializer.hpp"
#include "uuid.hpp"

#include <boost/signals2.hpp>
#include <boost/asio.hpp>

#include <oneapi/tbb/concurrent_queue.h>
#include <oneapi/tbb/concurrent_hash_map.h>

#include <iterator>
#include <atomic>

namespace slsfs::launcher
{

// Specifies a READ or WRITE jobs managed by launcher
class job
{
public:
    enum class state : std::uint8_t
    {
        registered,
        started,
        finished
    };
    state state_ = state::registered;
    basic::time_point start_time_point_ = basic::now();

    using on_completion_callable = boost::signals2::signal<void (pack::packet_pointer)>;
    on_completion_callable on_completion_;

    pack::packet_pointer pack_;

    boost::asio::steady_timer timer_;

    template<typename Next>
    job (net::io_context& ioc, pack::packet_pointer p, Next && next):
        pack_{p}, timer_{ioc} { on_completion_.connect(next); }
};

using job_ptr = std::shared_ptr<job>;
using job_queue = oneapi::tbb::concurrent_queue<job_ptr>;
using job_map =
    oneapi::tbb::concurrent_hash_map<
        pack::packet_header,
        job_ptr,
        pack::packet_header_full_key_hash_compare>;

} // namespace slsfs::launcher

#endif // LAUNCHER_JOB_HPP__
