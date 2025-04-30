#pragma once

#ifndef SLSFS_STORAGE_CASSANDRA_HPP__
#define SLSFS_STORAGE_CASSANDRA_HPP__

#include "storage.hpp"
#include "basetypes.hpp"
#include "scope-exit.hpp"
#include "debuglog.hpp"

#include <fmt/core.h>
#include <cassandra.h>
#include <vector>

namespace slsfs::storage
{

class cassandra : public interface
{
    CassCluster* cluster_ = nullptr;
    CassSession* session_ = nullptr;
    CassFuture*  connect_future_ = nullptr;

    std::string tablename_;

public:
    cassandra(char const * hosts, std::string const& tablename):
        tablename_{tablename}
    {
        cluster_ = cass_cluster_new();
        session_ = cass_session_new();
        cass_cluster_set_contact_points(cluster_, hosts);
        cass_cluster_set_protocol_version(cluster_, CASS_PROTOCOL_VERSION_V4);
    }

    ~cassandra()
    {
        cass_future_free(connect_future_);
        CassFuture* close_future = cass_session_close(session_);

        cass_future_wait(close_future);
        cass_future_free(close_future);

        cass_session_free(session_);
        cass_cluster_free(cluster_);
    }

    void connect() override
    {
        connect_future_ = cass_session_connect(session_, cluster_);
    }

    template<typename Callback> // Callback(CassResult const*)
    void run_query(CassStatement * statement, Callback callback)
    {
        if (cass_future_error_code(connect_future_) == CASS_OK)
        {
            CassFuture* result_future = cass_session_execute(session_, statement);
            //SCOPE_DEFER([&result_future]() { cass_future_free(result_future); });

            CassResult const * result = cass_future_get_result(result_future);
            if (result == NULL)
            {
                CassError error_code = cass_future_error_code(result_future);
                char const* error_message;
                size_t error_message_length;
                cass_future_error_message(result_future, &error_message, &error_message_length);
                std::fprintf(stderr, "Error: %s\nError Message %.*s\n",
                             cass_error_desc(error_code),
                             (int)error_message_length,
                             error_message);
            }

            std::invoke(callback, result);
        }
    }

    auto read_key(pack::key_t const& namepack, std::size_t partition, std::size_t location, std::size_t size) -> base::buf override
    {
        std::string const name = uuid::encode_base64(namepack);
        std::string query = fmt::format("SELECT value FROM {} WHERE key=?", tablename_);

        log::log(query);

        CassStatement* statement = cass_statement_new(query.c_str(), 1);
        SCOPE_DEFER([&statement]() { cass_statement_free(statement); });

        std::string const casskey = name + "-" + std::to_string(partition);
        cass_statement_bind_string(statement, 0, casskey.c_str());

        base::buf buf;
        run_query(statement,
                  [&buf] (CassResult const* result) {
                      CassRow const * row = cass_result_first_row(result);
                      if (row)
                      {
                          CassValue const * value = cass_row_get_column_by_name(row, "value");

                          char const * block = nullptr;
                          std::size_t block_length;
                          cass_value_get_string(value, &block, &block_length);
                          std::string s(block, block_length);

                          buf = base::decode(s);
                      }
                  });

        std::size_t minsize = std::min(buf.size(), size);
        base::buf selected(minsize);
        std::copy_n(std::next(buf.begin(), location), minsize, selected.begin());
        return selected;
    }

    void write_key(pack::key_t const& namepack, std::size_t partition, base::buf const& buffer, std::size_t location, std::uint32_t version) override
    {
        std::string const name = uuid::encode_base64(namepack);

        std::string query = fmt::format("INSERT INTO {} (key, value) VALUES (?, ?);", tablename_);

        log::log(query);

        CassStatement* statement = cass_statement_new(query.c_str(), 2);
        SCOPE_DEFER([&statement]() { cass_statement_free(statement); });

        std::string const casskey = name + "-" + std::to_string(partition);
        std::string const v = base::encode(buffer);
        cass_statement_bind_string(statement, 0, casskey.c_str());
        cass_statement_bind_string(statement, 1, v.c_str());

        run_query(statement, [] (CassResult const* result) {});
    }

    void append_list_key(pack::key_t const& namepack, base::buf const& buffer) override
    {
        std::string const name = uuid::encode_base64(namepack);
        std::string query = fmt::format("UPDATE {} SET value = value + ? WHERE key=?;", tablename_);

        CassStatement* statement = cass_statement_new(query.c_str(), 2);
        SCOPE_DEFER([&statement]() { cass_statement_free(statement); });

        std::string const buffer_encoded = base::encode(buffer);

        CassCollection* list = cass_collection_new(CASS_COLLECTION_TYPE_LIST, 1);
        SCOPE_DEFER([&list] { cass_collection_free(list); });
        cass_collection_append_string(list, buffer_encoded.c_str());

        cass_statement_bind_collection(statement, 0, list);
        cass_statement_bind_string(statement, 1, name.c_str());

        run_query(statement, [] (CassResult const*) {});
    }

    void merge_list_key(pack::key_t const& namepack, std::function<void(std::vector<base::buf> const&)> reduce) override
    {
        std::string const name = uuid::encode_base64(namepack);
        std::string query = fmt::format("SELECT value FROM {} WHERE key=?", tablename_);

        CassStatement* statement = cass_statement_new(query.c_str(), 1);
        SCOPE_DEFER([&statement]() { cass_statement_free(statement); });

        cass_statement_bind_string(statement, 0, name.c_str());

        run_query(statement,
                  [&reduce] (CassResult const* result) {
                      CassRow const * row = cass_result_first_row(result);
                      if (row)
                      {
                          CassValue const * value = cass_row_get_column_by_name(row, "value");
                          CassIterator * iter = cass_iterator_from_collection(value);
                          SCOPE_DEFER([&iter]{ cass_iterator_free(iter); });

                          std::vector<base::buf> items;

                          while (cass_iterator_next(iter))
                          {
                              CassValue const * v = cass_iterator_get_value(iter);
                              char const * block = nullptr;
                              std::size_t block_length;
                              cass_value_get_string(v, &block, &block_length);
                              std::string s(block, block_length);

                              items.push_back(base::decode(s));
                          }
                          std::invoke(reduce, items);
                      }
                  });
    }

    auto  get_list_key(pack::key_t const& namepack) -> base::buf override
    {
        return {};
    }
};

} // namespace storage

#endif // SLSFS_STORAGE_CASSANDRA_HPP__
