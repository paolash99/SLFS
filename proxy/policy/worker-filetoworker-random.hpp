#pragma once
#ifndef POLICY_WORKER_FILETOWORKER_RANDOM_ASSIGN_HPP__
#define POLICY_WORKER_FILETOWORKER_RANDOM_ASSIGN_HPP__

#include "worker-filetoworker.hpp"

#include <limits>

namespace slsfs::launcher::policy
{

/* Assigns new files to the worker with the lowest amount of assigned files */
class random_assign : public worker_filetoworker
{
    static
    auto engine() -> std::mt19937&
    {
        static thread_local std::random_device random_device;
        static thread_local std::mt19937 rng(random_device());
        return rng;
    }

public:
    auto get_available_worker(pack::packet_pointer /*packet_ptr*/,
                              worker_set& current_workers) -> df::worker_ptr override
    {
        df::worker_ptr pick = nullptr;

        if (!current_workers.empty())
            do
            {
                std::uniform_int_distribution<int> dist(0, current_workers.size()-1);
                pick = std::next(current_workers.begin(), dist(engine()))->first;
            } while (not pick->is_valid());

        return pick;
    }
};

} // namespace slsfs::launcher::policy

#endif // POLICY_WORKER_FILETOWORKER_RANDOM_ASSIGN_HPP__
