#pragma once
#ifndef POLICY_WORKER_LAUNCH_CONST_AVERAGE_LOAD_HPP__
#define POLICY_WORKER_LAUNCH_CONST_AVERAGE_LOAD_HPP__

#include "worker-launch.hpp"

#include <chrono>

namespace slsfs::launcher::policy
{

/* policy that launches a worker if the number of pending jobs of each current worker
exceeds a preset threshold  */
class const_average_load : public worker_launch
{
    std::uint64_t const max_average_load_; // bytes per second
    basic::time_point last_update_ = basic::now();
    std::atomic<int> ideal_worker_count_ = 0;
    std::atomic<std::uint64_t> processed_size_ = 0;

public:
    const_average_load(int max_outstanding_starting_request, std::uint64_t max_average_load):
        worker_launch{max_outstanding_starting_request},
        max_average_load_{max_average_load /*kbps*/ * 1024} {}

    void execute() override
    {
        std::uint64_t const duration = std::chrono::duration_cast<std::chrono::milliseconds>(basic::now() - last_update_).count();
        last_update_ = basic::now();

        std::uint64_t const normalized_processed_size_ = (processed_size_ * 1000) / duration;
        processed_size_ = 0;

        ideal_worker_count_.store(normalized_processed_size_ / max_average_load_);

        BOOST_LOG_TRIVIAL(debug) << "normalized_processed_size: " << normalized_processed_size_
                                 << " get_ideal_worker_count: " << ideal_worker_count_;
    }

    void finished_a_job (df::worker *, job_ptr job) override {
        processed_size_.fetch_add(job->pack_->header.datasize, std::memory_order_relaxed);
    }

    void started_a_new_job (df::worker *, job_ptr job) override {
        processed_size_.fetch_add(job->pack_->header.datasize, std::memory_order_relaxed);
    }

    int get_ideal_worker_count(worker_set &ws) override {
        return std::max(worker_launch::get_ideal_worker_count(ws), ideal_worker_count_.load());
    }
};

} // namespace slsfs::launcher

#endif // POLICY_WORKER_LAUNCH_CONST_AVERAGE_LOAD_HPP__
