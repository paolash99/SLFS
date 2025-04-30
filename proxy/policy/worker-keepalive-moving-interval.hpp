#pragma once
#ifndef POLICY_WORKER_KEEPALIVE_MOVING_INTERVAL_HPP__
#define POLICY_WORKER_KEEPALIVE_MOVING_INTERVAL_HPP__


#include "worker-keepalive.hpp"

#include <boost/log/trivial.hpp>

#include <map>
#include <chrono>
#include <vector>
#include <cstdint>

namespace slsfs::launcher::policy
{

/* Ring buffer implementation of a simple moving average */
class SMA {
    private:
        std::uint32_t N = 1;
        std::uint32_t index = 0;
        std::vector<std::uint32_t> previousInputs;
        std::uint32_t sum = 0;

    public:
        SMA(std::uint32_t buffer_size) {
            if (buffer_size != 0)
                N = buffer_size;
            previousInputs.resize(N, 0);
        }

        std::uint32_t record(std::uint32_t input) {
            sum -= previousInputs[index];
            sum += input;
            previousInputs[index] = input;

            if (++index == N)
                index = 0;
            return (sum + (N / 2)) / N;
        }

        std::uint32_t get_sma() {
            return (sum + (N / 2)) / N;
        }
};

/* Keeps track of the simple moving average of time intervals between incoming requests
   requests for each worker and adapts the keep alive accordingly */
class keepalive_moving_interval : public worker_keepalive
{
    private:
        std::uint32_t sma_buffer_size;
        double error_margin;
        pack::waittime_type default_wait_time_;
        pack::waittime_type concurrency_threshold;
        std::map<df::worker_id, SMA> wait_times_;
        std::map<df::worker_id, std::chrono::high_resolution_clock::time_point> last_request_;

    public:
        keepalive_moving_interval (
            std::uint32_t buffer_size,
            pack::waittime_type default_ms,
            pack::waittime_type threshold,
            double error_ms):
            sma_buffer_size{buffer_size}, error_margin{error_ms},
            default_wait_time_{default_ms}, concurrency_threshold{threshold} {}

        void set_worker_keepalive(df::worker_ptr worker_ptr_shared) override
        {
            df::worker_id worker_ptr = worker_ptr_shared->worker_id_;
            pack::waittime_type keep_alive;

            auto it = wait_times_.find(worker_ptr);
            if (it == wait_times_.end())
                return;

            SMA& sma = it->second;

            if (sma.get_sma() < concurrency_threshold) {
                keep_alive = default_wait_time_;
            }
            else {
                if (sma.get_sma() > 100) {
                    keep_alive = sma.get_sma() +
                    (error_margin * sma.get_sma());
                }
                else {
                    keep_alive = sma.get_sma() + (100 / sma.get_sma());
                }
            }

            BOOST_LOG_TRIVIAL(info) << "SMA sent = " << keep_alive;
            send_worker_keepalive(worker_ptr_shared, keep_alive);
        }

        // Updating the policy
        void started_a_new_job(df::worker* worker_ptr, job_ptr) override
        {
            df::worker_id worker_id = worker_ptr->worker_id_;
            if (last_request_.count(worker_id) == 0)
            {
                //wait_times_[worker_id] = SMA(sma_buffer_size);
//                BOOST_LOG_TRIVIAL(info) << "sma_buffer_size: " << sma_buffer_size;
                wait_times_.emplace(worker_id, sma_buffer_size);

                auto it = wait_times_.find(worker_id);
                if (it == wait_times_.end())
                    return;

                it->second.record(default_wait_time_);
                last_request_[worker_id] = std::chrono::high_resolution_clock::now();
                return;
            }

            auto const current_request_time = std::chrono::high_resolution_clock::now();
            auto const last_request_time = last_request_[worker_id];
            last_request_[worker_id] = current_request_time;

            auto request_interval = std::chrono::duration_cast<std::chrono::milliseconds>(
                current_request_time - last_request_time).count();


            if (request_interval > concurrency_threshold){
                auto it = wait_times_.find(worker_id);
                if (it == wait_times_.end())
                    return;

                BOOST_LOG_TRIVIAL(debug) << "recording request interval = " << request_interval
                                         << ", SMA = " << it->second.record(request_interval);
            }
        }
};

} // namespace slsfs::launcher::policy

#endif // POLICY_WORKER_KEEPALIVE_MOVING_INTERVAL_HPP__
