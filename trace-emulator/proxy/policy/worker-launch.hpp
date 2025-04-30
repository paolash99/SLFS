#pragma once
#ifndef POLICY_WORKER_LAUNCH_HPP__
#define POLICY_WORKER_LAUNCH_HPP__

#include "../launcher-base-types.hpp"
#include <atomic>

namespace slsfs::launcher::policy
{

/* Resource provisioning policy interface responsible for starting and managing the pool of workers */
class worker_launch : public info
{
protected:
    int const max_outstanding_starting_request_;
    std::atomic<int> starting_worker_count_ = 0;
    std::atomic<int> starter_ = 0;

public:
    worker_launch(int max_outstanding_starting_request):
        max_outstanding_starting_request_{max_outstanding_starting_request} {}

    void starting_a_new_worker() override {
        starting_worker_count_.fetch_add(1, std::memory_order_seq_cst);
    }

    void registered_a_new_worker(df::worker*) override {
        starting_worker_count_.fetch_sub(1, std::memory_order_seq_cst);
    }

    int get_ideal_worker_count_delta (worker_set& ws)
    {
        int should_start = std::max(get_ideal_worker_count(ws) - (static_cast<int>(ws.size()) + starting_worker_count_.load()), 0);
        int can_start    = std::max(max_outstanding_starting_request_ - starting_worker_count_.load(), 0);

        //BOOST_LOG_TRIVIAL(debug) << "starting_worker_count_: " << starting_worker_count_.load() ;

        return std::min(should_start, can_start);
    }

    void schedule_a_new_job(worker_set& ws, job_ptr) override
    {
        if (ws.empty())
            starter_ = 1;
        else
            starter_ = 0;
    }

    void reschedule_a_job (worker_set& ws, job_ptr) override
    {
        if (ws.empty())
            starter_ = 1;
        else
            starter_ = 0;
    }

    virtual
    int get_ideal_worker_count(worker_set &) {
        return starter_.load();
    }
};

} // namespace slsfs::launcher::policy

#endif // POLICY_WORKER_LAUNCH_HPP__
