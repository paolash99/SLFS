#pragma once
#ifndef POLICY_WORKER_LAUNCH_ADAPTIVE_MAX_LOAD_HPP__
#define POLICY_WORKER_LAUNCH_ADAPTIVE_MAX_LOAD_HPP__

#include "worker-launch-const-limit-launch.hpp"

#include <oneapi/tbb/concurrent_hash_map.h>

#include <chrono>
#include <atomic>

namespace slsfs::launcher::policy
{

/*
   sets the max wait time for one request to finish
   e.g clients want 1 request must finish in 2 second
   we calculate the average load that a worker can process in 2 seconds
   e.g 3000 requests in 2 seconds
   if all worker have a job queue that > 3000
   start a new worker
 */

class adaptive_max_load : public const_limit_launch
{
    std::chrono::milliseconds const interval_;
    double const minimum_process_rate_;
    basic::time_point last_update_ = basic::now();

    oneapi::tbb::concurrent_hash_map<df::worker*, std::atomic<unsigned int>> finished_jobs_;
    using finished_jobs_accessor = decltype(finished_jobs_)::accessor;

    oneapi::tbb::concurrent_hash_map<df::worker*, double> process_rate_;
    using process_rate_accessor = decltype(process_rate_)::accessor;

public:
    adaptive_max_load(int max_latency, int minimum_process_rate_in_seconds, unsigned int max_outstanding_starting_request):
        const_limit_launch(300, max_outstanding_starting_request),
        interval_{max_latency * 1ms},
        minimum_process_rate_{minimum_process_rate_in_seconds / 1000.0} {}

    void finished_a_job(df::worker* worker_ptr) override
    {
        finished_jobs_accessor it;
        if (finished_jobs_.find(it, worker_ptr))
            it->second.fetch_add(1, std::memory_order_relaxed);
        else
            finished_jobs_.emplace(worker_ptr, 0);
    }

    void execute() override
    {
        if (basic::now() - last_update_ < interval_)
            return;

        std::chrono::milliseconds const time_elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                basic::now() - last_update_);

        for (auto&& [worker_ptr, job_count] : finished_jobs_)
        {
            [[maybe_unused]]
            auto ptrcopy = worker_ptr->shared_from_this();
            process_rate_.emplace(worker_ptr, std::max(minimum_process_rate_,
                                                    static_cast<double>(job_count.load()) / time_elapsed.count()));
            finished_jobs_.emplace(worker_ptr, 0);
        }

        last_update_ = basic::now();
    }

    bool should_start_new_worker (worker_set& ws) override
    {
        BOOST_LOG_TRIVIAL(info) << "should_start_new_worker " << counter_.load() << " " << max_outstanding_starting_request_ << " " << worker_launch::should_start_new_worker(ws);
        if (reaches_max_pending_start_worker_requests())
            return false;

        if (worker_launch::should_start_new_worker(ws))
            return true;

        for (auto [worker_ptr, _notused] : ws)
        {
            process_rate_accessor it;
            if (worker_ptr->is_valid() &&
                process_rate_.find(it, worker_ptr.get()) &&
                (worker_ptr->pending_jobs() / static_cast<double>(interval_.count()) < it->second))
                    return false; // found one worker that is underload
        }
        return true;
    }
};
} // namespace slsfs::launcher

#endif // POLICY_WORKER_LAUNCH_ADAPTIVE_MAX_LOAD_HPP__
