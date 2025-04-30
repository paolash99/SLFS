#pragma once
#ifndef POLICY_WORKER_LAUNCH_PRESTART_ONE_HPP__
#define POLICY_WORKER_LAUNCH_PRESTART_ONE_HPP__

#include "worker-launch.hpp"

#include <chrono>

namespace slsfs::launcher::policy
{

namespace {
    using namespace std::chrono_literals;
}

/* policy that launches a worker if the number of pending jobs of each current worker
exceeds a preset threshold  */
class prestart_one : public worker_launch
{
    std::chrono::milliseconds no_older_than_;
public:
    prestart_one(int no_older_than): worker_launch(10),
                                     no_older_than_{no_older_than * 1ms} {}

    bool should_start_new_worker (worker_set& ws) override
    {
        for (auto [worker_ptr, _notused] : ws)
        {
            //BOOST_LOG_TRIVIAL(info) << std::chrono::high_resolution_clock::now() - worker_ptr->started_ << " less then " << no_older_than_;
            if (worker_ptr->is_valid() &&
                std::chrono::high_resolution_clock::now() - worker_ptr->started_ < no_older_than_)
            {
                worker_ptr->started_ = std::chrono::high_resolution_clock::now();
                return false; // found one worker that is still young
            }
        }

        return true;
    }
};
} // namespace slsfs::launcher

#endif // POLICY_WORKER_LAUNCH_PRESTART_ONE_HPP__
