#pragma once

#ifndef SLSFS_STORAGE_HPP__
#define SLSFS_STORAGE_HPP__

#include "basetypes.hpp"

#include <boost/asio.hpp>

#include <functional>

namespace slsfs::storage
{

class interface
{
public:
    virtual ~interface() {}
    virtual void connect() {};

    // data operations, key interface   [str] -> buf
    // update operation
    virtual auto read_key        (pack::key_t const& name, std::size_t partition, std::size_t location, std::size_t size) -> base::buf { return {}; };
    virtual void write_key       (pack::key_t const& name, std::size_t partition, base::buf const& buffer, std::size_t location, std::uint32_t version) {};
    virtual bool check_version_ok(pack::key_t const& name, std::size_t partition, std::uint32_t& version) { return true; };
    //virtual bool check_version_ok(pack::key_t const& name, std::size_t partition, base::buf const& version_buffer) { return true; };

    virtual void start_read_key        (std::shared_ptr<pack::key_t> const name, std::size_t partition, std::size_t location, std::size_t size, std::function<void(base::buf)> completeion_handler) {};
    virtual void start_write_key       (std::shared_ptr<pack::key_t> const name, std::size_t partition, std::shared_ptr<base::buf> const buffer, std::size_t location, std::uint32_t version, std::function<void(base::buf)> completeion_handler) {};
    virtual void start_check_version_ok(std::shared_ptr<pack::key_t> const name, std::size_t partition, std::uint32_t version, std::function<void(bool)> completeion_handler) { completeion_handler(true); };

    // Metatdata operations, list interface  [str] -> buf

    // Create file operation
    virtual void append_list_key(pack::key_t const& name, base::buf const& buffer) {};
    virtual void merge_list_key (pack::key_t const& name, std::function<void(std::vector<base::buf> const&)> reduce) {};
    virtual auto get_list_key   (pack::key_t const& name) -> base::buf { return {}; };
};

}


#endif // SLSFS_STORAGE_HPP__
