#pragma once
#ifndef STORAGE_CONF_HPP__
#define STORAGE_CONF_HPP__

#include <slsfs.hpp>
#include <memory>
#include <vector>

namespace slsfsdf
{

// Used to configurate the backend storage configuration.
class storage_conf
{
protected:
    std::uint32_t fullsize_ = 4096;

    std::vector<std::shared_ptr<slsfs::storage::interface>> hostlist_;

    virtual
    void connect()
    {
        for (std::shared_ptr<slsfs::storage::interface>& host : hostlist_)
            host->connect();
    }

public:
    virtual ~storage_conf() {}

    virtual void close() {}

    virtual
    void init(slsfs::base::json const& config)
    {
        if (config.contains("blocksize"))
            fullsize_ = config["blocksize"].get<std::uint32_t>();
        connect();
    }

    virtual auto blocksize() -> std::uint32_t { return fullsize_; }
    virtual bool use_async() { return false; }

    virtual
    auto perform(slsfs::jsre::request_parser<slsfs::base::byte> const& input)
        -> slsfs::base::buf
    {
        assert(false && "to use perform, please override this function");
        return {};
    }

    virtual
    auto perform_metadata(slsfs::jsre::request_parser<slsfs::base::byte> const& input)
        -> slsfs::base::buf
    {
        assert(false && "to use perform_metadata, please override this function");
        return {};
    }

    virtual
    void start_perform (slsfs::jsre::request_parser<slsfs::base::byte> const& input,
                        std::function<void(slsfs::base::buf)> next) {
        assert(false && "to use start_perform, please override this function");
    }

    virtual
    void start_perform_metadata (slsfs::jsre::request_parser<slsfs::base::byte> const& input,
                                 std::function<void(slsfs::base::buf)> next) {
        assert(false && "to use start_perform_metadata, please override this function");
    }
};

} // namespace slsfsdf

#endif // STORAGE_CONF_HPP__
