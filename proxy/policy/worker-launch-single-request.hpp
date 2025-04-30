#pragma once
#ifndef POLICY_WORKER_LAUNCH_SINGLE_REQUEST_HPP__
#define POLICY_WORKER_LAUNCH_SINGLE_REQUEST_HPP__

#include "worker-launch.hpp"

#include <boost/log/trivial.hpp>

namespace slsfs::launcher::policy
{

/* policy that launches a worker if the number of pending jobs of each current worker
exceeds a preset threshold  */
class single_request : public worker_launch
{
    std::atomic<int> request_count_;
public:
    single_request (int max_outstanding_starting_request):
        worker_launch{max_outstanding_starting_request} {}

    void finished_a_job (df::worker *ws, job_ptr job) override {
        request_count_.fetch_sub(1, std::memory_order_seq_cst);
        worker_launch::finished_a_job(ws, job);
    }

    void schedule_a_new_job (worker_set& ws, job_ptr job) override
    {
        request_count_.fetch_add(1, std::memory_order_seq_cst);
        worker_launch::schedule_a_new_job(ws, job);
    }

    int get_ideal_worker_count (worker_set & ws) override {
        return std::max(worker_launch::get_ideal_worker_count(ws), request_count_.load());
    }
};

} // namespace slsfs::launcher

#endif // POLICY_WORKER_LAUNCH_SINGLE_REQUEST_HPP__
