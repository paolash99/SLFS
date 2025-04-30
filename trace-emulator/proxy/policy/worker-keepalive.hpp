#pragma once
#ifndef POLICY_WORKER_KEEPALIVE_HPP__
#define POLICY_WORKER_KEEPALIVE_HPP__

#include "../launcher-base-types.hpp"

namespace slsfs::launcher::policy
{

/* Resource provisioning policy interface responsible for deciding a keep alive time for workers after
they have processed their last request */
class worker_keepalive : public info
{
protected:
    void send_worker_keepalive(df::worker_ptr worker, pack::waittime_type duration_in_ms)
    {
        pack::packet_pointer pack = std::make_shared<pack::packet>();
        pack->header.gen();
        pack->header.type = pack::msg_t::set_timer;
        pack->data.buf = std::vector<pack::unit_t>(sizeof(pack::waittime_type));

        duration_in_ms = pack::hton(duration_in_ms);
        std::memcpy(pack->data.buf.data(), &duration_in_ms, sizeof(duration_in_ms));

        worker->start_write(pack);
    }

public:
    virtual
    void set_worker_keepalive(df::worker_ptr worker) {
        send_worker_keepalive(worker, 10000);
    }
};

} // namespace slsfs::launcher::policy

#endif // POLICY_WORKER_KEEPALIVE_HPP__
