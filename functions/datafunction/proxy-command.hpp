#pragma once

#ifndef PROXY_COMMAND_HPP__
#define PROXY_COMMAND_HPP__

// temp remove for compile
#include "caching.hpp"
#include "tcp-server.hpp"

#include <oneapi/tbb/concurrent_hash_map.h>
#include <boost/signals2.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>

#include <slsfs.hpp>
#include <string>
#include <optional>

namespace slsfsdf::server
{

    namespace
    {
        using boost::asio::ip::tcp;
        using namespace std::chrono_literals;
    }

    using queue_map = oneapi::tbb::concurrent_hash_map<slsfs::uuid::uuid,
                                                       std::shared_ptr<boost::asio::io_context::strand>,
                                                       slsfs::uuid::hash_compare<slsfs::uuid::uuid>>;

    // std::invocable<slsfs::base::buf(slsfsdf::storage_conf*, jsre::request_parser<base::byte> const&)>;
    template <typename Func>
    concept StorageOperationConcept = requires(Func func) {
        { std::invoke(func,
                      std::declval<slsfsdf::storage_conf *>(),
                      std::declval<slsfs::jsre::request_parser<slsfs::base::byte> const &>()) }
          -> std::convertible_to<slsfs::base::buf>;
    };

    class proxy_command;

    using proxy_set = oneapi::tbb::concurrent_hash_map<boost::asio::ip::tcp::endpoint, std::shared_ptr<proxy_command>>;

    class proxy_command : public std::enable_shared_from_this<proxy_command>
    {
        boost::asio::io_context &io_context_;
        boost::asio::ip::tcp::socket socket_;
        boost::asio::steady_timer recv_deadline_;

        using time_point = std::chrono::time_point<std::chrono::steady_clock>;
        static auto now() -> time_point { return std::chrono::steady_clock::now(); }
        time_point last_update_ = now();
        std::chrono::milliseconds waittime_ = 10s;

        std::shared_ptr<storage_conf> datastorage_conf_;
        slsfs::socket_writer::socket_writer<slsfs::pack::packet, std::vector<slsfs::pack::unit_t>> writer_;

        queue_map &queue_map_;
        proxy_set &proxy_set_;

        std::uint16_t const server_port_ = 2000;
        std::shared_ptr<tcp_server> tcp_server_ = nullptr;

        bool const enable_cache_;
        std::uint32_t const cache_size_;
        std::string const cache_policy_;
        cache::cache cache_engine_;

        void timer_reset()
        {
            recv_deadline_.cancel();

            std::chrono::steady_clock::time_point timeout_time = last_update_ + waittime_;
            recv_deadline_.expires_at(timeout_time);
            recv_deadline_.async_wait(
                [self = this->shared_from_this()](boost::system::error_code ec)
                {
                    switch (ec.value())
                    {
                    case boost::system::errc::success: // timer timeout
                        self->close();
                        break;
                    case boost::system::errc::operation_canceled: // timer canceled
                        break;
                    default:
                        slsfs::log::log<slsfs::log::level::error>("timer_reset: read header timeout. close connection()");
                        break;
                    }
                });
        }

    public:
        proxy_command(boost::asio::io_context &io_context,
                      std::shared_ptr<storage_conf> conf,
                      queue_map &qm,
                      proxy_set &ps,
                      std::uint16_t server_port,
                      bool enable_cache,
                      std::uint32_t cache_size,
                      std::string const &cache_policy)
            : io_context_{io_context}, socket_{io_context_}, recv_deadline_{io_context_},
              datastorage_conf_{conf}, writer_{io_context_, socket_},
              queue_map_{qm}, proxy_set_{ps},
              server_port_{server_port},
              tcp_server_{std::make_shared<tcp_server>(io_context_, *this, server_port)},
              enable_cache_{enable_cache},
              cache_size_{cache_size},
              cache_policy_{cache_policy},
              cache_engine_{cache_size, cache_policy}
        {
            tcp_server_->start_accept();
        }

        void close()
        {
            slsfs::pack::packet_pointer pack = std::make_shared<slsfs::pack::packet>();
            pack->header.type = slsfs::pack::msg_t::worker_dereg;
            pack->data.buf = cache_engine_.get_tables();

            slsfs::log::log("close: sending cache table to proxy");
            start_write(
                pack,
                [self = shared_from_this()](boost::system::error_code ec, std::size_t length)
                {
                    self->socket_.shutdown(tcp::socket::shutdown_receive, ec);
                    slsfs::log::log("timer_reset: send shutdown");

                    self->io_context_.stop();
                });
        }

        void start_connect(boost::asio::ip::tcp::endpoint endpoint)
        {
            socket_.async_connect(
                endpoint,
                [self = shared_from_this()](boost::system::error_code const &ec)
                {
                    if (ec)
                    {
                        slsfs::log::log<slsfs::log::level::error>("connect to proxy error: {}", ec.message());
                        return;
                    }
                    self->socket_.set_option(tcp::no_delay(true));

                    slsfs::log::log<slsfs::log::level::info>("Start listening from proxy {}", boost::lexical_cast<std::string>(self->socket_.remote_endpoint()));

                    boost::asio::ip::address_v4::bytes_type ip_bytes = self->get_host();
                    slsfs::log::log("connected. start write and listen");
                    slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
                    ptr->header.type = slsfs::pack::msg_t::worker_reg;
                    ptr->header.gen();

                    std::uint16_t port = self->server_port_;
                    port = slsfs::pack::hton(port);

                    ptr->data.buf.resize(sizeof(ip_bytes) + sizeof(port));

                    std::memcpy(ptr->data.buf.data(), std::addressof(ip_bytes), sizeof(ip_bytes));
                    std::memcpy(ptr->data.buf.data() + sizeof(ip_bytes),
                                std::addressof(port), sizeof(port));
                    self->start_write(ptr);
                    self->start_listen_commands();
                    self->timer_reset();
                });
        }

        auto get_host() -> boost::asio::ip::address_v4::bytes_type
        {
            boost::system::error_code ec;

            boost::asio::ip::tcp::resolver resolver(io_context_);
            boost::asio::ip::tcp::resolver::query query(boost::asio::ip::host_name(), "");
            boost::asio::ip::tcp::resolver::iterator it = resolver.resolve(query, ec);
            boost::asio::ip::tcp::resolver::iterator end;

            slsfs::log::log("my ip is {}", it->endpoint().address().to_string());

            if (ec)
                return {};

            return it->endpoint().address().to_v4().to_bytes();
        }

        void start_listen_commands()
        {
            // slsfs::log::log("start_listen_commands called");
            auto readbuf = std::make_shared<std::array<slsfs::pack::unit_t, slsfs::pack::packet_header::bytesize>>();

            boost::asio::async_read(
                socket_, boost::asio::buffer(readbuf->data(), readbuf->size()),
                [self = this->shared_from_this(), readbuf](boost::system::error_code ec, std::size_t /*length*/)
                {
                    if (ec)
                    {
                        slsfs::log::log<slsfs::log::level::error>("error listen command {}", ec.message());
                        return;
                    }

                    slsfs::pack::packet_pointer pack = std::make_shared<slsfs::pack::packet>();
                    pack->header.parse(readbuf->data());

                    if (pack->header.type != slsfs::pack::msg_t::set_timer)
                        self->recv_deadline_.cancel();

                    self->start_listen_commands_body(pack);
                });
        }

        void start_listen_commands_body(slsfs::pack::packet_pointer pack)
        {
            auto read_buf = std::make_shared<std::vector<slsfs::pack::unit_t>>(pack->header.datasize);
            boost::asio::async_read(
                socket_,
                boost::asio::buffer(read_buf->data(), read_buf->size()),
                [self = this->shared_from_this(), read_buf, pack](boost::system::error_code ec, std::size_t length)
                {
                    if (ec)
                    {
                        slsfs::log::log<slsfs::log::level::error>("start_listen_commands_body: "); // + ec.messag$
                        return;
                    }

                    pack->data.parse(length, read_buf->data());

                    switch (pack->header.type)
                    {
                    case slsfs::pack::msg_t::proxyjoin:
                    {
                        slsfs::log::log("switch proxy master");
                        std::uint32_t addr;
                        std::memcpy(&addr, pack->data.buf.data(), sizeof(addr));
                        addr = slsfs::pack::ntoh(addr);
                        boost::asio::ip::address_v4 new_host{addr};
                        boost::asio::ip::port_type new_port;
                        std::memcpy(&new_port, pack->data.buf.data() + 4, sizeof(new_port));
                        new_port = slsfs::pack::ntoh(new_port);

                        boost::asio::ip::tcp::endpoint ep{new_host, new_port};

                        if (proxy_set::accessor acc;
                            !self->proxy_set_.find(acc, ep))
                        {
                            slsfs::log::log("try connect to {}", boost::lexical_cast<std::string>(ep));

                            for (int i = 1;; i++)
                                try
                                {
                                    auto proxy_command_ptr = std::make_shared<slsfsdf::server::proxy_command>(
                                        self->io_context_,
                                        self->datastorage_conf_,
                                        self->queue_map_,
                                        self->proxy_set_,
                                        self->server_port_ + i,
                                        self->enable_cache_,
                                        self->cache_size_,
                                        self->cache_policy_);

                                    proxy_command_ptr->start_connect(ep);
                                    self->proxy_set_.emplace(ep, proxy_command_ptr);
                                    break;
                                }
                                catch (boost::system::system_error const &e)
                                {
                                    if (e.code() == boost::asio::error::address_in_use)
                                        slsfs::log::log("bind on {} failed\n", self->server_port_ + i);
                                    else
                                        throw;
                                }
                        }
                        break;
                    }

                    case slsfs::pack::msg_t::set_timer:
                    {
                        slsfs::pack::waittime_type duration_in_ms = 0;
                        std::memcpy(&duration_in_ms, read_buf->data(), sizeof(slsfs::pack::waittime_type));
                        self->waittime_ = slsfs::pack::ntoh(duration_in_ms) * 1ms;
                        // slsfs::log::log("set timer wait time to {}ms", slsfs::pack::ntoh(duration_in_ms));
                        break;
                    }
                    case slsfs::pack::msg_t::cache_transfer:
                    {
                        slsfs::log::log("received cache_transfer");
                        if (self->cache_engine_.eviction_policy_ == "LRU" ||
                            self->cache_engine_.eviction_policy_ == "FIFO")
                        {
                            slsfs::log::log("executing cache_transfer");
                            self->cache_engine_.build_cache(pack->data.buf, self->datastorage_conf_);
                        }
                        // for (auto chr : pack->data.buf)
                        //     slsfs::log::log("received table : '{}'", (int)chr);
                        // deserialize cache and reconstruct it
                        break;
                    }

                    default:
                        self->start_job(pack);
                    }

                    slsfs::pack::packet_pointer ok = std::make_shared<slsfs::pack::packet>();
                    ok->header = pack->header;
                    ok->header.type = slsfs::pack::msg_t::ack;

                    slsfs::log::log<slsfs::log::level::debug>(fmt::format("ACK ok for: {}", pack->header.print()));

                    self->start_write(ok);
                    if (pack->header.type != slsfs::pack::msg_t::set_timer)
                        self->last_update_ = now();

                    self->timer_reset();
                    self->start_listen_commands();
                });
        }

        void start_write(slsfs::pack::packet_pointer pack)
        {
            start_write(pack, [](boost::system::error_code, std::size_t) {});
        }

        template <typename Func>
        void start_write(slsfs::pack::packet_pointer pack, Func next)
        {
            auto next_warpper = std::make_shared<slsfs::socket_writer::boost_callback>(
                [self = this->shared_from_this(), next = std::move(next)](boost::system::error_code ec, std::size_t length)
                {
                    std::invoke(next, ec, length);
                });

            writer_.start_write_socket(pack, next_warpper);
        }

        template <typename Func>
        void start_job(slsfs::pack::packet_pointer pack, Func next)
        {
            queue_map::accessor it;
            boost::asio::io_context::strand *ptr = nullptr;
            if (not queue_map_.find(it, slsfs::uuid::to_uuid(pack->header.key)))
            {
                auto new_strand = std::make_unique<boost::asio::io_context::strand>(io_context_);
                ptr = new_strand.get();
                queue_map_.emplace(slsfs::uuid::to_uuid(pack->header.key), std::move(new_strand));
            }
            else
                ptr = it->second.get();

            boost::asio::post(
                boost::asio::bind_executor(
                    *ptr,
                    [self = this->shared_from_this(), pack, next]
                    {
                        auto const start = std::chrono::high_resolution_clock::now();

                        slsfs::jsre::request_parser<slsfs::base::byte> input{pack};
                        slsfs::log::log("process request: {}", pack->header.print());

                        if (self->datastorage_conf_->use_async())
                        {
                            self->start_storage_perform(
                                input,
                                [self = self->shared_from_this(), pack, start, next](slsfs::base::buf buf)
                                {
                                    std::invoke(next, std::move(buf));
                                });
                        }
                    }));
        }

        void start_job(slsfs::pack::packet_pointer pack)
        {
            queue_map::accessor it;
            boost::asio::io_context::strand *ptr = nullptr;
            if (not queue_map_.find(it, slsfs::uuid::to_uuid(pack->header.key)))
            {
                auto new_strand = std::make_unique<boost::asio::io_context::strand>(io_context_);
                ptr = new_strand.get();
                queue_map_.emplace(slsfs::uuid::to_uuid(pack->header.key), std::move(new_strand));
            }
            else
                ptr = it->second.get();

            boost::asio::post(
                boost::asio::bind_executor(
                    *ptr,
                    [self = this->shared_from_this(), pack]
                    {
                        auto const start = std::chrono::high_resolution_clock::now();

                        slsfs::jsre::request_parser<slsfs::base::byte> input{pack};
                        slsfs::log::log("process request: {}", pack->header.print());
                        bool is_open_file_operation = input.type() == slsfs::jsre::type_t::metadata && input.meta_operation() == slsfs::jsre::meta_operation_t::open;
                        bool is_rename_file_operation = input.type() == slsfs::jsre::type_t::metadata && input.meta_operation() == slsfs::jsre::meta_operation_t::rename_sameproxy;

                        if (self->datastorage_conf_->use_async())
                        {

                            //                        // debug
                            //                        slsfs::pack::packet_pointer response = std::make_shared<slsfs::pack::packet>();
                            //                        response->header = pack->header;
                            //                        response->header.type = slsfs::pack::msg_t::worker_response;
                            //                        response->data.buf = slsfs::base::to_buf("ok");
                            //
                            //                        self->start_write(response);

                            self->start_storage_perform(
                                input,
                                [self = self->shared_from_this(), pack, start, is_open_file_operation, is_rename_file_operation, input](slsfs::base::buf buf)
                                {
                                    slsfs::pack::packet_pointer response = std::make_shared<slsfs::pack::packet>();
                                    response->header = pack->header;
                                    response->header.type = slsfs::pack::msg_t::worker_response;
                                    response->data.buf = std::move(buf);
                                    auto end_index = std::find(response->data.buf.begin(), response->data.buf.end(), '\0');
                                    std::string buf_str(response->data.buf.begin(), end_index);
                                    if (is_open_file_operation)
                                    {
                                        slsfs::pack::key_t file_key = input.file_key();
                                        response->header.type = buf_str == "OK" ? slsfs::pack::msg_t::open_file_success : slsfs::pack::msg_t::open_file_not_found;
                                        response->data.buf = slsfs::base::to_buf(file_key);
                                    }
                                    else if (is_rename_file_operation)
                                    {
                                        slsfs::jsre::meta::rename_info rename_info;
                                        std::memcpy(std::addressof(rename_info), input.data(), sizeof(rename_info));
                                        slsfs::pack::key_t parent_key = rename_info.file_parent_key;
                                        response->header.type = buf_str == "OK" ? slsfs::pack::msg_t::rename_success : slsfs::pack::msg_t::rename_fail;
                                        response->data.buf = slsfs::base::to_buf(parent_key);
                                    }

                                    self->start_write(response);
                                    auto const end = std::chrono::high_resolution_clock::now();
                                    auto relativetime = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                                    slsfs::log::log<slsfs::log::level::debug>("req finish in: {}ns", relativetime);
                                });
                        }
                        else
                        {
                            slsfs::base::buf v = self->storage_perform(input);
                            auto end_index = std::find(v.begin(), v.end(), '\0');
                            std::string buf_str(v.begin(), end_index);
                            pack->header.type = slsfs::pack::msg_t::worker_response;
                            if (is_open_file_operation && buf_str == "OK")
                            {
                                slsfs::pack::key_t file_key = input.file_key();
                                pack->header.type = buf_str == "OK" ? slsfs::pack::msg_t::open_file_success : slsfs::pack::msg_t::open_file_not_found;
                                v = slsfs::base::to_buf(file_key);
                            }
                            else if (is_rename_file_operation)
                            {
                                slsfs::jsre::meta::rename_info rename_info;
                                std::memcpy(std::addressof(rename_info), input.data(), sizeof(rename_info));
                                slsfs::pack::key_t parent_key = rename_info.file_parent_key;
                                pack->header.type = buf_str == "OK" ? slsfs::pack::msg_t::rename_success : slsfs::pack::msg_t::rename_fail;
                                pack->data.buf = slsfs::base::to_buf(parent_key);
                            }
                            pack->data.buf.resize(v.size()); // = std::vector<slsfs::pack::unit_t>(v.size(), '\0');
                            std::memcpy(pack->data.buf.data(), v.data(), v.size());

                            self->start_write(pack);
                            auto const end = std::chrono::high_resolution_clock::now();
                            auto relativetime = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
                            slsfs::log::log("req finish in: {}", relativetime);
                        }
                    }));
        }

        auto storage_perform(slsfs::jsre::request_parser<slsfs::base::byte> const &single_input)
            -> slsfs::base::buf
        {
            switch (single_input.type())
            {
            case slsfs::jsre::type_t::file:
                if (single_input.operation() == slsfs::jsre::operation_t::write)
                {
                    slsfs::base::buf buf(single_input.size());
                    std::memcpy(buf.data(), single_input.data(), single_input.size());

                    cache_engine_.write_to_cache(single_input, buf.data());
                    return datastorage_conf_->perform(single_input);
                }
                else
                {
                    std::optional<slsfs::base::buf> cached_file = cache_engine_.read_from_cache(single_input);

                    if (cached_file)
                    {
                        slsfs::log::log("CACHE HIT");
                        return cached_file.value();
                    }
                    else // Cache miss
                    {
                        // write to cache
                        slsfs::base::buf data = datastorage_conf_->perform(single_input);
                        cache_engine_.write_to_cache(single_input, data.data());
                        return data;
                    }
                }
                break;

            case slsfs::jsre::type_t::metadata:
                return datastorage_conf_->perform(single_input);
                break;

            case slsfs::jsre::type_t::wakeup:
            case slsfs::jsre::type_t::storagetest:
                break;
            }
            return {};
        }

        void start_storage_perform(slsfs::jsre::request_parser<slsfs::base::byte> const &single_input,
                                   std::function<void(slsfs::base::buf)> next)
        {
            switch (single_input.type())
            {
            case slsfs::jsre::type_t::file:
                if (single_input.operation() == slsfs::jsre::operation_t::write)
                {
                    datastorage_conf_->start_perform(
                        single_input,
                        [next = std::move(next), single_input, self = shared_from_this()](slsfs::base::buf buf)
                        {
                            // bug?
                            // i.e. "OK" or "Error: abort"

                            std::invoke(next, buf);
                            slsfs::log::log("storage perform write finished. write to cache");
                            if (self->enable_cache_)
                                self->cache_engine_.write_to_cache(single_input, single_input.data());
                        });
                }
                else
                {
                    std::optional<slsfs::base::buf> cached_file = cache_engine_.read_from_cache(single_input);

                    if (cached_file)
                        std::invoke(next, cached_file.value());
                    else
                        datastorage_conf_->start_perform(
                            single_input,
                            [next = std::move(next), single_input, self = shared_from_this()](slsfs::base::buf buf)
                            {
                                std::invoke(next, buf);
                                if (self->enable_cache_)
                                    self->cache_engine_.write_to_cache(single_input, buf.data());
                            });
                }
                break;

            case slsfs::jsre::type_t::metadata:
                datastorage_conf_->start_perform_metadata(single_input, std::move(next));
                break;

            case slsfs::jsre::type_t::wakeup:
                break;

            case slsfs::jsre::type_t::storagetest:
                slsfs::log::log("end read from storage (1000)");
            }
        }
    };

} // namespace slsfsdf::server

#endif // PROXY_COMMAND_HPP__
