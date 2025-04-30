#pragma once
#ifndef POLICY_WORKER_KEEPALIVE_MOVING_INTERVAL_GLOBAL_HPP__
#define POLICY_WORKER_KEEPALIVE_MOVING_INTERVAL_GLOBAL_HPP__

#include "worker-keepalive.hpp"

#include <boost/log/trivial.hpp>

#include <map>
#include <chrono>
#include <vector>
#include <cstdint>

namespace slsfs::launcher::policy
{

/* Ring buffer implementation of a simple moving average */
class SMA_global
{
private:
    std::uint32_t N_ = 1;
    std::uint32_t index_ = 0;
    std::vector<std::uint32_t> previous_inputs_;
    std::uint32_t sum_ = 0;
    long int iter_counter_ = 1;
    double CV_ = 0.25;

public:
    SMA_global(std::uint32_t buffer_size)
    {
        if (buffer_size != 0)
            N_ = buffer_size;
        previous_inputs_.resize(N_, 0);
    }

    auto record(std::uint32_t input) -> std::uint32_t
    {
        iter_counter_ += 1;
        sum_ -= previous_inputs_[index_];
        sum_ += input;
        previous_inputs_[index_] = input;

        if (++index_ == N_)
            index_ = 0;

        if (iter_counter_ < N_ + 1)
            return (sum_ + (N_ / 2)) / iter_counter_;
        else
            return (sum_ + (N_ / 2)) / N_;
    }

    std::uint32_t get_sma()
    {
        if (iter_counter_ < N_ + 1)
            return (sum_ + (N_ / 2)) / iter_counter_;
        else
            return (sum_ + (N_ / 2)) / N_;
    }

    template<typename Iterator>
    auto stats(Iterator start, Iterator end) -> int
    {
        int const size = std::distance(start, end);

        double sum = std::accumulate(start, end, 0.0);
        double mean = sum / size, var = 0;

        for (; start != end; start++)
            var += std::pow((*start) - mean, 2);
        var /= size;

        return std::sqrt(var);
    }

    bool is_within_2_stdev()
    {
        int mean = sum_ / N_;
        int standard_deviation = 0;

        for(std::uint32_t i = 0; i < N_; ++i)
            standard_deviation += pow(previous_inputs_[i] - mean, 2);

        double stdev = stats(previous_inputs_.begin(), previous_inputs_.end());
        BOOST_LOG_TRIVIAL(trace) << "STDEV = " << stdev << ", Mean = " << mean << ", CV_ = " << stdev / mean;
        if (stdev / mean  < CV_)
            return true;
        return false;
    }
};

/* Keeps track of the simple moving average of time intervals between incoming requests
   requests for each worker and adapts the keep alive accordingly */
class keepalive_moving_interval_global : public worker_keepalive
{
private:
    SMA_global sma_;
    double error_margin_;
    bool first_req_ = true;
    pack::waittime_type default_wait_time_;
    pack::waittime_type concurrency_threshold_;
    std::chrono::high_resolution_clock::time_point last_request_;


public:
    keepalive_moving_interval_global (
        std::uint32_t buffer_size,
        pack::waittime_type default_ms,
        pack::waittime_type threshold,
        double error_ms):
        sma_{buffer_size}, error_margin_{error_ms},
        default_wait_time_{default_ms}, concurrency_threshold_{threshold} {}

    void set_worker_keepalive(df::worker_ptr worker_ptr_shared) override
    {
        pack::waittime_type keep_alive;
        bool use_sma = sma_.is_within_2_stdev();
        //BOOST_LOG_TRIVIAL(debug) << "USE SMA: " << use_sma;

        if (sma_.get_sma() < concurrency_threshold_ or !use_sma)
            keep_alive = default_wait_time_;
        else
        {
            if (sma_.get_sma() > 100)
                keep_alive = sma_.get_sma() +
                    (error_margin_ * sma_.get_sma()) + 300;
            else
                keep_alive = sma_.get_sma() + (100 / sma_.get_sma()) + 300;
        }

        BOOST_LOG_TRIVIAL(trace) << "SMA sent = " << keep_alive;
        send_worker_keepalive(worker_ptr_shared, keep_alive);
    }

    // Updating the policy
    void started_a_new_job(df::worker* , job_ptr) override
    {
        if (first_req_)
        {
            first_req_ = false;
            last_request_ = std::chrono::high_resolution_clock::now();
            return;
        }

        auto const current_request_time = std::chrono::high_resolution_clock::now();
        auto const last_request_time = last_request_;
        last_request_ = current_request_time;

        auto request_interval = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_request_time - last_request_time).count();

        if (request_interval > concurrency_threshold_)
        {
            sma_.record(request_interval);
            BOOST_LOG_TRIVIAL(trace) << "recording request interval = " << request_interval
                                     << ", SMA = " << sma_.get_sma();
        }
        else
            BOOST_LOG_TRIVIAL(trace) << "request interval =" << request_interval << ", current SMA =" << sma_.get_sma();
    }
};

} // namespace slsfs::launcher::policy

#endif // POLICY_WORKER_KEEPALIVE_MOVING_IN_TERVAL_GLOBAL_HPP__
