#pragma once
#ifndef LAUNCHER_BASE_TYPES_HPP__
#define LAUNCHER_BASE_TYPES_HPP__

#include "worker.hpp"
#include "launcher-job.hpp"

#include <oneapi/tbb/concurrent_queue.h>
#include <oneapi/tbb/concurrent_hash_map.h>
#include <oneapi/tbb/concurrent_vector.h>

#include <fstream>
#include <type_traits>
#include <concepts>
#include <chrono>

namespace slsfs::launcher
{

using worker_set = oneapi::tbb::concurrent_hash_map<df::worker_ptr, int /* not used */>;
using worker_set_accessor = worker_set::accessor;

using fileid_map =
    oneapi::tbb::concurrent_hash_map<
        pack::packet_header,
        df::worker_ptr,
        pack::packet_header_key_hash_compare>;

using fileid_to_worker_accessor = fileid_map::accessor;
using fileid_worker_pair        = fileid_map::value_type;

namespace policy
{

struct info
{
    virtual void execute() {}
    virtual void schedule_a_new_job (worker_set&, job_ptr) {}
    virtual void reschedule_a_job   (worker_set&, job_ptr) {}
    virtual void started_a_new_job  (df::worker*, job_ptr) {}
    virtual void finished_a_job     (df::worker*, job_ptr) {}
    virtual void starting_a_new_worker() {}
    virtual void registered_a_new_worker(df::worker*) {}
    virtual void deregistered_a_worker  (df::worker*) {}
};

} // policy

namespace {
    using namespace std::chrono_literals;
}

class reporter : public policy::info
{
    std::string const report_file_;
    basic::time_point start_time_ = basic::now();
    std::atomic<unsigned int> started_worker_ = 0;
    oneapi::tbb::concurrent_queue<basic::time_point> pending_worker_timestamps_;

    struct worker_info
    {
        std::chrono::nanoseconds start_duration;
        basic::time_point start_time = basic::now();
        basic::time_point end_time   = start_time;
        std::atomic<unsigned int> finished_job_count = 0;
        worker_info(std::chrono::nanoseconds duration): start_duration{duration} {}
    };

    oneapi::tbb::concurrent_hash_map<df::worker_id, worker_info> worker_info_map_;
    using worker_info_map_accessor = decltype(worker_info_map_)::accessor;

    struct history
    {
        int worker_count;
        int number_of_incoming_request;
        basic::time_point when = basic::now();
    };

    std::atomic<int> worker_count_ = 0, number_of_incoming_request_ = 0;
    oneapi::tbb::concurrent_vector<history> history_;

public:
    reporter(std::string const& report_file): report_file_{report_file} {}

    void execute() override
    {
        std::ofstream output{report_file_};
        json report;
        report["total_duration"] = (basic::now() - start_time_).count();
        report["started_df"] = started_worker_.load();
        report["df"] = json::array();
        for (auto && [ptr, info] : worker_info_map_)
        {
            json dfstat;
            dfstat["start_duration"] = info.start_duration.count();

            using namespace std::chrono_literals;

            std::chrono::nanoseconds duration = info.end_time - info.start_time;
            if (duration == 0ns)
                duration = (basic::now() - info.start_time);

            dfstat["duration"] = duration.count();
            dfstat["finished_job_count"] = info.finished_job_count.load();
            report["df"].push_back(dfstat);
        }

        //report["history"] = json::array();
        history_.emplace_back(worker_count_, number_of_incoming_request_);
        for (history &h : history_)
        {
            json obj;
            obj["timestamp"] = (h.when - start_time_).count();
            obj["worker_count"] = h.worker_count;
            obj["number_of_incoming_request"] = h.number_of_incoming_request;
            //report["history"].push_back(obj);
        }

        number_of_incoming_request_ = 0;
        output << report.dump();
    }

    void finished_a_job(df::worker* ptr, job_ptr) override
    {
        worker_info_map_accessor it;
        if (worker_info_map_.find(it, ptr->worker_id_))
            it->second.finished_job_count.fetch_add(1, std::memory_order_relaxed);
    }

    void started_a_new_job(df::worker*, job_ptr) override {
        number_of_incoming_request_.fetch_add(1, std::memory_order_relaxed);
    }

    void starting_a_new_worker() override {
        pending_worker_timestamps_.push(basic::now());
    }

    void registered_a_new_worker(df::worker * ptr) override
    {
        basic::time_point started_time;
        pending_worker_timestamps_.try_pop(started_time);

        started_worker_.fetch_add(1, std::memory_order_relaxed);
        worker_count_.fetch_add(1, std::memory_order_relaxed);

        worker_info_map_.emplace(ptr->worker_id_, basic::now() - started_time);
    }

    void deregistered_a_worker(df::worker * ptr) override
    {
        worker_count_.fetch_sub(1, std::memory_order_relaxed);
        worker_info_map_accessor it;
        if (worker_info_map_.find(it, ptr->worker_id_))
            it->second.end_time = basic::now();
    }
};

} // namespace slsfs::launcher

#endif // LAUNCHER_BASE_TYPES_HPP__
