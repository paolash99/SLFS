#pragma once

#ifndef PERSISTENT_LOG_HPP__
#define PERSISTENT_LOG_HPP__

#include "leveldb-serializer.hpp"
#include <leveldb/cache.h>
#include <leveldb/db.h>

#include <fmt/core.h>

namespace ssbd
{

class persistent_log
{
    std::unique_ptr<leveldb::Cache> cache_ = nullptr;
    std::unique_ptr<leveldb::DB> db_log_ = nullptr;

public:
    persistent_log (std::string const &dbname, std::size_t const cache_size)
    {
        leveldb::DB* db = nullptr;
        leveldb::Options options;

        cache_.reset(leveldb::NewLRUCache(cache_size));

        options.create_if_missing = true;
        options.write_buffer_size = 32 * 1024 * 1024;
        options.block_cache = cache_.get();

        leveldb::Status status = leveldb::DB::Open(options, std::string(dbname), &db);
        if (not status.ok())
        {
            BOOST_LOG_TRIVIAL(error) << status.ToString() << "\n";
            throw std::runtime_error("cannot open db");
        }

        BOOST_LOG_TRIVIAL(debug) << "open db log ptr: " << db << "\n";
        db_log_.reset(db);
    }

    auto get_committed_version (std::string const& key) -> slsfs::leveldb_pack::versionint_t
    {
        std::string const commit_version_key = key + "-committed-version";
        std::string commit_version_buffer;// (sizeof(slsfs::leveldb_pack::versionint_t), '\0');
        db_log_->Get(leveldb::ReadOptions(), commit_version_key, &commit_version_buffer);

        if (commit_version_buffer.empty())
            return 0;

        try
        {
            return std::stoll(commit_version_buffer);
        } catch (std::exception&) {
            BOOST_LOG_TRIVIAL(error) << "in get_committed_version, error on converting '" << commit_version_buffer << "' to number";
            commit_version_buffer = "0";
            db_log_->Put(leveldb::WriteOptions(), commit_version_key, commit_version_buffer);
            return 0;
        }
    }

    void put_committed_version (std::string const& key, slsfs::leveldb_pack::versionint_t version)
    {
        std::string const commit_version_key = key + "-committed-version";
        std::string commit_version_buffer = std::to_string(version);// (sizeof(slsfs::leveldb_pack::versionint_t), '\0');

        // save as network format
        //version = slsfs::leveldb_pack::hton(version);
        //std::memcpy(commit_version_buffer.data(), &version, sizeof(slsfs::leveldb_pack::versionint_t));
        BOOST_LOG_TRIVIAL(trace) << "put to log " << commit_version_buffer;
        db_log_->Put(leveldb::WriteOptions(), commit_version_key, commit_version_buffer);
    }

    void put_pending_prepare (std::string const& key, std::string const& value, slsfs::leveldb_pack::versionint_t version)
    {
        { // save version
            std::string version_value = fmt::format("{}", version);// (sizeof(slsfs::leveldb_pack::versionint_t), '\0');
            std::string const version_key = key + "-version";

            // save as network format
            //version = slsfs::leveldb_pack::hton(version);
            //std::memcpy(version_value.data(), &version, sizeof(slsfs::leveldb_pack::versionint_t));

            db_log_->Put(leveldb::WriteOptions(), version_key, version_value);
        }

        {
            std::string const log_key = key + "-data";
            db_log_->Put(leveldb::WriteOptions(), log_key, value);
        }
    }

    auto get_pending_prepare_data (std::string const& key) -> std::string
    {
        std::string const log_key = key + "-data";
        std::string result;
        db_log_->Get(leveldb::ReadOptions(), log_key, &result);
        //BOOST_LOG_TRIVIAL(trace) << "read pending data " << result;
        return result;
    }

    auto get_pending_prepare_version (std::string const& key) -> slsfs::leveldb_pack::versionint_t
    {
        std::string const version_key = key + "-version";
        std::string version_value;

        db_log_->Get(leveldb::ReadOptions(), version_key, &version_value);

        if (version_value.empty())
            return 0;

        try
        {
            return std::stoll(version_value);
        } catch (std::exception&) {
            BOOST_LOG_TRIVIAL(error) << "in get_pending_prepare_version, error on converting '" << version_value << "' to number";
            version_value = "0";
            db_log_->Put(leveldb::WriteOptions(), version_key, version_value);
            return 0;
        }
    }

    void commit_pending_prepare (std::string const& key, leveldb::DB& save_dest)
    {
        std::string const value = get_pending_prepare_data(key);
        slsfs::leveldb_pack::versionint_t const version = get_pending_prepare_version(key);
        BOOST_LOG_TRIVIAL(trace) << "commit pending prepare version: " << version << " value=" << value;

        put_committed_version(key, version);
        save_dest.Put(leveldb::WriteOptions(), key, value);
    }

    bool have_pending_log (std::string const& key)
    {
        return ! ((get_pending_prepare_version(key) == get_committed_version(key)) ||
                  (get_pending_prepare_version(key) == 0)); // 0 == empty log
    }
};

} // namespace ssbd

#endif // PERSISTENT_LOG_HPP__
