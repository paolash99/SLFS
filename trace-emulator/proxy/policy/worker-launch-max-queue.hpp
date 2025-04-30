#pragma once
#ifndef POLICY_WORKER_LAUNCH_MAX_AVERAGE_QUEUE_HPP__
#define POLICY_WORKER_LAUNCH_MAX_AVERAGE_QUEUE_HPP__

#include "worker-launch.hpp"

#include <boost/log/trivial.hpp>

namespace slsfs::launcher::policy
{

/* policy that launches a worker if the number of pending jobs of each current worker
exceeds a preset threshold  */
class max_queue : public worker_launch
{
    std::uint64_t const max_average_queue_; // max queue size (bytes)
    std::atomic<std::uint64_t> queue_size_ = 0;
    std::atomic<int> ideal_worker_count_ = 0;

public:
    max_queue(int max_outstanding_starting_request, std::uint64_t max_queue):
        worker_launch{max_outstanding_starting_request},
        max_average_queue_ {max_queue /*kb*/ * 1024} {}

    void execute() override
    {
        ideal_worker_count_.store(queue_size_ / max_average_queue_);

        BOOST_LOG_TRIVIAL(debug) << "queue size: " << queue_size_
                                 << " internal count: " << ideal_worker_count_;
    }

    void finished_a_job (df::worker *, job_ptr job) override {
        queue_size_.fetch_sub(job->pack_->header.datasize, std::memory_order_seq_cst);
    }

    void schedule_a_new_job (worker_set& ws, job_ptr job) override
    {

        queue_size_.fetch_add(job->pack_->header.datasize, std::memory_order_seq_cst);
        worker_launch::schedule_a_new_job(ws, job);
    }

    int get_ideal_worker_count(worker_set & ws) override {
        return std::max(worker_launch::get_ideal_worker_count(ws), ideal_worker_count_.load());
    }
};

} // namespace slsfs::launcher

#endif // POLICY_WORKER_LAUNCH_MAX_AVERAGE_QUEUE_HPP__
