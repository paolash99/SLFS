#pragma once
#ifndef STORAGE_CONF_SSBD_BACKEND_HPP__
#define STORAGE_CONF_SSBD_BACKEND_HPP__

#include "storage-conf.hpp"

#include <slsfs.hpp>

#include <oneapi/tbb/concurrent_vector.h>
#include <boost/coroutine2/all.hpp>
#include <boost/asio.hpp>

#include <vector>
#include <semaphore>
#include <sstream>
#include <iomanip> // for std::put_time

namespace slsfsdf
{

    namespace detail
    {

        class recoder
        {
            using filecheckmap =
                oneapi::tbb::concurrent_hash_map<slsfs::pack::key_t,
                                                 int /* not used */,
                                                 slsfs::uuid::hash_compare<slsfs::pack::key_t>>;
            filecheckmap map_;

        public:
            bool is_checked(slsfs::pack::key_t const &uuid)
            {
                filecheckmap::accessor it;
                return map_.find(it, uuid);
            }

            void mark_checked(slsfs::pack::key_t const &uuid)
            {
                map_.emplace(uuid, 0);
            }

            bool erase_checked(slsfs::pack::key_t const &uuid)
            {
                return map_.erase(uuid);
            }
        };

    } // namespace

    // Storage backend configuration for SSBD stripe
    class storage_conf_ssbd_backend : public storage_conf
    {
        boost::asio::io_context &io_context_;
        int replication_size_ = 3, replication_start_index_ = 0;

        // first half is normal operation; second half is backup operation
        std::vector<std::shared_ptr<slsfs::backend::ssbd>> backend_list_;

        detail::recoder recoder_;

        void connect() override
        {
            for (std::shared_ptr<slsfs::backend::ssbd> host : backend_list_)
                host->connect();
        }

        void close() override
        {
            for (std::shared_ptr<slsfs::backend::ssbd> host : backend_list_)
                host->close();
        }

        static auto static_engine() -> std::mt19937 &
        {
            static thread_local std::mt19937 mt;
            return mt;
        }

        int select_replica(slsfs::pack::key_t const &uuid,
                           int const partition,
                           int const replication_index)
        {
            std::seed_seq seeds{uuid.begin(), uuid.end()};
            static_engine().seed(seeds);

            std::uniform_int_distribution<> dist(0, replication_start_index_ - 1);
            static_engine().discard(partition * (partition * replication_index));

            return dist(static_engine());
        }

        static auto version() -> std::uint32_t
        {
            std::uint64_t v = std::chrono::duration_cast<std::chrono::microseconds>(
                                  std::chrono::system_clock::now().time_since_epoch())
                                  .count();
            return static_cast<std::uint32_t>(v >> 6);
        }

        std::pair<std::string, std::string> split_path(const std::string &path)
        {
            // Remove trailing '/' if present
            std::string normalized_path = path;
            if (normalized_path == "/")
            {
                return {"", "/"};
            }
            if (!normalized_path.empty() && normalized_path.back() == '/')
            {
                normalized_path.pop_back();
            }

            // Find the last occurrence of '/'
            size_t last_separator = normalized_path.find_last_of("/");

            if (last_separator == std::string::npos)
            {
                return {"", path}; // No '/' found, return empty directory and full path as file name
            }
            if (last_separator == 0)
            {
                // The path is in the root directory
                return {"/", normalized_path.substr(1)};
            }

            // Normal case: path/file_name
            return {normalized_path.substr(0, last_separator + 1),
                    normalized_path.substr(last_separator + 1)};
        }

        // 2pc stuff
        void start_2pc_prepare(slsfs::jsre::request_parser<slsfs::base::byte> input,
                               slsfs::backend::ssbd::handler_ptr next)
        {
            auto request_dearline_timer = std::make_shared<boost::asio::steady_timer>(io_context_);
            using namespace std::chrono_literals;

            // every request must finish in 30s
            request_dearline_timer->expires_from_now(30s);
            request_dearline_timer->async_wait(
                [next, request_dearline_timer, input, this](boost::system::error_code ec)
                {
                    switch (ec.value())
                    {
                    case boost::system::errc::operation_canceled: // timer canceled
                        break;
                    case boost::system::errc::success:        // timer timeout
                        recoder_.erase_checked(input.uuid()); // ???
                        std::invoke(*next, slsfs::base::to_buf("Error: request timeout internally"));
                        [[fallthrough]];
                    default:
                        slsfs::log::log<slsfs::log::level::error>("timer_reset: write job '{}:{}' timeout.", input.print(), input.pack->header.print());
                        break;
                    }
                });

            std::uint32_t const realpos = input.position();
            std::uint32_t const endpos = realpos + input.size();
            std::uint32_t const selected_version = version();

            slsfs::log::log("start_2pc_prepare: {}", input.print());

            auto outstanding_requests = std::make_shared<std::atomic<int>>(0);
            auto all_ssbd_agree = std::make_shared<std::atomic<bool>>(true);
            for (std::uint32_t currentpos = realpos, buffer_pointer_offset = 0; currentpos < endpos;)
            {
                std::uint32_t const blockid = currentpos / blocksize();
                std::uint32_t const offset = currentpos % blocksize();
                std::uint32_t const blockwritesize = std::min<std::uint32_t>(endpos - currentpos,
                                                                             blocksize() - offset);
                slsfs::log::log("start_2pc_prepare sending: bid={}, @{}, size={}",
                                blockid, offset, blockwritesize);

                int const backend_index = select_replica(input.uuid(), blockid, 0);
                auto selected = backend_list_.at(backend_index);

                slsfs::leveldb_pack::packet_pointer request = slsfs::leveldb_pack::create_request(
                    input.uuid(),
                    recoder_.is_checked(input.uuid())?
                    slsfs::leveldb_pack::msg_t::two_pc_prepare_quick:
                    slsfs::leveldb_pack::msg_t::two_pc_prepare,
                    selected_version,
                    blockid,
                    offset,
                    blockwritesize);

                request->data.buf.resize(blockwritesize + headersize());

                std::memcpy(request->data.buf.data() + headersize(),
                            input.data() + buffer_pointer_offset,
                            blockwritesize);

                currentpos += blockwritesize;
                buffer_pointer_offset += blockwritesize;

                (*outstanding_requests)++;
                selected->start_send_request(
                    request,
                    [outstanding_requests, input, all_ssbd_agree, selected_version, request_dearline_timer, next, this](slsfs::leveldb_pack::packet_pointer response)
                    {
                        std::string message;
                        switch (response->header.type)
                        {
                        case slsfs::leveldb_pack::msg_t::two_pc_prepare_agree:
                            slsfs::log::log("2pc client agreed. Left {}", (*outstanding_requests - 1));
                            break;

                        case slsfs::leveldb_pack::msg_t::two_pc_prepare_abort:
                            slsfs::log::log("2pc abort: {}", response->header.print());
                            slsfs::log::log("2pc input: {}", input.print());
                            *all_ssbd_agree = false;
                            message = "abort";
                            break;

                        default:
                            // slsfs::log::log("unwanted header type {}", static_cast<int>(response->header.type));
                            *all_ssbd_agree = false;
                            message = "default";
                            break;
                        }

                        if (--(*outstanding_requests) == 0)
                        {
                            request_dearline_timer->cancel();

                            if (*all_ssbd_agree)
                            {
                                recoder_.mark_checked(input.uuid());
                                std::invoke(*next, slsfs::base::to_buf("OK"));
                            }
                            else
                            {
                                recoder_.erase_checked(input.uuid());
                                std::invoke(*next, slsfs::base::to_buf("Error: Found Pending 2PC Log" + message));
                            }

                            start_2pc_commit(input,
                                             *all_ssbd_agree,
                                             selected_version,
                                             nullptr);
                        }
                    });
            }
        }

        void start_2pc_commit(slsfs::jsre::request_parser<slsfs::base::byte> input,
                              bool const all_ssbd_agree,
                              std::uint32_t const selected_version,
                              slsfs::backend::ssbd::handler_ptr next)
        {
            std::uint32_t const realpos = input.position();
            std::uint32_t const endpos = realpos + input.size();

            auto outstanding_requests = std::make_shared<std::atomic<int>>(0);
            for (std::uint32_t currentpos = realpos, buffer_pointer_offset = 0; currentpos < endpos;)
            {
                std::uint32_t const blockid = currentpos / blocksize();
                std::uint32_t const offset = currentpos % blocksize();
                std::uint32_t const blockwritesize = std::min<std::uint32_t>(endpos - currentpos,
                                                                             blocksize() - offset);
                slsfs::log::log("start_2pc_commit: bid={}, @{}, size={}",
                                blockid, offset, blockwritesize);

                int const selected_index = select_replica(input.uuid(), blockid, 0);
                auto selected = backend_list_.at(selected_index);

                slsfs::leveldb_pack::packet_pointer request = slsfs::leveldb_pack::create_request(
                    input.uuid(),
                    all_ssbd_agree ? slsfs::leveldb_pack::msg_t::two_pc_commit_execute : slsfs::leveldb_pack::msg_t::two_pc_commit_rollback,
                    selected_version,
                    blockid,
                    offset,
                    /*blockwritesize*/ 0);

                currentpos += blockwritesize;
                buffer_pointer_offset += blockwritesize;

                (*outstanding_requests)++;
                selected->start_send_request(
                    request,
                    [outstanding_requests, input, all_ssbd_agree, selected_version, next, this](slsfs::leveldb_pack::packet_pointer response)
                    {
                        switch (response->header.type)
                        {
                        case slsfs::leveldb_pack::msg_t::two_pc_commit_ack:
                            break;

                        default:
                            slsfs::log::log("start_2pc_commit unwanted header type {}", response->header.print());

                            if (next)
                            {
                                slsfs::log::log("running request error reply");
                                recoder_.erase_checked(input.uuid());
                                std::invoke(*next, slsfs::base::to_buf("Error: Commit Message Get Error Reply"));
                            }
                        }

                        if (--(*outstanding_requests) == 0)
                        {
                            if (next)
                                std::invoke(*next, slsfs::base::to_buf("OK"));

                            if (all_ssbd_agree && replication_size_ > 1)
                                start_replication(input,
                                                  selected_version,
                                                  nullptr);
                        }
                    });
            }
        }

        void start_2pc_prepare_multi_inputs(std::vector<slsfs::jsre::request_parser<slsfs::base::byte>> inputs,
                                            slsfs::backend::ssbd::handler_ptr next)
        {

            auto request_deadline_timer = std::make_shared<boost::asio::steady_timer>(io_context_);
            using namespace std::chrono_literals;

            // every request must finish in 60s (increased from 30s due to two files)
            request_deadline_timer->expires_from_now(60s);
            request_deadline_timer->async_wait(
                [next, request_deadline_timer, inputs, this](boost::system::error_code ec)
                {
                    switch (ec.value())
                    {
                    case boost::system::errc::operation_canceled: // timer canceled
                        break;
                    case boost::system::errc::success: // timer timeout
                        for (const auto &input : inputs)
                        {
                            recoder_.erase_checked(input.uuid());
                        }
                        std::invoke(*next, slsfs::base::to_buf("Error: request timeout internally"));
                        [[fallthrough]];
                    default:
                        slsfs::log::log<slsfs::log::level::error>("timer_reset: write job for two files timed out.");
                        break;
                    }
                });

            std::uint32_t const selected_version = version();

            slsfs::log::log("start_2pc_prepare_multi_inputs");

            auto outstanding_requests = std::make_shared<std::atomic<int>>(0);
            auto all_ssbd_agree = std::make_shared<std::atomic<bool>>(true);

            auto prepare_input = [&](const auto &input)
            {
                std::uint32_t const realpos = input.position();
                std::uint32_t const endpos = realpos + input.size();

                for (std::uint32_t currentpos = realpos, buffer_pointer_offset = 0; currentpos < endpos;)
                {
                    std::uint32_t const blockid = currentpos / blocksize();
                    std::uint32_t const offset = currentpos % blocksize();
                    std::uint32_t const blockwritesize = std::min<std::uint32_t>(endpos - currentpos,
                                                                                 blocksize() - offset);
                    slsfs::log::log("start_2pc_prepare sending: bid={}, @{}, size={}",
                                    blockid, offset, blockwritesize);

                    int const backend_index = select_replica(input.uuid(), blockid, 0);
                    auto selected = backend_list_.at(backend_index);

                    slsfs::leveldb_pack::packet_pointer request = slsfs::leveldb_pack::create_request(
                        input.uuid(),
                        recoder_.is_checked(input.uuid())?
                        slsfs::leveldb_pack::msg_t::two_pc_prepare_quick:
                        slsfs::leveldb_pack::msg_t::two_pc_prepare,
                        selected_version,
                        blockid,
                        offset,
                        blockwritesize);

                    request->data.buf.resize(blockwritesize + headersize());

                    std::memcpy(request->data.buf.data() + headersize(),
                                input.data() + buffer_pointer_offset,
                                blockwritesize);

                    currentpos += blockwritesize;
                    buffer_pointer_offset += blockwritesize;

                    (*outstanding_requests)++;
                    selected->start_send_request(
                        request,
                        [outstanding_requests, inputs, all_ssbd_agree, selected_version, request_deadline_timer, next, this](slsfs::leveldb_pack::packet_pointer response)
                        {
                            switch (response->header.type)
                            {
                            case slsfs::leveldb_pack::msg_t::two_pc_prepare_agree:
                                slsfs::log::log("2pc client agreed. Left {}", (*outstanding_requests - 1));
                                break;

                            case slsfs::leveldb_pack::msg_t::two_pc_prepare_abort:
                                slsfs::log::log("2pc abort: {}", response->header.print());
                                *all_ssbd_agree = false;
                                break;

                            default:
                                *all_ssbd_agree = false;
                                break;
                            }

                            if (--(*outstanding_requests) == 0)
                            {
                                request_deadline_timer->cancel();

                                if (*all_ssbd_agree)
                                {
                                    for (const auto &input : inputs)
                                    {
                                        recoder_.mark_checked(input.uuid());
                                    }
                                    std::invoke(*next, slsfs::base::to_buf("OK"));
                                }
                                else
                                {
                                    for (const auto &input : inputs)
                                    {
                                        recoder_.erase_checked(input.uuid());
                                    }
                                    std::invoke(*next, slsfs::base::to_buf("Error: Found Pending 2PC Log"));
                                }

                                start_2pc_commit_multi_inputs(inputs,
                                                              *all_ssbd_agree,
                                                              selected_version,
                                                              nullptr);
                            }
                        });
                }
            };

            for (const auto &input : inputs)
            {
                prepare_input(input);
            }
        }

        void start_2pc_commit_multi_inputs(std::vector<slsfs::jsre::request_parser<slsfs::base::byte>> inputs,
                                           bool const all_ssbd_agree,
                                           std::uint32_t const selected_version,
                                           slsfs::backend::ssbd::handler_ptr next)
        {
            auto outstanding_requests = std::make_shared<std::atomic<int>>(0);

            auto commit_input = [&](const auto &input)
            {
                std::uint32_t const realpos = input.position();
                std::uint32_t const endpos = realpos + input.size();

                for (std::uint32_t currentpos = realpos, buffer_pointer_offset = 0; currentpos < endpos;)
                {
                    std::uint32_t const blockid = currentpos / blocksize();
                    std::uint32_t const offset = currentpos % blocksize();
                    std::uint32_t const blockwritesize = std::min<std::uint32_t>(endpos - currentpos,
                                                                                 blocksize() - offset);
                    slsfs::log::log("start_2pc_commit_multi_inputs: bid={}, @{}, size={}",
                                    blockid, offset, blockwritesize);

                    int const selected_index = select_replica(input.uuid(), blockid, 0);
                    auto selected = backend_list_.at(selected_index);

                    slsfs::leveldb_pack::packet_pointer request = slsfs::leveldb_pack::create_request(
                        input.uuid(),
                        all_ssbd_agree ? slsfs::leveldb_pack::msg_t::two_pc_commit_execute : slsfs::leveldb_pack::msg_t::two_pc_commit_rollback,
                        selected_version,
                        blockid,
                        offset,
                        /*blockwritesize*/ 0);

                    currentpos += blockwritesize;
                    buffer_pointer_offset += blockwritesize;

                    (*outstanding_requests)++;
                    selected->start_send_request(
                        request,
                        [outstanding_requests, inputs, all_ssbd_agree, selected_version, next, this](slsfs::leveldb_pack::packet_pointer response)
                        {
                            switch (response->header.type)
                            {
                            case slsfs::leveldb_pack::msg_t::two_pc_commit_ack:
                                break;

                            default:
                                slsfs::log::log("start_2pc_commit unwanted header type {}", response->header.print());

                                if (next)
                                {
                                    slsfs::log::log("running request error reply");
                                    for (const auto &input : inputs)
                                    {
                                        recoder_.erase_checked(input.uuid());
                                    }
                                    std::invoke(*next, slsfs::base::to_buf("Error: Commit Message Get Error Reply"));
                                }
                            }

                            if (--(*outstanding_requests) == 0)
                            {
                                if (next)
                                    std::invoke(*next, slsfs::base::to_buf("OK"));

                                if (all_ssbd_agree && replication_size_ > 1)
                                {
                                    for (const auto &input : inputs)
                                    {
                                        start_replication(input, selected_version, nullptr);
                                    }
                                }
                            }
                        });
                }
            };

            for (const auto &input : inputs)
            {
                commit_input(input);
            }
        }

        void start_replication(slsfs::jsre::request_parser<slsfs::base::byte> input,
                               std::uint32_t const selected_version,
                               slsfs::backend::ssbd::handler_ptr next)
        {
            slsfs::log::log("start_replication with {}", input.print());
            std::uint32_t const realpos = input.position();
            std::uint32_t const endpos = realpos + input.size();

            auto outstanding_requests = std::make_shared<std::atomic<int>>(0);
            for (std::uint32_t currentpos = realpos, buffer_pointer_offset = 0; currentpos < endpos;)
            {
                std::uint32_t const blockid = currentpos / blocksize();
                std::uint32_t const offset = currentpos % blocksize();
                std::uint32_t const blockwritesize = std::min<std::uint32_t>(endpos - currentpos,
                                                                             blocksize() - offset);
                slsfs::log::log("start_replication: bid={}, @{}, size={}",
                                blockid, offset, blockwritesize);

                currentpos += blockwritesize;
                buffer_pointer_offset += blockwritesize;
                // we already have one copy at replica_index 0 from the 2pc part, so start at replica 1
                for (int replica_index = 1; replica_index < replication_size_; replica_index++)
                {
                    int const selected_index =
                        replication_start_index_ + select_replica(input.uuid(), blockid, replica_index);
                    auto selected = backend_list_.at(selected_index);

                    slsfs::leveldb_pack::packet_pointer request = slsfs::leveldb_pack::create_request(
                        input.uuid(),
                        slsfs::leveldb_pack::msg_t::replication,
                        selected_version,
                        blockid,
                        offset,
                        blockwritesize);

                    (*outstanding_requests)++;
                    selected->start_send_request(
                        request,
                        [outstanding_requests, replica_index, input, next, this](slsfs::leveldb_pack::packet_pointer response)
                        {
                            switch (response->header.type)
                            {
                            case slsfs::leveldb_pack::msg_t::ack:
                                break;

                            default:
                                slsfs::log::log("start_replication unwanted header type {}",
                                                response->header.print());
                                if (next)
                                {
                                    recoder_.erase_checked(input.uuid());
                                    std::invoke(*next, slsfs::base::to_buf("Error: Replication Failed"));
                                }
                                return;
                            }

                            if (--(*outstanding_requests) == 0 and next)
                            {
                                slsfs::log::log("replication finished {}", response->header.print());
                                std::invoke(*next, slsfs::base::to_buf("OK"));
                            }
                        });
                }
            }
        }

        struct buf_stat_t
        {
            std::atomic<bool> ready = false;
            slsfs::base::buf buf;
        };

        // Helper function to format time
        std::string format_time(std::time_t time_val)
        {
            std::ostringstream oss;
            oss << std::put_time(std::localtime(&time_val), "%Y-%m-%d %H:%M:%S");
            return oss.str();
        }

        void start_write(slsfs::jsre::request_parser<slsfs::base::byte> const &input,
                         slsfs::backend::ssbd::handler_ptr next, std::uint32_t readsize = 4096)
        {
            slsfs::log::log("start_write {}", input.print());
            slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
            ptr->header.gen();
            ptr->header.key = input.uuid();

            slsfs::jsre::request read_request{
                .type = slsfs::jsre::type_t::file,
                .operation = slsfs::jsre::operation_t::read,
                .position = 0,
                .size = readsize};

            read_request.to_network_format();
            ptr->data.buf.resize(sizeof(read_request));
            std::memcpy(ptr->data.buf.data(), &read_request, sizeof(read_request));

            auto readnext = std::make_shared<slsfs::backend::ssbd::handler>(
                [input, next, this](slsfs::base::buf file_content)
                {
                    slsfs::log::log("start_write get read resp: size={}", file_content.size());
                    slsfs::jsre::meta::stats stat;
                    if (file_content.size() < sizeof(stat))
                    {
                        recoder_.erase_checked(input.uuid());
                        std::invoke(*next, slsfs::base::to_buf("Error: No such directory"));
                        return;
                    }

                    // get number of files
                    std::memcpy(std::addressof(stat), file_content.data(), sizeof(stat));
                    stat.to_host_format();

                    // not enough content. reread all file
                    if (sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server) > file_content.size())
                    {
                        start_write(input, next, sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server));
                        return;
                    }

                    // Set modification time to current time
                    auto now = std::chrono::system_clock::now();
                    auto now_time_t = std::chrono::system_clock::to_time_t(now);

                    // Compute the possible size of the updated file
                    std::uint32_t const real_pos = input.position();
                    std::uint32_t const end_pos = real_pos + input.size();
                    std::uint32_t block_id = 0;
                    std::uint32_t block_write_size = 0;
                    for (std::uint32_t current_pos = real_pos; current_pos < end_pos;)
                    {
                        block_id = current_pos / blocksize();
                        std::uint32_t const offset = current_pos % blocksize();
                        block_write_size = std::min<std::uint32_t>(end_pos - current_pos,
                                                                   blocksize() - offset);
                        current_pos += block_write_size;
                    }
                    std::uint32_t const new_file_size = block_id * blocksize() + block_write_size;

                    std::string file_name = input.file_name();

                    slsfs::jsre::meta::filemeta_server current_filemeta;
                    for (std::uint32_t i = 1; i <= stat.file_count; ++i)
                    {
                        unsigned int const pos = i * sizeof(current_filemeta);
                        std::memcpy(std::addressof(current_filemeta),
                                    file_content.data() + pos,
                                    sizeof(current_filemeta));

                        auto end = std::find(current_filemeta.file_name.begin(), current_filemeta.file_name.end(), '\0');
                        std::string current_file_name(current_filemeta.file_name.begin(), end);
                        if (current_file_name == file_name)
                        {
                            current_filemeta.to_host_format();
                            // File found, update its metadata
                            current_filemeta.file_size = new_file_size > current_filemeta.file_size ? new_file_size : current_filemeta.file_size;
                            current_filemeta.modification_time = static_cast<uint64_t>(now_time_t);

                            current_filemeta.to_network_format();
                            std::memcpy(file_content.data() + pos,
                                        &current_filemeta,
                                        sizeof(slsfs::jsre::meta::filemeta_server));
                            break;
                        }
                    }

                    file_content.resize(sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server));

                    slsfs::jsre::request write_request{
                        .type = slsfs::jsre::type_t::file,
                        .operation = slsfs::jsre::operation_t::write,
                        .position = 0,
                        .size = static_cast<std::uint32_t>(file_content.size())};

                    write_request.to_network_format();

                    slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
                    ptr->header.gen();
                    ptr->header.key = input.uuid();
                    ptr->data.buf.resize(sizeof(write_request) + file_content.size());
                    std::memcpy(ptr->data.buf.data(), &write_request, sizeof(write_request));
                    std::memcpy(ptr->data.buf.data() + sizeof(write_request), file_content.data(), file_content.size());

                    slsfs::jsre::request_parser<slsfs::base::byte> write_request_parser{ptr};

                    // //second write request
                    slsfs::jsre::request write_request2{
                        .type = slsfs::jsre::type_t::file,
                        .operation = slsfs::jsre::operation_t::write,
                        .position = 0,
                        .size = static_cast<std::uint32_t>(input.size())};

                    write_request2.to_network_format();

                    slsfs::pack::packet_pointer ptr2 = std::make_shared<slsfs::pack::packet>();
                    ptr2->header.gen();
                    ptr2->header.key = input.file_key();
                    ptr2->data.buf.resize(sizeof(write_request2) + input.size());
                    std::memcpy(ptr2->data.buf.data(), &write_request2, sizeof(write_request2));
                    std::memcpy(ptr2->data.buf.data() + sizeof(write_request2), input.data(), input.size());

                    slsfs::jsre::request_parser<slsfs::base::byte> write_request_parser2{ptr2};
                    std::vector<slsfs::jsre::request_parser<slsfs::base::byte>> inputs = {write_request_parser, write_request_parser2};
                    start_2pc_prepare_multi_inputs(inputs, next);
                    return;
                });

            slsfs::jsre::request_parser<slsfs::base::byte> read_request_input{ptr};
            start_read(read_request_input, readnext);
        }

        void start_read(slsfs::jsre::request_parser<slsfs::base::byte> const input,
                        slsfs::backend::ssbd::handler_ptr next)
        {
            slsfs::log::log("ssbd start_read");
            auto timer = std::make_shared<boost::asio::steady_timer>(io_context_);
            using namespace std::chrono_literals;
            timer->expires_from_now(30s);
            timer->async_wait(
                [next, timer, input, this](boost::system::error_code ec)
                {
                    switch (ec.value())
                    {
                    case boost::system::errc::operation_canceled: // timer canceled
                        break;
                    case boost::system::errc::success: // timer timeout
                        recoder_.erase_checked(input.uuid());
                        std::invoke(*next, slsfs::base::to_buf("Error: Read Request Timeout"));
                        [[fallthrough]];

                    default:
                        slsfs::log::log<slsfs::log::level::error>("timer_reset: read job '{}:{}' timeout.", input.print(), input.pack->header.print());
                        break;
                    }
                });

            std::uint32_t const realpos = input.position();
            std::uint32_t const readsize = input.size();
            std::uint32_t const endpos = realpos + readsize;

            if (readsize == 0)
            {
                std::invoke(*next, slsfs::base::buf{});
                timer->cancel();
                return;
            }

            int result_vector_size = 0;
            for (std::uint32_t currentpos = realpos; currentpos < endpos; result_vector_size++)
            {
                std::uint32_t const offset = currentpos % blocksize();
                std::uint32_t const blockreadsize = std::min<std::uint32_t>(endpos - currentpos,
                                                                            blocksize() - offset);
                currentpos += blockreadsize;
            }

            auto result_accumulator = std::make_shared<oneapi::tbb::concurrent_vector<buf_stat_t>>(result_vector_size);

            for (std::uint32_t currentpos = realpos, index = 0; currentpos < endpos; index++)
            {
                std::uint32_t const blockid = currentpos / blocksize();
                std::uint32_t const offset = currentpos % blocksize();
                std::uint32_t const blockreadsize = std::min<std::uint32_t>(endpos - currentpos,
                                                                            blocksize() - offset);
                slsfs::log::log("start_read: {}, {}, {}",
                                blockid, offset, blockreadsize);

                slsfs::leveldb_pack::packet_pointer request = slsfs::leveldb_pack::create_request(
                    input.uuid(),
                    slsfs::leveldb_pack::msg_t::get,
                    0 /* version number. not use for read request */,
                    blockid,
                    offset,
                    blockreadsize);

                int const selected_index = select_replica(input.uuid(), blockid, 0);
                auto selected = backend_list_.at(selected_index);
                selected->start_send_request(
                    request,
                    [result_accumulator, input, next, index, timer, this](slsfs::leveldb_pack::packet_pointer resp)
                    {
                        result_accumulator->at(index).ready = true;
                        result_accumulator->at(index).buf = std::move(resp->data.buf);

                        for (buf_stat_t &bufstat : *result_accumulator)
                            if (not bufstat.ready)
                                return;

                        timer->cancel();
                        slsfs::base::buf collect;
                        collect.reserve((result_accumulator->size() + 2 /* head and tail */) * blocksize());
                        for (buf_stat_t &bufstat : *result_accumulator)
                            collect.insert(collect.end(),
                                           bufstat.buf.begin(),
                                           bufstat.buf.end());

                        std::invoke(*next, std::move(collect));
                    });
                currentpos += blockreadsize;
            }
        }

        void start_meta_get_stat(slsfs::jsre::request_parser<slsfs::base::byte> const &input,
                                 slsfs::backend::ssbd::handler_ptr next,
                                 bool is_directory = false, std::uint32_t readsize = 4096)
        {
            slsfs::log::log("start_meta_get_stat {}", input.print());

            slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
            ptr->header.gen();
            ptr->header.key = input.uuid();

            slsfs::jsre::request read_request{
                .type = slsfs::jsre::type_t::file,
                .operation = slsfs::jsre::operation_t::read,
                .position = 0,
                .size = readsize};

            read_request.to_network_format();
            ptr->data.buf.resize(sizeof(read_request));
            std::memcpy(ptr->data.buf.data(), &read_request, sizeof(read_request));

            auto readnext = std::make_shared<slsfs::backend::ssbd::handler>(
                [input, next, is_directory, this](slsfs::base::buf file_content)
                {
                    slsfs::log::log("start_meta_get_stat get read resp: size={}", file_content.size());
                    slsfs::jsre::meta::stats stat;
                    if (file_content.size() < sizeof(stat))
                    {
                        recoder_.erase_checked(input.uuid());
                        std::invoke(*next, slsfs::base::to_buf("Error: No such directory"));
                        return;
                    }

                    // get number of files
                    std::memcpy(std::addressof(stat), file_content.data(), sizeof(stat));
                    stat.to_host_format();

                    // not enough content. reread all file
                    if (sizeof(stat) +  stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server) > file_content.size())
                    {
                        start_meta_get_stat(input, next, is_directory, sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server));
                        return;
                    }

                    // Find the file
                    slsfs::base::buf output;
                    std::string file_name = input.file_name();
                    std::string total;

                    if (is_directory)
                    {
                        total = fmt::format("Directory name: {}\n", file_name);
                    }
                    else
                    {
                        total = fmt::format("File name: {}\n", file_name);
                    }

                    output.insert(output.end(), total.begin(), total.end());

                    bool file_found = false;
                    slsfs::jsre::meta::filemeta_server current_meta;
                    for (std::uint32_t i = 1; i <= stat.file_count; ++i)
                    {
                        unsigned int const pos = i * sizeof(current_meta);
                        std::memcpy(std::addressof(current_meta),
                                    file_content.data() + pos,
                                    sizeof(current_meta));

                        auto end = std::find(current_meta.file_name.begin(), current_meta.file_name.end(), '\0');
                        std::string current_file_name(current_meta.file_name.begin(), end);
                        if (current_file_name == file_name)
                        {
                            // File found
                            current_meta.to_host_format();
                            std::string info;
                            // Add other filemeta information
                            if (is_directory)
                            {
                                info = fmt::format("Owner: {}\t  Permission: {:o}\t Created: {}\t  Modified: {}\t\n",
                                                   current_meta.owner,
                                                   current_meta.permission,
                                                   format_time(current_meta.creation_time),
                                                   format_time(current_meta.modification_time));
                            }
                            else
                            {
                                info = fmt::format("Owner: {}\t  Permission: {:o}\t  Size: {} bytes\t  Created: {}\t  Modified: {}\t\n",
                                                   current_meta.owner,
                                                   current_meta.permission,
                                                   current_meta.file_size,
                                                   format_time(current_meta.creation_time),
                                                   format_time(current_meta.modification_time));
                            }
                            output.insert(output.end(), info.begin(), info.end());
                            file_found = true;
                            break;
                        }
                    }

                    if (!file_found)
                    {
                        std::invoke(*next, slsfs::base::to_buf("Error: File not found"));
                        return;
                    }
                    std::invoke(*next, output);
                });

            slsfs::jsre::request_parser<slsfs::base::byte> read_request_input{ptr};
            start_read(read_request_input, readnext);
        }

        void start_meta_delete_file(slsfs::jsre::request_parser<slsfs::base::byte> const &input,
                                    slsfs::backend::ssbd::handler_ptr next,
                                    std::uint32_t readsize = 4096)
        {
            slsfs::log::log("start_meta_delete_file {}", input.print());
            slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
            ptr->header.gen();
            ptr->header.key = input.uuid();

            slsfs::jsre::request read_request{
                .type = slsfs::jsre::type_t::file,
                .operation = slsfs::jsre::operation_t::read,
                .position = 0,
                .size = readsize};

            read_request.to_network_format();
            ptr->data.buf.resize(sizeof(read_request));
            std::memcpy(ptr->data.buf.data(), &read_request, sizeof(read_request));

            auto readnext = std::make_shared<slsfs::backend::ssbd::handler>(
                [input, next, this](slsfs::base::buf file_content)
                {
                    slsfs::log::log("start_meta_delete_file get read resp: size={}", file_content.size());
                    slsfs::jsre::meta::stats stat;
                    if (file_content.size() < sizeof(stat))
                    {
                        recoder_.erase_checked(input.uuid());
                        std::invoke(*next, slsfs::base::to_buf("Error: No such directory"));
                        return;
                    }

                    // get number of files
                    std::memcpy(std::addressof(stat), file_content.data(), sizeof(stat));
                    stat.to_host_format();

                    // not enough content. reread all file
                    if (sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server) > file_content.size())
                    {
                        start_meta_delete_file(input, next, sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server));
                        return;
                    }

                    std::string file_name = input.file_name();

                    // Find and remove the file
                    bool file_found = false;
                    slsfs::jsre::meta::filemeta_server current_filemeta;
                    for (std::uint32_t i = 1; i <= stat.file_count; ++i)
                    {
                        unsigned int const pos = i * sizeof(current_filemeta);
                        std::memcpy(std::addressof(current_filemeta),
                                    file_content.data() + pos,
                                    sizeof(current_filemeta));

                        auto end = std::find(current_filemeta.file_name.begin(), current_filemeta.file_name.end(), '\0');
                        std::string current_file_name(current_filemeta.file_name.begin(), end);
                        if (current_file_name == file_name)
                        {
                            // File found, swap with the last entry if it's not already the last
                            if (i < stat.file_count)
                            {
                                std::memcpy(file_content.data() + pos,
                                            file_content.data() + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server),
                                            sizeof(slsfs::jsre::meta::filemeta_server));
                            }
                            file_found = true;
                            break;
                        }
                    }

                    if (!file_found)
                    {
                        std::invoke(*next, slsfs::base::to_buf("Error: File not found"));
                        return;
                    }

                    // Update file count
                    stat.file_count--;

                    slsfs::log::log("start_meta_delete_file removed file, now have {} files", stat.file_count);

                    // Update file content size
                    file_content.resize(sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server));

                    stat.to_network_format();

                    // update at position 0
                    std::memcpy(file_content.data(), std::addressof(stat), sizeof(stat));

                    slsfs::jsre::request write_request{
                        .type = slsfs::jsre::type_t::file,
                        .operation = slsfs::jsre::operation_t::write,
                        .position = 0,
                        .size = static_cast<std::uint32_t>(file_content.size())};

                    write_request.to_network_format();
                    slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
                    ptr->header.gen();
                    ptr->header.key = input.uuid();

                    ptr->data.buf.resize(sizeof(write_request) + file_content.size());
                    std::memcpy(ptr->data.buf.data(), &write_request, sizeof(write_request));
                    std::memcpy(ptr->data.buf.data() + sizeof(write_request), file_content.data(), file_content.size());

                    slsfs::jsre::request_parser<slsfs::base::byte> write_request_parser{ptr};

                    // send the write request, and at finish, write back to client
                    start_2pc_prepare(write_request_parser, next);
                });

            slsfs::jsre::request_parser<slsfs::base::byte> read_request_input{ptr};
            start_read(read_request_input, readnext);
        }

        void start_meta_add_file(slsfs::jsre::request_parser<slsfs::base::byte> const &input,
                                 slsfs::backend::ssbd::handler_ptr next,
                                 std::uint32_t readsize = 4096)
        {
            slsfs::log::log("start_meta_add_file {}", input.print());
            slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
            ptr->header.gen();
            ptr->header.key = input.uuid();

            slsfs::jsre::request read_request{
                .type = slsfs::jsre::type_t::file,
                .operation = slsfs::jsre::operation_t::read,
                .position = 0,
                .size = readsize};

            read_request.to_network_format();
            ptr->data.buf.resize(sizeof(read_request));
            std::memcpy(ptr->data.buf.data(), &read_request, sizeof(read_request));

            /*
               Read first block: in the first block for a directory,
               end of file list (== number of files) (network format). i.e.
               struct slsfsdf::ssbd::meta::stats {
                   std::uint32_t files;
                   std::uint32_t last;
               }

               0: 4K block
               [[{meta::stats}: 64 bytes], [{owner, permission, file_size, creation_time, modification_time, file_name}: 64 bytes], ...]
            */

            auto readnext = std::make_shared<slsfs::backend::ssbd::handler>(
                [input, next, this](slsfs::base::buf file_content)
                {
                    slsfs::log::log("start_meta_add_file get read resp: size={}", file_content.size());
                    slsfs::jsre::meta::stats stat;
                    if (file_content.size() < sizeof(stat))
                    {
                        recoder_.erase_checked(input.uuid());
                        std::invoke(*next, slsfs::base::to_buf("Error: No such directory"));
                        return;
                    }

                    // // // get number of files
                    std::memcpy(std::addressof(stat), file_content.data(), sizeof(stat));
                    stat.to_host_format();

                    // not enough content. reread all file
                    if ((sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server)) > file_content.size())
                    {
                        start_meta_add_file(input, next, sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server));
                        return;
                    }

                    // // // // get metadata from request (network format)
                    slsfs::jsre::meta::filemeta filemeta;
                    std::memcpy(std::addressof(filemeta), input.data(), sizeof(filemeta));

                    filemeta.to_host_format();

                    //check if the file already exists in the parent directory file
                    std::string file_name = input.file_name();

                    slsfs::jsre::meta::filemeta_server current_filemeta;
                    for (std::uint32_t i = 1; i <= stat.file_count; ++i)
                    {
                        unsigned int const pos = i * sizeof(current_filemeta);
                        std::memcpy(std::addressof(current_filemeta),
                                    file_content.data() + pos,
                                    sizeof(current_filemeta));

                        auto end = std::find(current_filemeta.file_name.begin(), current_filemeta.file_name.end(), '\0');
                        std::string current_file_name(current_filemeta.file_name.begin(), end);
                        if (current_file_name == file_name)
                        {
                            std::invoke(*next, slsfs::base::to_buf("Error: File already exists"));
                            return;
                        }
                    }

                    auto now = std::chrono::system_clock::now();
                    auto now_time_t = std::chrono::system_clock::to_time_t(now);

                    // prepare the file meta data
                    slsfs::jsre::meta::filemeta_server final_file_meta = {
                        .owner = filemeta.owner,
                        .permission = filemeta.permission,
                        .file_size = 0,
                        .creation_time = static_cast<uint64_t>(now_time_t),
                        .modification_time = static_cast<uint64_t>(now_time_t),
                        .file_name = {}};

                    std::memcpy(final_file_meta.file_name.data(), file_name.data(), std::min(file_name.size(), sizeof(final_file_meta)));
                    final_file_meta.to_network_format();

                    // // calculate the append address of the stat in the parent directory file
                    std::uint32_t const position = (++stat.file_count) * sizeof(final_file_meta);

                    // // append stat in the parent directory
                    file_content.resize(position + sizeof(final_file_meta));
                    std::memcpy(file_content.data() + position,
                                std::addressof(final_file_meta), sizeof(final_file_meta));

                    stat.to_network_format();

                    // update file content at position 0 (stat position)
                    std::memcpy(file_content.data(), std::addressof(stat), sizeof(stat));

                    // //create a write request to update the parent directory

                    slsfs::jsre::request write_request{
                        .type = slsfs::jsre::type_t::file,
                        .operation = slsfs::jsre::operation_t::write,
                        .position = 0,
                        .size = static_cast<std::uint32_t>(file_content.size())};

                    write_request.to_network_format();

                    slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
                    ptr->header.gen();
                    ptr->header.key = input.uuid();
                    ptr->data.buf.resize(sizeof(write_request) + file_content.size());
                    std::memcpy(ptr->data.buf.data(), &write_request, sizeof(write_request));
                    std::memcpy(ptr->data.buf.data() + sizeof(write_request), file_content.data(), file_content.size());

                    slsfs::jsre::request_parser<slsfs::base::byte> write_request_parser{ptr};

                    // send the write request, and at finish, write back to client
                    start_2pc_prepare(write_request_parser, next);
                });

            slsfs::jsre::request_parser<slsfs::base::byte> read_request_input{ptr};
            start_read(read_request_input, readnext);
        }

        void start_meta_init(slsfs::jsre::request_parser<slsfs::base::byte> const &input,
                             slsfs::backend::ssbd::handler_ptr next,
                             std::uint32_t readsize = 4096)
        {
            slsfs::log::log("start_meta_init {}", input.print());
            // creating write request for directory file
            slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
            ptr->header.gen();
            ptr->header.key = input.uuid();

            // create a stat and put in the dir file
            slsfs::jsre::meta::stats stat;
            stat.to_network_format();

            slsfs::jsre::request write_request{
                .type = slsfs::jsre::type_t::file,
                .operation = slsfs::jsre::operation_t::write,
                .position = 0,
                .size = sizeof(stat)};

            write_request.to_network_format();

            ptr->data.buf.resize(sizeof(write_request) + sizeof(stat));
            std::memcpy(ptr->data.buf.data(), &write_request, sizeof(write_request));
            std::memcpy(ptr->data.buf.data() + sizeof(write_request), &stat, sizeof(stat));
            slsfs::jsre::request_parser<slsfs::base::byte> write_request_input{ptr};

            start_2pc_prepare(write_request_input, next);
        }

        void start_meta_mkdir(slsfs::jsre::request_parser<slsfs::base::byte> const &input,
                              slsfs::backend::ssbd::handler_ptr next,
                              std::uint32_t readsize = 4096)
        {
            slsfs::log::log("start_meta_mkdir {}", input.print());
            slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
            ptr->header.gen();
            ptr->header.key = input.uuid();

            slsfs::jsre::request read_request{
                .type = slsfs::jsre::type_t::file,
                .operation = slsfs::jsre::operation_t::read,
                .position = 0,
                .size = readsize};

            read_request.to_network_format();
            ptr->data.buf.resize(sizeof(read_request));
            std::memcpy(ptr->data.buf.data(), &read_request, sizeof(read_request));

            auto readnext = std::make_shared<slsfs::backend::ssbd::handler>(
                [input, next, this](slsfs::base::buf file_content)
                {
                    slsfs::log::log("start_meta_mkdir get read resp: size={}", file_content.size());
                    slsfs::jsre::meta::stats stat;
                    if (file_content.size() < sizeof(stat))
                    {
                        recoder_.erase_checked(input.uuid());
                        std::invoke(*next, slsfs::base::to_buf("Error: No such directory"));
                        return;
                    }

                    // get number of files
                    std::memcpy(std::addressof(stat), file_content.data(), sizeof(stat));
                    stat.to_host_format();

                    // not enough content. reread all file
                    if (sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server) > file_content.size())
                    {
                        start_meta_mkdir(input, next, sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server));
                        return;
                    }

                    std::string dir_name = input.file_name();
                    slsfs::jsre::meta::filemeta_server current_filemeta;

                    // check if the directory already exists in the parent directory file
                    for (std::uint32_t i = 1; i <= stat.file_count; ++i)
                    {
                        unsigned int const pos = i * sizeof(current_filemeta);
                        std::memcpy(std::addressof(current_filemeta),
                                    file_content.data() + pos,
                                    sizeof(current_filemeta));

                        auto end = std::find(current_filemeta.file_name.begin(), current_filemeta.file_name.end(), '\0');
                        std::string current_file_name(current_filemeta.file_name.begin(), end);
                        if (current_file_name == dir_name)
                        {
                            std::invoke(*next, slsfs::base::to_buf("Error: Directory already exists"));
                            return;
                        }
                    }

                    // get metadata from request (network format)
                    slsfs::jsre::meta::dirmeta dir_meta;
                    std::memcpy(std::addressof(dir_meta), input.data(), sizeof(dir_meta));
                    dir_meta.to_host_format();

                    // set creation time and modification time to current time
                    auto now = std::chrono::system_clock::now();
                    auto now_time_t = std::chrono::system_clock::to_time_t(now);

                    slsfs::jsre::meta::filemeta_server file_meta = {
                        .owner = dir_meta.owner,
                        .permission = dir_meta.permission,
                        .file_size = 0,
                        .creation_time = static_cast<uint64_t>(now_time_t),
                        .modification_time = static_cast<uint64_t>(now_time_t),
                        .file_name = {}};

                    std::memcpy(file_meta.file_name.data(), dir_name.data(), std::min(dir_name.size(), sizeof(file_meta)));
                    file_meta.to_network_format();

                    // calculate the append address of stat in the parent directory
                    std::uint32_t const position = (++stat.file_count) * sizeof(file_meta);

                    // append stat in the parent directory
                    file_content.resize(position + sizeof(file_meta));
                    std::memcpy(file_content.data() + position,
                                std::addressof(file_meta), sizeof(file_meta));

                    stat.to_network_format();

                    // update stat at position 0
                    std::memcpy(file_content.data(), std::addressof(stat), sizeof(stat));

                    // creating write request for parent directory file
                    slsfs::jsre::request write_request_parent{
                        .type = slsfs::jsre::type_t::file,
                        .operation = slsfs::jsre::operation_t::write,
                        .position = 0,
                        .size = static_cast<std::uint32_t>(file_content.size())};

                    write_request_parent.to_network_format();

                    slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
                    ptr->header.gen();
                    ptr->header.key = input.uuid();
                    ptr->data.buf.resize(sizeof(write_request_parent) + file_content.size());
                    std::memcpy(ptr->data.buf.data(), &write_request_parent, sizeof(write_request_parent));
                    std::memcpy(ptr->data.buf.data() + sizeof(write_request_parent), file_content.data(), file_content.size());
                    slsfs::jsre::request_parser<slsfs::base::byte> write_request_parser{ptr};

                    // creating another write request for directory file
                    slsfs::pack::packet_pointer ptr2 = std::make_shared<slsfs::pack::packet>();
                    ptr2->header.gen();
                    ptr2->header.key = input.file_key();

                    // create a stat and put in the dir file
                    slsfs::jsre::meta::stats stat2;
                    stat2.to_network_format();

                    slsfs::jsre::request write_request_dir{
                        .type = slsfs::jsre::type_t::file,
                        .operation = slsfs::jsre::operation_t::write,
                        .position = 0,
                        .size = sizeof(stat2)};

                    write_request_dir.to_network_format();

                    ptr2->data.buf.resize(sizeof(write_request_dir) + sizeof(stat2));
                    std::memcpy(ptr2->data.buf.data(), &write_request_dir, sizeof(write_request_dir));
                    std::memcpy(ptr2->data.buf.data() + sizeof(write_request_dir), &stat2, sizeof(stat2));

                    slsfs::jsre::request_parser<slsfs::base::byte> write_request_parser_2{ptr2};
                    std::vector<slsfs::jsre::request_parser<slsfs::base::byte>> inputs = {write_request_parser, write_request_parser_2};
                    start_2pc_prepare_multi_inputs(inputs, next);
                });

            slsfs::jsre::request_parser<slsfs::base::byte> read_request_input{ptr};
            start_read(read_request_input, readnext);
        }

        void start_meta_rename_part_2(slsfs::jsre::request_parser<slsfs::base::byte> const &input,
                                      slsfs::jsre::meta::filemeta_server const &filemeta, std::string const &new_file_name,
                                      std::shared_ptr<std::function<void(slsfs::jsre::rename_sub_operation_t, slsfs::pack::packet_pointer)>> next,
                                      std::uint32_t readsize = 4096)
        {
            slsfs::log::log("start_meta_rename_part_2");
            slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
            ptr->header.gen();
            ptr->header.key = input.uuid();

            slsfs::jsre::request read_request{
                .type = slsfs::jsre::type_t::file,
                .operation = slsfs::jsre::operation_t::read,
                .position = 0,
                .size = readsize};

            read_request.to_network_format();
            ptr->data.buf.resize(sizeof(read_request));
            std::memcpy(ptr->data.buf.data(), &read_request, sizeof(read_request));

            auto readnext = std::make_shared<slsfs::backend::ssbd::handler>(
                [input, next, filemeta, new_file_name, this](slsfs::base::buf file_content)
                {
                    slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
                    slsfs::log::log("start_meta_rename_part_2 get read resp: size={}", file_content.size());
                    slsfs::jsre::meta::stats stat;
                    if (file_content.size() < sizeof(stat))
                    {
                        recoder_.erase_checked(input.uuid());
                        std::invoke(*next, slsfs::jsre::rename_sub_operation_t::directory_not_found, ptr);
                        return;
                    }

                    // get number of files
                    std::memcpy(std::addressof(stat), file_content.data(), sizeof(stat));
                    stat.to_host_format();

                    // not enough content. reread all file
                    if (sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server) > file_content.size())
                    {
                        start_meta_rename_part_2(input, filemeta, new_file_name, next, sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server));
                        return;
                    }

                    slsfs::jsre::meta::filemeta_server current_filemeta;
                    for (std::uint32_t i = 1; i <= stat.file_count; ++i)
                    {
                        unsigned int const pos = i * sizeof(current_filemeta);
                        std::memcpy(std::addressof(current_filemeta),
                                    file_content.data() + pos,
                                    sizeof(current_filemeta));

                        auto end = std::find(current_filemeta.file_name.begin(), current_filemeta.file_name.end(), '\0');
                        std::string current_file_name(current_filemeta.file_name.begin(), end);
                        if (current_file_name == new_file_name)
                        {
                            std::invoke(*next, slsfs::jsre::rename_sub_operation_t::file_name_already_exists, ptr);
                            return;
                        }
                    }

                    // calculate the append address of stat in the parent directry file
                    std::uint32_t const position = (++stat.file_count) * sizeof(filemeta);

                    // add new filemeta in the parent file directory
                    file_content.resize(position + sizeof(filemeta));
                    std::memcpy(file_content.data() + position,
                                std::addressof(filemeta), sizeof(filemeta));

                    stat.to_network_format();

                    // update stat at position 0 in the parent file directory
                    std::memcpy(file_content.data(), std::addressof(stat), sizeof(stat));

                    // create write request to update the parent file directory
                    slsfs::jsre::request write_request{
                        .type = slsfs::jsre::type_t::file,
                        .operation = slsfs::jsre::operation_t::write,
                        .position = 0,
                        .size = static_cast<std::uint32_t>(file_content.size())};

                    write_request.to_network_format();

                    ptr->header.gen();
                    ptr->header.key = input.uuid();
                    ptr->data.buf.resize(sizeof(write_request) + file_content.size());

                    std::memcpy(ptr->data.buf.data(), &write_request, sizeof(write_request));
                    std::memcpy(ptr->data.buf.data() + sizeof(write_request), file_content.data(), file_content.size());

                    std::invoke(*next, slsfs::jsre::rename_sub_operation_t::ok_part_2, ptr);
                });
            slsfs::jsre::request_parser<slsfs::base::byte> read_request_input{ptr};
            start_read(read_request_input, readnext);
        }

        void start_meta_rename_part_3(slsfs::jsre::request_parser<slsfs::base::byte> const &input, slsfs::pack::key_t const &new_file_key,
                                      std::shared_ptr<std::function<void(slsfs::jsre::rename_sub_operation_t, slsfs::pack::packet_pointer)>> next,
                                      std::uint32_t readsize = 4096)
        {
            slsfs::log::log("start_meta_rename_file_part_3");
            slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
            ptr->header.gen();
            ptr->header.key = input.file_key();

            slsfs::jsre::request read_request{
                .type = slsfs::jsre::type_t::file,
                .operation = slsfs::jsre::operation_t::read,
                .position = 0,
                .size = readsize};

            read_request.to_network_format();
            ptr->data.buf.resize(sizeof(read_request));
            std::memcpy(ptr->data.buf.data(), &read_request, sizeof(read_request));

            auto readnext = std::make_shared<slsfs::backend::ssbd::handler>(
                [input, next, new_file_key, this](slsfs::base::buf buf)
                {
                    slsfs::log::log("start_meta_rename_file_part_3 get read resp: size={}", buf.size());

                    slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
                    ptr->header.gen();
                    ptr->header.key = new_file_key;

                    slsfs::jsre::request r{
                        .type = slsfs::jsre::type_t::file,
                        .operation = slsfs::jsre::operation_t::write,
                        .position = 0,
                        .size = static_cast<std::uint32_t>(buf.size())};
                    r.to_network_format();

                    ptr->data.buf.resize(sizeof(r) + buf.size());
                    std::memcpy(ptr->data.buf.data(), &r, sizeof(r));
                    std::memcpy(ptr->data.buf.data() + sizeof(r), buf.data(), buf.size());
                    slsfs::log::log("start_meta_rename_file_part_3 completed");
                    std::invoke(*next, slsfs::jsre::rename_sub_operation_t::ok_part_3, ptr);
                    return;
                });
            slsfs::jsre::request_parser<slsfs::base::byte> read_request_input{ptr};

            start_read(read_request_input, readnext);
        }

        void start_meta_rename_file(slsfs::jsre::request_parser<slsfs::base::byte> const &input,
                                    slsfs::backend::ssbd::handler_ptr next,
                                    std::uint32_t readsize = 4096)
        {
            slsfs::log::log("start_meta_rename_file {}", input.print());

            slsfs::jsre::meta::rename_info rename_info;
            std::memcpy(std::addressof(rename_info), input.data(), sizeof(rename_info));

            slsfs::pack::key_t new_file_key = rename_info.new_file_key;
            slsfs::pack::key_t parent_key = rename_info.file_parent_key;
            std::string new_file_name(rename_info.new_file_name.begin(), std::find(rename_info.new_file_name.begin(), rename_info.new_file_name.end(), '\0'));
            std::string file_name = input.file_name();
            bool same_parent_dir = slsfs::pack::key_compare(parent_key, input.uuid());

            slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
            ptr->header.gen();
            ptr->header.key = parent_key;

            slsfs::jsre::request read_request{
                .type = slsfs::jsre::type_t::file,
                .operation = slsfs::jsre::operation_t::read,
                .position = 0,
                .size = readsize};

            read_request.to_network_format();
            ptr->data.buf.resize(sizeof(read_request));
            std::memcpy(ptr->data.buf.data(), &read_request, sizeof(read_request));

            auto readnext = std::make_shared<slsfs::backend::ssbd::handler>(
                [input, next, parent_key, file_name, new_file_name, new_file_key, same_parent_dir, this](slsfs::base::buf file_content)
                {
                    slsfs::jsre::meta::stats stat;
                    if (file_content.size() < sizeof(stat))
                    {
                        recoder_.erase_checked(parent_key);
                        std::invoke(*next, slsfs::base::to_buf("Error: No such directory"));
                        return;
                    }

                    // get number of files
                    std::memcpy(std::addressof(stat), file_content.data(), sizeof(stat));
                    stat.to_host_format();

                    // not enough content. reread all file
                    if (sizeof(stat) +  stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server) > file_content.size())
                    {
                        start_meta_rename_file(input, next, sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server));
                        return;
                    }

                    // check if the file exists in the parent directory file
                    bool file_found = false;
                    slsfs::jsre::meta::filemeta_server current_filemeta;
                    for (std::uint32_t i = 1; i <= stat.file_count; ++i)
                    {
                        unsigned int const pos = i * sizeof(current_filemeta);
                        std::memcpy(std::addressof(current_filemeta),
                                    file_content.data() + pos,
                                    sizeof(current_filemeta));

                        auto end = std::find(current_filemeta.file_name.begin(), current_filemeta.file_name.end(), '\0');
                        std::string current_file_name(current_filemeta.file_name.begin(), end);
                        if (current_file_name == file_name)
                        {
                            current_filemeta.file_name = {};
                            std::memcpy(current_filemeta.file_name.data(),
                                        new_file_name.data(), std::min(new_file_name.size(), sizeof(current_filemeta)));
                            // check if the renaming is in the same directory
                            if (same_parent_dir)
                            {
                                if (new_file_name == file_name)
                                {
                                    std::invoke(*next, slsfs::base::to_buf("Error: File name already exists"));
                                    return;
                                }

                                // file found, change only the name of the file
                                std::memcpy(file_content.data() + pos,
                                            std::addressof(current_filemeta), sizeof(current_filemeta));
                            }
                            else
                            {
                                // file found, swap with the last entry if it's not already the last
                                if (i < stat.file_count)
                                {
                                    std::memcpy(file_content.data() + pos,
                                                file_content.data() + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server),
                                                sizeof(slsfs::jsre::meta::filemeta_server));
                                }
                            }
                            file_found = true;
                            break;
                        }
                    }

                    if (!file_found)
                    {
                        std::invoke(*next, slsfs::base::to_buf("Error: File not found"));
                        return;
                    }

                    if (!same_parent_dir)
                    {
                        // update file count
                        stat.file_count--;
                        slsfs::log::log("start_meta_rename_file removed file, now have {} files", stat.file_count);
                        // update file content size
                    }

                    file_content.resize(sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server));

                    if (!same_parent_dir)
                    {
                        // update stat at position 0 in the parent directory file
                        stat.to_network_format();
                        std::memcpy(file_content.data(), std::addressof(stat), sizeof(stat));
                    }

                    // create write request to update the parent directory of the initial file
                    slsfs::jsre::request write_request_parent_dir{
                        .type = slsfs::jsre::type_t::file,
                        .operation = slsfs::jsre::operation_t::write,
                        .position = 0,
                        .size = static_cast<std::uint32_t>(file_content.size())};

                    write_request_parent_dir.to_network_format();
                    slsfs::pack::packet_pointer ptr1 = std::make_shared<slsfs::pack::packet>();
                    ptr1->header.gen();
                    ptr1->header.key = parent_key;

                    ptr1->data.buf.resize(sizeof(write_request_parent_dir) + file_content.size());
                    std::memcpy(ptr1->data.buf.data(), &write_request_parent_dir, sizeof(write_request_parent_dir));
                    std::memcpy(ptr1->data.buf.data() + sizeof(write_request_parent_dir), file_content.data(), file_content.size());

                    /// part2 and part3

                    auto all_requests_successful = std::make_shared<std::atomic<bool>>(true);
                    auto outstanding_requests = std::make_shared<std::atomic<int>>(1);
                    auto error_string = std::make_shared<std::string>("");
                    auto ptr2 = std::make_shared<slsfs::pack::packet_pointer>();
                    auto ptr3 = std::make_shared<slsfs::pack::packet_pointer>();

                    auto readnext = std::make_shared<std::function<void(slsfs::jsre::rename_sub_operation_t, slsfs::pack::packet_pointer)>>(
                        [all_requests_successful, outstanding_requests, same_parent_dir, next, ptr1, ptr2, ptr3, error_string, this](
                            slsfs::jsre::rename_sub_operation_t operation_type, slsfs::pack::packet_pointer pack)
                        {
                            switch (operation_type)
                            {
                            case slsfs::jsre::rename_sub_operation_t::file_not_found:
                                *error_string = "File not found";
                                *all_requests_successful = false;
                                break;
                            case slsfs::jsre::rename_sub_operation_t::directory_not_found:
                                *error_string = "No such directory";
                                *all_requests_successful = false;
                                break;
                            case slsfs::jsre::rename_sub_operation_t::file_name_already_exists:
                                *error_string = "File name already exists";
                                *all_requests_successful = false;
                                break;
                            case slsfs::jsre::rename_sub_operation_t::ok_part_2:
                                *ptr2 = pack;
                                break;
                            case slsfs::jsre::rename_sub_operation_t::ok_part_3:
                                *ptr3 = pack;
                                break;
                            }

                            // all requests are completed and successful
                            if (--(*outstanding_requests) == 0 && *all_requests_successful)
                            {
                                std::vector<slsfs::jsre::request_parser<slsfs::base::byte>> inputs;
                                slsfs::jsre::request_parser<slsfs::base::byte> write_request_input_1{ptr1};
                                inputs.push_back(std::move(write_request_input_1));

                                if (!same_parent_dir)
                                {
                                    slsfs::jsre::request_parser<slsfs::base::byte> write_request_input_2{*ptr2};
                                    inputs.push_back(std::move(write_request_input_2));
                                }

                                slsfs::jsre::request_parser<slsfs::base::byte> write_request_input_3{*ptr3};
                                inputs.push_back(std::move(write_request_input_3));

                                start_2pc_prepare_multi_inputs(inputs, next);
                            }
                            // one of the requests returned an error
                            else if ((*outstanding_requests) == 0)
                            {
                                std::invoke(*next, slsfs::base::to_buf("Error: " + (*error_string)));
                            }

                            // // add timeout
                        });

                    if (!same_parent_dir)
                    {
                        ++(*outstanding_requests);
                        start_meta_rename_part_2(input, current_filemeta, new_file_name, readnext);
                    }

                    start_meta_rename_part_3(input, new_file_key, readnext, slsfs::pack::hton(current_filemeta.file_size));
                });

            slsfs::jsre::request_parser<slsfs::base::byte> read_request_input{ptr};
            start_read(read_request_input, readnext);
        }

        void start_create_and_write_file_single_request(slsfs::jsre::request_parser<slsfs::base::byte> const &input,
                                                        slsfs::backend::ssbd::handler_ptr next,
                                                        std::uint32_t readsize = 4096)
        {
            slsfs::log::log("start_create_and_write_file_single_request {}", input.print());
            slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
            ptr->header.gen();
            ptr->header.key = input.uuid();

            slsfs::jsre::request read_request{
                .type = slsfs::jsre::type_t::file,
                .operation = slsfs::jsre::operation_t::read,
                .position = 0,
                .size = readsize};

            read_request.to_network_format();
            ptr->data.buf.resize(sizeof(read_request));
            std::memcpy(ptr->data.buf.data(), &read_request, sizeof(read_request));

            auto readnext = std::make_shared<slsfs::backend::ssbd::handler>(
                [input, next, this](slsfs::base::buf file_content)
                {
                    slsfs::log::log("start_create_and_write_file_single_request get read resp: size={}", file_content.size());
                    slsfs::jsre::meta::stats stat;
                    if (file_content.size() < sizeof(stat))
                    {
                        recoder_.erase_checked(input.uuid());
                        std::invoke(*next, slsfs::base::to_buf("Error: No such directory"));
                        return;
                    }

                    // get number of files
                    std::memcpy(std::addressof(stat), file_content.data(), sizeof(stat));
                    stat.to_host_format();

                    // not enough content. reread all file
                    if (sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server) > file_content.size())
                    {
                        start_create_and_write_file_single_request(input, next, sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server));
                        return;
                    }

                    // get metadata from request (network format)
                    slsfs::jsre::meta::filemeta filemeta;
                    std::memcpy(std::addressof(filemeta), input.data(), sizeof(filemeta));
                    filemeta.to_host_format();

                    std::string file_name = input.file_name();

                    slsfs::jsre::meta::filemeta_server current_filemeta;
                    for (std::uint32_t i = 1; i <= stat.file_count; ++i)
                    {
                        unsigned int const pos = i * sizeof(current_filemeta);
                        std::memcpy(std::addressof(current_filemeta),
                                    file_content.data() + pos,
                                    sizeof(current_filemeta));

                        auto end = std::find(current_filemeta.file_name.begin(), current_filemeta.file_name.end(), '\0');
                        std::string current_file_name(current_filemeta.file_name.begin(), end);
                        if (current_file_name == file_name)
                        {
                            std::invoke(*next, slsfs::base::to_buf("Error: File already exists"));
                            return;
                        }
                    }

                    auto now = std::chrono::system_clock::now();
                    auto now_time_t = std::chrono::system_clock::to_time_t(now);
                    // prepare the file meta data
                    slsfs::jsre::meta::filemeta_server final_file_meta = {
                        .owner = filemeta.owner,
                        .permission = filemeta.permission,
                        .file_size = 0, // temporary ?????
                        .creation_time = static_cast<uint64_t>(now_time_t),
                        .modification_time = static_cast<uint64_t>(now_time_t),
                        .file_name = {}};

                    std::memcpy(final_file_meta.file_name.data(), file_name.data(), std::min(file_name.size(), sizeof(final_file_meta)));
                    final_file_meta.to_network_format();

                    // calculate the append address of the stat in the parent directory file
                    std::uint32_t const position = (++stat.file_count) * sizeof(final_file_meta);

                    // append stat in the parent directory
                    file_content.resize(position + sizeof(final_file_meta));
                    std::memcpy(file_content.data() + position,
                                std::addressof(final_file_meta), sizeof(final_file_meta));

                    stat.to_network_format();

                    // update file content at position 0 (stat position)
                    std::memcpy(file_content.data(), std::addressof(stat), sizeof(stat));

                    slsfs::jsre::request write_request{
                        .type = slsfs::jsre::type_t::file,
                        .operation = slsfs::jsre::operation_t::write,
                        .position = 0,
                        .size = static_cast<std::uint32_t>(file_content.size())};

                    write_request.to_network_format();
                    slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
                    ptr->header.gen();
                    ptr->header.key = input.uuid();
                    ptr->data.buf.resize(sizeof(write_request) + file_content.size());
                    std::memcpy(ptr->data.buf.data(), &write_request, sizeof(write_request));
                    std::memcpy(ptr->data.buf.data() + sizeof(write_request), file_content.data(), file_content.size());

                    slsfs::jsre::request_parser<slsfs::base::byte> write_request_parser{ptr};

                    slsfs::jsre::request write_request2{
                        .type = slsfs::jsre::type_t::file,
                        .operation = slsfs::jsre::operation_t::write,
                        .position = 0,
                        .size = static_cast<std::uint32_t>(input.size() - sizeof(slsfs::jsre::meta::filemeta))};

                    write_request2.to_network_format();

                    slsfs::pack::packet_pointer ptr2 = std::make_shared<slsfs::pack::packet>();
                    ptr2->header.gen();
                    ptr2->header.key = input.file_key();
                    ptr2->data.buf.resize(sizeof(write_request2) + input.size() - sizeof(slsfs::jsre::meta::filemeta));
                    std::memcpy(ptr2->data.buf.data(), &write_request2, sizeof(write_request2));
                    std::memcpy(ptr2->data.buf.data() + sizeof(write_request2), input.data() + sizeof(slsfs::jsre::meta::filemeta), input.size() - sizeof(slsfs::jsre::meta::filemeta));
                    slsfs::jsre::request_parser<slsfs::base::byte> write_request_parser2{ptr2};

                    std::vector<slsfs::jsre::request_parser<slsfs::base::byte>> inputs = {write_request_parser, write_request_parser2};

                    start_2pc_prepare_multi_inputs(inputs, next);
                });

            slsfs::jsre::request_parser<slsfs::base::byte> read_request_input{ptr};
            start_read(read_request_input, readnext);
        }

        void start_meta_open_file(slsfs::jsre::request_parser<slsfs::base::byte> const &input,
                                  slsfs::backend::ssbd::handler_ptr next,
                                  std::uint32_t readsize = 4096)
        {

            slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
            ptr->header.gen();
            ptr->header.key = input.uuid();

            slsfs::jsre::request read_request{
                .type = slsfs::jsre::type_t::file,
                .operation = slsfs::jsre::operation_t::read,
                .position = 0,
                .size = readsize};

            read_request.to_network_format();
            ptr->data.buf.resize(sizeof(read_request));
            std::memcpy(ptr->data.buf.data(), &read_request, sizeof(read_request));

            auto readnext = std::make_shared<slsfs::backend::ssbd::handler>(
                [input, next, this](slsfs::base::buf file_content)
                {
                    slsfs::log::log("start_meta_open_file get read resp: size={}", file_content.size());
                    slsfs::jsre::meta::stats stat;
                    if (file_content.size() < sizeof(stat))
                    {
                        recoder_.erase_checked(input.uuid());
                        std::invoke(*next, slsfs::base::to_buf("Error: No such directory"));
                        return;
                    }

                    // get number of files
                    std::memcpy(std::addressof(stat), file_content.data(), sizeof(stat));
                    stat.to_host_format();

                    // not enough content. reread all file
                    if (sizeof(stat) +  stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server) > file_content.size())
                    {
                        start_meta_open_file(input, next, sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server));
                        return;
                    }

                    // check if the file exists
                    std::string file_name = input.file_name();
                    bool file_found = false;

                    slsfs::jsre::meta::filemeta_server current_filemeta;
                    for (std::uint32_t i = 1; i <= stat.file_count; ++i)
                    {
                        unsigned int const pos = i * sizeof(current_filemeta);
                        std::memcpy(std::addressof(current_filemeta),
                                    file_content.data() + pos,
                                    sizeof(current_filemeta));

                        auto end = std::find(current_filemeta.file_name.begin(), current_filemeta.file_name.end(), '\0');
                        std::string current_file_name(current_filemeta.file_name.begin(), end);
                        if (current_file_name == file_name)
                        {
                            file_found = true;
                            break;
                        }
                    }

                    if (!file_found)
                    {
                        std::invoke(*next, slsfs::base::to_buf("Error: File not found"));
                        return;
                    }
                    std::invoke(*next, slsfs::base::to_buf("OK"));
                });

            slsfs::jsre::request_parser<slsfs::base::byte> read_request_input{ptr};
            start_read(read_request_input, readnext);
        }

        void start_meta_ls(slsfs::jsre::request_parser<slsfs::base::byte> const &input,
                           slsfs::backend::ssbd::handler_ptr next,
                           std::uint32_t readsize = 4096)
        {
            slsfs::log::log("start_meta_ls {}", input.print());

            slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
            ptr->header.gen();
            ptr->header.key = input.uuid();

            slsfs::jsre::request read_request{
                .type = slsfs::jsre::type_t::file,
                .operation = slsfs::jsre::operation_t::read,
                .position = 0,
                .size = readsize};

            read_request.to_network_format();
            ptr->data.buf.resize(sizeof(read_request));
            std::memcpy(ptr->data.buf.data(), &read_request, sizeof(read_request));

            auto readnext = std::make_shared<slsfs::backend::ssbd::handler>(
                [input, next, this](slsfs::base::buf file_content)
                {
                    slsfs::log::log("start_meta_ls get file resp: size={}", file_content.size());
                    slsfs::jsre::meta::stats stat;
                    if (file_content.size() < sizeof(stat))
                    {
                        recoder_.erase_checked(input.uuid());

                        std::invoke(*next, slsfs::base::to_buf("Error: No such directory"));
                        return;
                    }

                    // get number of files
                    std::memcpy(std::addressof(stat), file_content.data(), sizeof(stat));
                    stat.to_host_format();

                    // not enough content. reread all file
                    if (sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server) > file_content.size())
                    {
                        start_meta_ls(input, next, sizeof(stat) + stat.file_count * sizeof(slsfs::jsre::meta::filemeta_server));
                        return;
                    }

                    slsfs::log::log("start_meta_ls stat have {} files", stat.file_count);

                    slsfs::base::buf output;

                    std::string const total = fmt::format("Total: {} files/directories\n", stat.file_count);
                    output.insert(output.end(), total.begin(), total.end());
                    for (unsigned int i = 1; i <= stat.file_count; i++)
                    {
                        slsfs::jsre::meta::filemeta_server filemeta;
                        unsigned int const pos = i * sizeof(filemeta);
                        std::memcpy(std::addressof(filemeta), file_content.data() + pos, sizeof(filemeta));
                        auto end = std::find(filemeta.file_name.begin(), filemeta.file_name.end(), '\0');
                        output.insert(output.end(), filemeta.file_name.begin(), end);
                        output.push_back('\n');

                        filemeta.to_host_format();
                        std::string info = fmt::format("Owner: {}\t  Permission: {:o}\t  Size: {} bytes\t  Created: {}\t  Modified: {}\t\n",
                                                       filemeta.owner,
                                                       filemeta.permission,
                                                       filemeta.file_size,
                                                       format_time(filemeta.creation_time),
                                                       format_time(filemeta.modification_time));
                        output.insert(output.end(), info.begin(), info.end());
                    }
                    std::invoke(*next, output);
                });
            slsfs::jsre::request_parser<slsfs::base::byte> read_request_input{ptr};
            start_read(read_request_input, readnext);
        }

        void start_read_file(slsfs::jsre::request_parser<slsfs::base::byte> const &input,
                             slsfs::backend::ssbd::handler_ptr next)
        {

            slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
            ptr->header.gen();
            ptr->header.key = input.file_key();

            slsfs::jsre::request read_request{
                .type = slsfs::jsre::type_t::file,
                .operation = slsfs::jsre::operation_t::read,
                .position = input.position(),
                .size = input.size()};

            read_request.to_network_format();
            ptr->data.buf.resize(sizeof(read_request));
            std::memcpy(ptr->data.buf.data(), &read_request, sizeof(read_request));
            slsfs::jsre::request_parser<slsfs::base::byte> read_request_input{ptr};
            start_read(read_request_input, next);
        }

    public:
        storage_conf_ssbd_backend(boost::asio::io_context &io) : io_context_{io} {}

        auto headersize() -> std::uint32_t
        {
            return 0;
        };
        virtual auto blocksize() -> std::uint32_t override
        {
            return storage_conf::blocksize() - headersize();
        }

        void init(slsfs::base::json const &config) override
        {
            replication_size_ = config["replication_size"].get<int>();

            // setup normal operating host
            for (auto &&element : config["hosts"])
            {
                std::string const host = element["host"].get<std::string>();
                std::string const port = element["port"].get<std::string>();
                slsfs::log::log("adding {}:{}", host, port);

                backend_list_.push_back(std::make_shared<slsfs::backend::ssbd>(io_context_, host, port));
            }

            replication_start_index_ = backend_list_.size();
            // setup replication operating host (but as the second half of the vector)
            for (auto &&element : config["hosts"])
            {
                std::string const host = element["host"].get<std::string>();
                std::string const port = element["port"].get<std::string>();
                slsfs::log::log("adding replication {}:{}", host, port);

                backend_list_.push_back(std::make_shared<slsfs::backend::ssbd>(io_context_, host, port));
            }
            storage_conf::init(config);
        }

        bool use_async() override
        {
            return true;
        }

        void start_perform(slsfs::jsre::request_parser<slsfs::base::byte> const &input,
                           std::function<void(slsfs::base::buf)> next) override
        {
            auto next_ptr = std::make_shared<std::function<void(slsfs::base::buf)>>(std::move(next));
            switch (input.operation())
            {
            case slsfs::jsre::operation_t::write:
                slsfs::log::log("start_perform -> slsfs::jsre::operation_t::write");
                start_write(input, next_ptr);
                break;

            case slsfs::jsre::operation_t::read:
                slsfs::log::log("start_perform -> slsfs::jsre::operation_t::read");
                start_read_file(input, next_ptr);
                break;
            }
        }

        void start_perform_metadata(slsfs::jsre::request_parser<slsfs::base::byte> const &input,
                                    std::function<void(slsfs::base::buf)> next) override
        {
            auto next_ptr = std::make_shared<std::function<void(slsfs::base::buf)>>(std::move(next));
            switch (input.meta_operation())
            {
            case slsfs::jsre::meta_operation_t::addfile:
                start_meta_add_file(input, next_ptr);
                break;
            case slsfs::jsre::meta_operation_t::init:
                start_meta_init(input, next_ptr);
                break;
            case slsfs::jsre::meta_operation_t::mkdir:
                start_meta_mkdir(input, next_ptr);
                break;
            case slsfs::jsre::meta_operation_t::ls:
                start_meta_ls(input, next_ptr);
                break;
            case slsfs::jsre::meta_operation_t::deletefile:
                start_meta_delete_file(input, next_ptr);
                break;
            case slsfs::jsre::meta_operation_t::rename_sameproxy:
                start_meta_rename_file(input, next_ptr);
                break;
            case slsfs::jsre::meta_operation_t::getstatfile:
                start_meta_get_stat(input, next_ptr);
                break;
            case slsfs::jsre::meta_operation_t::getstatdir:
                start_meta_get_stat(input, next_ptr, true);
                break;
            case slsfs::jsre::meta_operation_t::open:
                start_meta_open_file(input, next_ptr);
                break;
            case slsfs::jsre::meta_operation_t::addfile_write:
                start_create_and_write_file_single_request(input, next_ptr);
                // Do nothing on close
                break;
            case slsfs::jsre::meta_operation_t::close:
                // Do nothing on close
                break;
            }
        }
    };

} // namespace slsfsdf

#endif // STORAGE_CONF_SSBD_BACKEND_HPP__
