#pragma once
#ifndef POLICY_WORKER_LAUNCH_CONST_LIMIT_LAUNCH_HPP__
#define POLICY_WORKER_LAUNCH_CONST_LIMIT_LAUNCH_HPP__

#include "worker-launch.hpp"
#include <boost/log/trivial.hpp>

namespace slsfs::launcher::policy
{

/* policy that launches a worker if the number of pending jobs of each current worker
exceeds a preset threshold  */
class const_limit_launch : public worker_launch
{
protected:
    int threshold_;
public:
    const_limit_launch(): const_limit_launch(3) {}
    const_limit_launch(int threshold): worker_launch(10), threshold_{threshold} {}
    const_limit_launch(int threshold, unsigned int max_outstanding_starting_request):
        worker_launch(max_outstanding_starting_request), threshold_{threshold} {}

    bool should_start_new_worker (worker_set& ws) override
    {
        for (auto [worker_ptr, _notused] : ws)
            if (worker_ptr->pending_jobs() <= threshold_ and worker_ptr->is_valid())
                return false; // have an underload worker
        return true;
    }
};
} // namespace slsfs::launcher

#endif // POLICY_WORKER_LAUNCH_CONST_LIMIT_LAUNCH_HPP__
