#pragma once
#ifndef STORAGE_CONF_SSBD_BASIC_ASYNC_HPP__
#define STORAGE_CONF_SSBD_BASIC_ASYNC_HPP__

#include <slsfs.hpp>

#include "storage-conf.hpp"

namespace slsfsdf
{

// Storage backend configuration for SSBD
class storage_conf_ssbd_basic_async : public storage_conf
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

protected:
    boost::asio::io_context &io_context_;
public:
    storage_conf_ssbd_basic_async(boost::asio::io_context &io): io_context_{io} {}

    bool use_async() override { return true; }

    void init(slsfs::base::json const& config) override
    {
        for (auto&& element : config["hosts"])
        {
            std::string const host = element["host"].get<std::string>();
            std::string const port = element["port"].get<std::string>();

            slsfs::log::log("adding {}:{}", host, port);

            hostlist_.push_back(std::make_shared<slsfs::storage::ssbd>(io_context_, host, port));
        }

        replication_size_ = config["replication_size"].get<int>();

        storage_conf::init(config);
    }

    auto headersize() -> std::uint32_t          { return 4; } // byte
    auto blocksize()  -> std::uint32_t override { return fullsize_ - headersize(); }

    void start_perform(slsfs::jsre::request_parser<slsfs::base::byte> const& input, std::function<void(slsfs::base::buf)> next) override
    {
        switch (input.operation())
        {
        case slsfs::jsre::operation_t::write:
        {
            start_write_key(input, std::move(next));
            break;
        }

        case slsfs::jsre::operation_t::create:
        {
            break;
        }

        case slsfs::jsre::operation_t::read:
        {
            start_read_key(input, std::move(next));
            break;
        }
        }
    }

    void start_write_key(slsfs::jsre::request_parser<slsfs::base::byte> const& input,
                         std::function<void(slsfs::base::buf)> next)
    {
        //slsfs::log::log("ssbd basic async start_write_key, starting with index {}", write_index);
        auto const uuid = input.uuid_shared();
        auto selected = hostlist_.at(select_replica(*uuid, 1).front());

        auto next_ptr = std::make_shared<std::function<void(slsfs::base::buf)>>(next);

        std::uint32_t const realpos = input.position();
        std::uint32_t const endpos  = realpos + input.size();

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
            slsfs::log::log("ssbd basic async perform_single_request writing");

            std::uint32_t blockwritesize = std::min<std::uint32_t>(endpos - currentpos, blocksize() - offset);

            slsfs::log::log("ssbd basic async write perform_single_request sending: {}, {}, {}", blockid, offset, blockwritesize);
            auto partial_buf = std::make_shared<slsfs::base::buf> (blockwritesize);
            std::copy_n(input.data() + buf_pointer, blockwritesize, partial_buf->begin());

            currentpos  += blockwritesize;
            buf_pointer += blockwritesize;

            selected->start_write_key(
                uuid, blockid,
                partial_buf, offset,
                0,
                [outstanding_writes, partial_buf,
                 uuid, input, next_ptr] (slsfs::base::buf return_val) {
                    (*outstanding_writes)--;

                    // still have outstanding writes
                    if (outstanding_writes->load() != 0)
                        return;

                    std::invoke(*next_ptr, return_val);
                });
        }
    }

    struct buf_stat_t
    {
        std::atomic<bool> ready = false;
        slsfs::base::buf  buf;
    };

    void start_read_key(slsfs::jsre::request_parser<slsfs::base::byte> const& input,
                        std::function<void(slsfs::base::buf)> next)
    {
        slsfs::log::log("ssbd start_read_key");

        std::uint32_t const realpos  = input.position();
        std::uint32_t const readsize = input.size(); // input["size"].get<std::size_t>();
        std::uint32_t const endpos   = realpos + readsize;

        if (readsize == 0)
        {
            std::invoke(next, slsfs::base::buf{});
            return;
        }

        auto const uuid = input.uuid_shared();
        auto next_ptr = std::make_shared<std::function<void(slsfs::base::buf)>>(next);

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

        for (std::uint32_t index = 0; currentpos < endpos; index++)
        {
            std::uint32_t blockid = currentpos / blocksize();
            std::uint32_t offset  = currentpos % blocksize();
            slsfs::log::log("_data_ perform_single_request reading");
            std::uint32_t blockreadsize = std::min<std::uint32_t>(endpos - currentpos, blocksize() - offset);

            currentpos += blockreadsize;
            slsfs::log::log("_data_ async read perform_single_request sending: {}, {}, {}", blockid, offset, blockreadsize);

            auto selected = hostlist_.at(select_replica(*uuid, 1).front());

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

#endif // STORAGE_CONF_SSBD_BASIC_ASYNC_HPP__
