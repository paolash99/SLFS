#pragma once
#ifndef POLICY_WORKER_KEEPALIVE_CONST_TIME_HPP__
#define POLICY_WORKER_KEEPALIVE_CONST_TIME_HPP__

#include "worker-keepalive.hpp"

namespace slsfs::launcher::policy
{

/* Sets a constant keep alive time for idle workers */
class keepalive_const_time : public worker_keepalive
{
    pack::waittime_type waittime_;

public:
    keepalive_const_time (pack::waittime_type ms): waittime_{ms} {}

    void set_worker_keepalive(df::worker_ptr worker) override {
        send_worker_keepalive(worker, waittime_);
    }
};

} // namespace slsfs::launcher::policy

#endif // POLICY_WORKER_KEEPALIVE_CONST_TIME_HPP__
