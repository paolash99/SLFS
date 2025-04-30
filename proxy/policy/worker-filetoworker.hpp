#pragma once
#ifndef POLICY_WORKER_FILETOWORKER_HPP__
#define POLICY_WORKER_FILETOWORKER_HPP__

#include "../launcher-base-types.hpp"

namespace slsfs::launcher::policy
{

/* Resource provisioning interface policy responsible for assigning files to workers */
class worker_filetoworker : public info
{
public:
    fileid_map fileid_to_worker_;

    virtual
    auto get_assigned_worker(pack::packet_pointer packet_ptr) -> df::worker_ptr
    {
        fileid_to_worker_accessor it;
        if (fileid_to_worker_.find(it, packet_ptr->header))
            return it->second;
        return nullptr;
    }

    virtual
    void start_transfer() {}

    virtual
    auto get_available_worker(pack::packet_pointer packet_ptr,
                              worker_set& current_workers) -> df::worker_ptr = 0;
};

} // namespace slsfs::launcher::policy

#endif // POLICY_WORKER_FILETOWORKER_HPP__
