#pragma once
#ifndef LAUNCHER_HPP__
#define LAUNCHER_HPP__

#include "basic.hpp"
#include "serializer.hpp"
#include "launcher-job.hpp"
#include "launcher-policy.hpp"
#include "uuid.hpp"

#include <oneapi/tbb/concurrent_queue.h>
#include <oneapi/tbb/concurrent_hash_map.h>

#include <iterator>
#include <atomic>
#include <mutex>

namespace slsfs::launcher
{

template<typename T>
concept CanResolveZookeeper = requires(T t)
{
    { t.get_uuid(std::declval<std::string>()) }
        -> std::convertible_to<net::ip::tcp::endpoint>;
};

// Component responsible for launching jobs and workers
class launcher
{
    net::io_context& io_context_;
    net::io_context::strand launcher_strand_;

    worker_set worker_set_;

    uuid::uuid const& id_;
    std::string const announce_host_;
    net::ip::port_type const announce_port_;
    launcher_policy launcher_policy_;

    void start_execute_policy()
    {
        using namespace std::chrono_literals;
        auto timer = std::make_shared<boost::asio::steady_timer>(io_context_);
        timer->expires_from_now(1s);
        timer->async_wait(
            [this, timer] (boost::system::error_code error) {
                switch (error.value())
                {
                case boost::system::errc::success: // timer timeout
                {
                    start_execute_policy();
                    launcher_policy_.execute();
                    start_create_worker_with_policy();
                    break;
                }
                case boost::system::errc::operation_canceled: // timer canceled
                    BOOST_LOG_TRIVIAL(error) << "getting an operation_aborted error on launcher start_execute_policy()";
                    break;
                default:
                    BOOST_LOG_TRIVIAL(error) << "getting error: " << error.message() << " on launcher start_execute_policy()";
                }
            });
    }

    void start_create_worker_with_policy()
    {
        net::post(
            launcher_strand_,
            [this] {
                int const should_start = launcher_policy_.get_ideal_worker_count_delta();

                if (should_start != 0)
                    BOOST_LOG_TRIVIAL(trace) << "Starting worker of size: " << should_start;
                for (int i = 0; i < should_start; i++)
                {
                    create_worker (
                        launcher_policy_.get_worker_config().post_string,
                        launcher_policy_.get_worker_config().max_func_count);
                    launcher_policy_.start_transfer();
                }
            });
    }

    void create_worker (std::string const& body, int const max_func_count = 0)
    {
        launcher_policy_.starting_a_new_worker();

        trigger::make_trigger(io_context_, max_func_count)
            ->register_on_read(
                [](std::shared_ptr<slsfs::http::response<slsfs::http::string_body>> res) {
                    BOOST_LOG_TRIVIAL(info) << "openwhisk response: " << res->body();
                })
            .start_post(body);
    }

public:
    launcher(net::io_context& io, uuid::uuid const& id,
             std::string const& announce, net::ip::port_type port,
             std::string const& save_report):
        io_context_{io},
        launcher_strand_{io},
        id_{id},
        announce_host_{announce},
        announce_port_{port},
        launcher_policy_{io, worker_set_,
                         announce_host_, announce_port_,
                         save_report} {
        start_execute_policy();
    }

    template<typename PolicyType, typename ... Args>
    void set_policy_filetoworker (Args&& ... args) {
        launcher_policy_.filetoworker_policy_ = std::make_unique<PolicyType>(std::forward<Args>(args)...);
    }

    auto fileid_to_worker() -> fileid_map& {
        return launcher_policy_.filetoworker_policy_->fileid_to_worker_;
    }

    template<typename PolicyType, typename ... Args>
    void set_policy_launch (Args&& ... args) {
        launcher_policy_.launch_policy_       = std::make_unique<PolicyType>(std::forward<Args>(args)...);
    }

    template<typename PolicyType, typename ... Args>
    void set_policy_keepalive (Args&& ... args) {
        launcher_policy_.keepalive_policy_    = std::make_unique<PolicyType>(std::forward<Args>(args)...);
    }

    template<typename ... Args>
    void set_worker_config (Args && ... args) {
        launcher_policy_.worker_config_ = worker_config(std::forward<Args>(args)...);
    }

    void add_worker (tcp::socket socket, [[maybe_unused]] pack::packet_pointer worker_info)
    {
        auto worker_ptr = std::make_shared<df::worker>(io_context_, std::move(socket), *this);
        bool ok = worker_set_.emplace(worker_ptr, 0);

        BOOST_LOG_TRIVIAL(info) << "Get new worker [" << worker_ptr->id_ << "]. Active worker count: " << worker_set_.size();

        if (not ok)
            BOOST_LOG_TRIVIAL(error) << "Emplace worker not success";

        launcher_policy_.registered_a_new_worker(worker_ptr.get());
        worker_ptr->start_read_header();
    }

    void on_worker_reschedule (job_ptr job)
    {
        BOOST_LOG_TRIVIAL(trace) << "job " << job->pack_->header << " reschedule due to worker close";
        fileid_to_worker().erase(job->pack_->header);
        reschedule (job);
    }

    void on_worker_close (df::worker_ptr worker)
    {
        BOOST_LOG_TRIVIAL(trace) << "worker close: " << worker.get();
        worker_set_.erase(worker);
        launcher_policy_.deregistered_a_worker(worker.get());
    }

    void on_worker_finished_a_job (df::worker* worker, job_ptr job) {
        launcher_policy_.finished_a_job(worker, job);
    }

    void process_job (job_ptr job)
    {
        df::worker_ptr worker_ptr = launcher_policy_.get_assigned_worker(job->pack_);
        if (!worker_ptr || not worker_ptr->is_valid())
        {
            worker_ptr = launcher_policy_.get_available_worker(job->pack_);

            // no available worker. Re-scheduling request
            if (!worker_ptr || not worker_ptr->is_valid())
            {
                //BOOST_LOG_TRIVIAL(trace) << "No available worker. Re-scheduling request";
                //start_create_worker_with_policy();
                reschedule(job);
                return;
            }

            fileid_to_worker().emplace(job->pack_->header, worker_ptr);
        }

        BOOST_LOG_TRIVIAL(trace) << "Starting jobs, Start post.";

        // async launch stat calculator
        launcher_policy_.started_a_new_job(worker_ptr.get(), job);

        worker_ptr->start_write(job);

        using namespace std::chrono_literals;
        job->timer_.expires_from_now(2s);
        job->timer_.async_wait(
            [this, job] (boost::system::error_code ec) {
                if (ec && ec != net::error::operation_aborted)
                {
                    BOOST_LOG_TRIVIAL(error) << "error: " << ec << "repush job " << job->pack_->header;
                    reschedule(job);
                }
            });
        BOOST_LOG_TRIVIAL(trace) << "job started" << job->pack_->header;
    }

    template<typename Callback>
    void start_trigger_post (std::string const& body, pack::packet_pointer original_pack, Callback next)
    {
        pack::packet_pointer pack = std::make_shared<pack::packet>();
        pack->header      = original_pack->header;
        pack->header.type = pack::msg_t::worker_push_request;
        pack->data.buf    = std::vector<pack::unit_t>(body.size());
        std::memcpy(pack->data.buf.data(), body.data(), body.size());

        auto job_ptr = std::make_shared<job>(io_context_, pack, next);
        schedule(job_ptr);
    }

    void schedule (job_ptr job)
    {
        launcher_policy_.schedule_a_new_job(job);
        net::post(io_context_, [this, job] { process_job(job); });
    }

    void reschedule (job_ptr job)
    {
        launcher_policy_.reschedule_a_job(job);
        net::post(io_context_, [this, job] { process_job(job); });
    }

    // assumes begin -> end are sorted
    template<std::forward_iterator ForwardIterator, CanResolveZookeeper Zookeeper>
    void reconfigure (ForwardIterator begin, ForwardIterator end, Zookeeper&& zoo)
    {
        BOOST_LOG_TRIVIAL(trace) << "launcher reconfigure";
        for (auto&& pair : fileid_to_worker())
        {
            auto it = std::upper_bound (begin, end, pair.first.key);
            if (it == end)
                it = begin;

            if (*it != id_)
                start_send_reconfigure_message(pair, zoo.get_uuid(it->encode_base64()));
        }
    }

    void start_send_reconfigure_message (fileid_worker_pair & pair, net::ip::tcp::endpoint new_proxy)
    {
        BOOST_LOG_TRIVIAL(trace) << "start_send_reconfigure_message: " << new_proxy;

        pack::packet_pointer p = std::make_shared<pack::packet>();
        p->header = pair.first;
        p->header.type = pack::msg_t::proxyjoin;

        auto ip = new_proxy.address().to_v4().to_bytes();
        auto port = pack::hton(new_proxy.port());
        p->data.buf = std::vector<pack::unit_t>(sizeof(ip) + sizeof(port));
        std::copy(ip.begin(), ip.end(), p->data.buf.begin());
        std::memcpy(p->data.buf.data() + ip.size(), &port, sizeof(port));

        // may change to job ptr
        pair.second->start_write(p);
    }
};

} // namespace launcher

#endif // LAUNCHER_HPP__
