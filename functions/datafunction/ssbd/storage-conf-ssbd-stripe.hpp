#pragma once
#ifndef STORAGE_CONF_SSBD_STRIPE_HPP__
#define STORAGE_CONF_SSBD_STRIPE_HPP__

#include "storage-conf-ssbd.hpp"

#include <slsfs.hpp>

#include <oneapi/tbb/concurrent_vector.h>

#include <boost/coroutine2/all.hpp>

#include <vector>
#include <semaphore>

namespace slsfsdf
{

// Storage backend configuration for SSBD stripe
class storage_conf_ssbd_stripe : public storage_conf_ssbd
{
    int replication_size_ = 3;

    static auto static_engine() -> std::mt19937&
    {
        static thread_local std::mt19937 mt;
        return mt;
    }

    auto select_replica(slsfs::pack::key_t const& uuid, int count) -> std::vector<int> // , std::uint32_t blockid
    {
        std::seed_seq seeds {uuid.begin(), uuid.end()};
        static_engine().seed(seeds);

        std::uniform_int_distribution<> dist(0, hostlist_.size() - 1);
        auto gen = [&dist] () { return dist(static_engine()); };

        std::vector<int> rv(count);
        std::generate(rv.begin(), rv.end(), gen);

        return rv;
    }

public:
    storage_conf_ssbd_stripe(boost::asio::io_context &io): storage_conf_ssbd{io} {}

    void init(slsfs::base::json const& config) override
    {
        replication_size_ = config["replication_size"].get<int>();
        storage_conf_ssbd::init(config);
    }

    bool use_async() override { return true; }

    void start_perform(slsfs::jsre::request_parser<slsfs::base::byte> const& input,
                       std::function<void(slsfs::base::buf)> next) override
    {
        switch (input.operation())
        {
        case slsfs::jsre::operation_t::write:
        {
            start_2pc_first_phase(input, std::move(next));
            break;
        }

        case slsfs::jsre::operation_t::create:
        {
            auto uuid = input.uuid_shared();
            std::vector<int> const selected_host_index = select_replica(*uuid, replication_size_);
            auto buffer = std::make_shared<slsfs::base::buf>();
            std::copy_n(input.data(), input.size(), std::back_inserter(*buffer));
            auto next_ptr = std::make_shared<std::function<void(slsfs::base::buf)>>(next);

            start_write_key(0, selected_host_index, uuid, 0, buffer, input, next_ptr);
            break;
        }

        case slsfs::jsre::operation_t::read:
        {
            auto uuid = input.uuid_shared();
            auto next_ptr = std::make_shared<std::function<void(slsfs::base::buf)>>(next);
            start_read_key(uuid, input, next_ptr);
            break;
        }
        }
    }

    void start_2pc_first_phase (slsfs::jsre::request_parser<slsfs::base::byte> const& input, std::function<void(slsfs::base::buf)> next)
    {
        auto const uuid = input.uuid_shared();

        std::vector<int> const selected_host_index = select_replica(*uuid, replication_size_);

        start_check_version_ok(0, selected_host_index,
                               uuid, 0,
                               input,
                               std::move(next));
    }

    void start_check_version_ok (int const write_index, std::vector<int> const selected_host_index,
                                 std::shared_ptr<slsfs::pack::key_t> const uuid, std::uint32_t version,
                                 slsfs::jsre::request_parser<slsfs::base::byte> const& input,
                                 std::function<void(slsfs::base::buf)> next)
    {
        slsfs::log::log("ssbd check version, starting with index {}, {}", write_index, selected_host_index.at(write_index));
        auto selected = hostlist_.at(selected_host_index.at(write_index));

        int const blockid = input.position() / blocksize();

        selected->start_check_version_ok(
            uuid, blockid, version,
            [=, this, next=std::move(next)] (bool ok) {
                if (not ok)
                {
                    slsfs::log::log("ssbd check version failed");
                    std::invoke(next, std::vector<unsigned char>{'F', 'A', 'I', 'L'});
                }
                else
                {
                    int next_index = write_index + 1;
                    if (next_index < replication_size_)
                        start_check_version_ok(
                            next_index, selected_host_index,
                            uuid, version,
                            input,
                            std::move(next));
                    else
                        start_2pc_second_phase(
                            uuid, version,
                            input,
                            std::move(next));
                }
            });
    }

    void start_2pc_second_phase(std::shared_ptr<slsfs::pack::key_t> const uuid, std::uint32_t version,
                                slsfs::jsre::request_parser<slsfs::base::byte> const& input,
                                std::function<void(slsfs::base::buf)> next)
    {
        auto buffer = std::make_shared<slsfs::base::buf>();
        std::copy_n(input.data(), input.size(), std::back_inserter(*buffer));

        std::vector<int> const selected_host_index = select_replica(*uuid, replication_size_);

        auto next_ptr = std::make_shared<std::function<void(slsfs::base::buf)>>(next);
        start_write_key(0, selected_host_index,
                        uuid,
                        version,
                        buffer,
                        input,
                        next_ptr);
    }

    void start_write_key(int const write_index, std::vector<int> const selected_host_index,
                         std::shared_ptr<slsfs::pack::key_t> const uuid,
                         std::uint32_t version,
                         std::shared_ptr<slsfs::base::buf> buffer,
                         slsfs::jsre::request_parser<slsfs::base::byte> const& input,
                         std::shared_ptr<std::function<void(slsfs::base::buf)>> next_ptr)
    {
        slsfs::log::log("ssbd start_write_key, starting with index {}", write_index);
        auto selected = hostlist_.at(selected_host_index.at(write_index));

        std::uint32_t const realpos = input.position();
        std::uint32_t const endpos  = realpos + buffer->size();

        std::uint32_t processpos  = realpos;
        std::uint32_t index_count = 0;
        while (processpos < endpos)
        {
            std::uint32_t offset = processpos % blocksize();
            std::uint32_t blockwritesize = std::min<std::uint32_t>(endpos - processpos, blocksize() - offset);
            processpos += blockwritesize;
            index_count++;
        }

        auto outstanding_writes = std::make_shared<std::atomic<int>>(index_count);

        std::uint32_t currentpos = realpos, buf_pointer = 0;

        while (currentpos < endpos)
        {
            std::uint32_t blockid = currentpos / blocksize();
            std::uint32_t offset  = currentpos % blocksize();
            slsfs::log::log("_data_ perform_single_request writing");

            std::uint32_t blockwritesize = std::min<std::uint32_t>(endpos - currentpos, blocksize() - offset);

            slsfs::log::log("_data_ async write perform_single_request sending: {}, {}, {}", blockid, offset, blockwritesize);
            auto partial_buf = std::make_shared<slsfs::base::buf> (blockwritesize);
            std::copy_n(buffer->begin() + buf_pointer, blockwritesize, partial_buf->begin());

            currentpos  += blockwritesize;
            buf_pointer += blockwritesize;

            selected->start_write_key(
                uuid, blockid,
                partial_buf, offset,
                version,
                [this, outstanding_writes, write_index, selected_host_index, version,
                 uuid, buffer, input, next_ptr] (slsfs::base::buf return_val) {
                    (*outstanding_writes)--;

                    // still have outstanding writes
                    if (*outstanding_writes != 0)
                        return;

                    // return at the primary finish
                    if (write_index == 0)
                    {
                        slsfs::log::log("finish first repl, executing next");
                        std::invoke(*next_ptr, return_val);
                    }

                    // replicate to other index
                    int const next_index = write_index + 1;
                    if (next_index < replication_size_)
                        start_write_key(
                            next_index, selected_host_index,
                            uuid,
                            version,
                            buffer,
                            input,
                            nullptr);
                });
        }
    }

    struct buf_stat_t
    {
        std::atomic<bool> ready = false;
        slsfs::base::buf  buf;
    };

    void start_read_key(std::shared_ptr<slsfs::pack::key_t> const uuid,
                        slsfs::jsre::request_parser<slsfs::base::byte> const input,
                        std::shared_ptr<std::function<void(slsfs::base::buf)>> next_ptr)
    {
        slsfs::log::log("ssbd start_read_key");

        std::uint32_t const realpos  = input.position();
        std::uint32_t const readsize = input.size(); // input["size"].get<std::size_t>();
        std::uint32_t const endpos   = realpos + readsize;

        if (readsize == 0)
        {
            std::invoke(*next_ptr, slsfs::base::buf{});
            return;
        }


        // dry run
        std::uint32_t processpos  = realpos;
        std::uint32_t index_count = 0;
        while (processpos < endpos)
        {
            std::uint32_t offset  = processpos % blocksize();
            std::uint32_t blockreadsize = std::min<std::uint32_t>(endpos - processpos, blocksize() - offset);
            processpos += blockreadsize;
            index_count++;
        }

        auto result_accumulator = std::make_shared<oneapi::tbb::concurrent_vector<buf_stat_t>>(index_count);

        std::uint32_t currentpos = realpos;
        slsfs::base::buf b;
        for (std::uint32_t index = 0; currentpos < endpos; index++)
        {
            std::uint32_t blockid = currentpos / blocksize();
            std::uint32_t offset  = currentpos % blocksize();
            slsfs::log::log("_data_ perform_single_request reading");
            std::uint32_t blockreadsize = std::min<std::uint32_t>(endpos - currentpos, blocksize() - offset);

            currentpos += blockreadsize;
            slsfs::log::log("_data_ async read perform_single_request sending: {}, {}, {}", blockid, offset, blockreadsize);

            auto selected = hostlist_.at(select_replica(*uuid, 1).front());

//            auto readed = selected->read_key(
//                uuid, blockid,
//                offset, blockreadsize);

            selected->start_read_key(
                uuid, blockid,
                offset, blockreadsize,
                [index, result_accumulator, next_ptr] (slsfs::base::buf b) {
                    result_accumulator->at(index).ready = true;
                    result_accumulator->at(index).buf   = std::move(b);

                    for (buf_stat_t& bufstat : *result_accumulator)
                        if (not bufstat.ready)
                        {
                            slsfs::log::log("read data have incomplete transfer. Wait for next call");
                            return;
                        }

                    slsfs::log::log("read executing next");
                    slsfs::base::buf collect;
                    for (buf_stat_t& bufstat : *result_accumulator)
                        collect.insert(collect.begin(),
                                       bufstat.buf.begin(),
                                       bufstat.buf.end());

                    slsfs::log::log("read executing next with bufsize = {}", collect.size());
                    std::invoke(*next_ptr, std::move(collect));
                });
        }
    }
};

} // namespace slsfsdf

#endif // STORAGE_CONF_SSBD_STRIPE_HPP__
