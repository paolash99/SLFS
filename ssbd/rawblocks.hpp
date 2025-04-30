#pragma once

#ifndef RAWBLOCKS_HPP__
#define RAWBLOCKS_HPP__

#include "basic.hpp"

#include <leveldb/db.h>

namespace slsfs::leveldb_pack
{

class rawblocks
{
public:
    // [data]
    std::string buf_;

    auto fullsize()   -> std::size_t& { static std::size_t s = 4 * 1024; return s; } // byte
    auto headersize() -> std::size_t  { return 0; } // byte
    auto blocksize()  -> std::size_t  { return fullsize() - headersize(); }

    template<typename InputIterator>
    void write(std::size_t logic_position, InputIterator it, std::size_t size) {
        std::copy_n(it, size, std::next(buf_.begin(), logic_position + headersize()));
    }

    template<typename OutputIterator>
    void read(std::size_t logic_position, OutputIterator ot, std::size_t size) {
        std::copy_n(std::next(buf_.begin(), logic_position + headersize()),
                    std::min(buf_.size() - headersize() - logic_position, size),
                    ot);
    }

    void move(std::string && newbuf) {
        buf_ = std::move(newbuf);
    }

    auto bind(leveldb::DB& db, std::string const& key) -> leveldb::Status {
        return db.Get(leveldb::ReadOptions(), key, &buf_);
    }

    auto flush(leveldb::DB& db, std::string const& key) -> leveldb::Status {
        return db.Put(leveldb::WriteOptions(), key, buf_);
    }
};

}; // namespace leveldb_pack

#endif // RAWBLOCKS_HPP__
