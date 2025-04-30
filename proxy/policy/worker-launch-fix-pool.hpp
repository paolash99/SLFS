#pragma once
#ifndef POLICY_WORKER_LAUNCH_FIX_POOL_HPP__
#define POLICY_WORKER_LAUNCH_FIX_POOL_HPP__

#include "worker-launch.hpp"
#include <atomic>

namespace slsfs::launcher::policy
{

/* Resource provisioning policy interface responsible for starting and managing the pool of workers */
class worker_launch_fix_pool : public worker_launch
{
public:
    worker_launch_fix_pool (int max_outstanding_starting_request, int size):
        worker_launch{max_outstanding_starting_request} {
        worker_launch::default_pool_value_ = size;
    }

    int get_ideal_worker_count(worker_set &) override {
        return worker_launch::default_pool_value_;
    }
};

} // namespace slsfs::launcher::policy

#endif // POLICY_WORKER_LAUNCH_FIX_POOL_HPP__
