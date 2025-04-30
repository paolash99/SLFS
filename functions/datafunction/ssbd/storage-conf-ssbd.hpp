#pragma once
#ifndef STORAGE_CONF_SSBD_HPP__
#define STORAGE_CONF_SSBD_HPP__

#include <slsfs.hpp>

#include "storage-conf.hpp"

namespace slsfsdf
{

// Storage backend configuration for SSBD
class storage_conf_ssbd : public storage_conf
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
    storage_conf_ssbd(boost::asio::io_context &io): io_context_{io} {}

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

    auto perform(slsfs::jsre::request_parser<slsfs::base::byte> const& input) -> slsfs::base::buf override
    {
        slsfs::base::buf response;
        switch (input.operation())
        {
        case slsfs::jsre::operation_t::write:
        {
            auto const write_buf = input.data();

            std::uint32_t realpos  = input.position();
            std::uint32_t writesize = input.size(); // input["size"].get<std::size_t>();
            std::uint32_t endpos   = realpos + writesize;
            while (realpos < endpos)
            {
                std::uint32_t blockid = realpos / blocksize();
                std::uint32_t offset  = realpos % blocksize();
                std::uint32_t blockwritesize = std::min<std::uint32_t>(endpos - realpos, blocksize());

                if (offset + writesize > blocksize())
                    blockwritesize = blocksize() - offset;
                realpos += blockwritesize;

                auto buf_ptr = std::make_shared<slsfs::base::buf>(blockwritesize);

                std::copy_n(write_buf, blockwritesize, buf_ptr->begin());

                auto const uuid = input.uuid_shared();
                std::vector<int> const selected_host_index = select_replica(*uuid, replication_size_);

                auto it = selected_host_index.begin();
                std::shared_ptr<slsfs::storage::interface> host = hostlist_.at(*it);
                host->write_key(*uuid, blockid, *buf_ptr, offset, 0);
                it++;
            }
            response = {'O', 'K'};
            break;
        }

        case slsfs::jsre::operation_t::create:
        {
            break;
        }

        case slsfs::jsre::operation_t::read:
        {
            std::uint32_t realpos  = input.position();
            std::uint32_t readsize = input.size(); // input["size"].get<std::size_t>();
            std::uint32_t endpos   = realpos + readsize;
            while (realpos < endpos)
            {
                std::uint32_t blockid = realpos / blocksize();
                std::uint32_t offset  = realpos % blocksize();
                slsfs::log::log("_data_ perform_single_request reading");

                std::uint32_t blockreadsize = std::min<std::uint32_t>(endpos - realpos, blocksize());
                if (offset + readsize > blocksize())
                    blockreadsize = blocksize() - offset;
                realpos += blockreadsize;

                auto const uuid = input.uuid_shared();
                std::vector<int> const selected_host_index = select_replica(*uuid, replication_size_);

                slsfs::log::log("_data_ read perform_single_request sending: {}, {}, {}", blockid, offset, blockreadsize);
                auto it = selected_host_index.begin();
                std::shared_ptr<slsfs::storage::interface> host = hostlist_.at(*it);
                slsfs::base::buf resp = host->read_key(input.uuid(), blockid, offset, blockreadsize);
                response.insert(response.end(), resp.begin(), resp.end());
            }
            break;
        }
        }

        return response;
    }
};

} // namespace slsfsdf

#endif // STORAGE_CONF_SSBD_HPP__
