#pragma once
#ifndef POLICY_WORKER_FILETOWORKER_ACTIVE_LOWEST_LOAD_HPP__
#define POLICY_WORKER_FILETOWORKER_ACTIVE_LOWEST_LOAD_HPP__

#include "worker-filetoworker-lowest-load.hpp"

#include <limits>

namespace slsfs::launcher::policy
{

/* Assigns new files to the worker with the lowest amount of assigned files */
class active_lowest_load : public lowest_load
{
    using file_to_count_map =
        oneapi::tbb::concurrent_hash_map<
            pack::packet_header,
            std::atomic<int>,
            pack::packet_header_key_hash_compare>;

    struct counter
    {
        std::atomic<int> total_counter;
        file_to_count_map file_counter;
        counter (int init_counter): total_counter{init_counter} {}
    };

    using worker_file_map =
        oneapi::tbb::concurrent_hash_map<df::worker_id, counter>;

    pack::packet_header most_count_file_;

    worker_file_map worker_file_map_;
    file_to_count_map transfering_;

public:

    void execute()
    {
        int top = -1;
        for (auto && [id, info] : worker_file_map_)
        {
            if (info.total_counter.load() > top && info.file_counter.size() > 1)
            {
                top = info.total_counter.load();

                int worker_top = -1;
                for (auto && [header, count] : info.file_counter)
                {
                    if (count.load() > worker_top)
                    {
                        most_count_file_ = header;
                        worker_top = count.load();
                    }
                }
            }

            info.total_counter.store(0);
            for (auto && [header, count] : info.file_counter)
                count.store(0);
        }
    }

    void started_a_new_job(df::worker* worker_ptr, job_ptr job) override
    {
        worker_file_map::accessor it;
        if (worker_file_map_.find(it, worker_ptr->worker_id_))
        {
            counter& info = it->second;
            file_to_count_map& map = info.file_counter;
            file_to_count_map::accessor map_it;
            if (map.find(map_it, job->pack_->header))
                map_it->second.fetch_add(1);
            else
                map.emplace(job->pack_->header, 1);

            info.total_counter.fetch_add(1);
        }
        else
            worker_file_map_.emplace(worker_ptr->worker_id_, 1);
    }

    void finished_a_job(df::worker* worker_ptr, job_ptr job) override
    {
        worker_file_map::accessor it;
        if (worker_file_map_.find(it, worker_ptr->worker_id_))
        {
            counter& info = it->second;
            file_to_count_map& map = info.file_counter;
            file_to_count_map::accessor map_it;
            if (map.find(map_it, job->pack_->header))
            {
                map_it->second.fetch_sub(1);
                if (map_it->second.load() == 0)
                {
                    transfering_.erase(job->pack_->header);
                    fileid_to_worker_.erase(job->pack_->header);
                    //BOOST_LOG_TRIVIAL(info) << job->pack_->header << " have no files in worker " << worker_ptr;
                }
            }
        }
    }

    void start_transfer() override
    {
        BOOST_LOG_TRIVIAL(trace) << "start_transfer " << most_count_file_;
        transfering_.emplace(most_count_file_, 0);
        execute();
    }

    auto get_assigned_worker(pack::packet_pointer packet_ptr) -> df::worker_ptr override
    {
        file_to_count_map::accessor it;
        if (transfering_.find(it, packet_ptr->header))
            return nullptr;

        return worker_filetoworker::get_assigned_worker(packet_ptr);
    }
};

} // namespace slsfs::launcher::policy

#endif // POLICY_WORKER_FILETOWORKER_ACTIVE_LOWEST_LOAD_HPP__
