#pragma once
#ifndef TCP_SERVER__
#define TCP_SERVER__

#include "proxy-command.hpp"

#include <slsfs.hpp>

#include <boost/lexical_cast.hpp>

#include <filesystem>

namespace slsfsdf::server
{

    template <typename ProxyCommand>
    class direct_client_connection : public std::enable_shared_from_this<direct_client_connection<ProxyCommand>>
    {
        boost::asio::io_context &io_context_;
        tcp::socket socket_;
        slsfs::socket_writer::socket_writer<slsfs::pack::packet, std::vector<slsfs::pack::unit_t>> writer_;
        // ref pxy cmd
        ProxyCommand &proxy_command_;

    public:
        using pointer = std::shared_ptr<direct_client_connection>;

        direct_client_connection(boost::asio::io_context &io, tcp::socket socket, ProxyCommand &pc) : io_context_{io},
                                                                                                      socket_{std::move(socket)},
                                                                                                      writer_{io, socket_},
                                                                                                      proxy_command_{pc} {}

        auto socket() -> tcp::socket & { return socket_; }

        void start_read_header()
        {
            // BOOST_LOG_TRIVIAL(trace) << "start_read_header";
            auto read_buf = std::make_shared<std::array<slsfs::pack::unit_t, slsfs::pack::packet_header::bytesize>>();
            boost::asio::async_read(
                socket_,
                boost::asio::buffer(read_buf->data(), read_buf->size()),
                [self = this->shared_from_this(), read_buf](boost::system::error_code ec, std::size_t /*length*/)
                {
                    if (ec)
                    {
                        if (ec != boost::asio::error::eof)
                            slsfs::log::log<slsfs::log::level::error>("start read header");
                        return;
                    }

                    slsfs::pack::packet_pointer pack = std::make_shared<slsfs::pack::packet>();
                    pack->header.parse(read_buf->data());

                    switch (pack->header.type)
                    {
                    case slsfs::pack::msg_t::trigger:
                        self->start_trigger(pack);
                        break;

                    case slsfs::pack::msg_t::put:
                    case slsfs::pack::msg_t::get:
                    case slsfs::pack::msg_t::ack:
                    case slsfs::pack::msg_t::worker_reg:
                    case slsfs::pack::msg_t::set_timer:
                    case slsfs::pack::msg_t::proxyjoin:
                    case slsfs::pack::msg_t::err:
                    case slsfs::pack::msg_t::cache_transfer:
                    case slsfs::pack::msg_t::worker_dereg:
                    case slsfs::pack::msg_t::worker_push_request:
                    case slsfs::pack::msg_t::worker_response:
                    case slsfs::pack::msg_t::open_file_success:
                    case slsfs::pack::msg_t::open_file_not_found:
                    case slsfs::pack::msg_t::rename_success:
                    case slsfs::pack::msg_t::rename_fail:
                    case slsfs::pack::msg_t::proxy_send_ok:
                    case slsfs::pack::msg_t::proxy_send_error:
                    case slsfs::pack::msg_t::proxy_send_ip:
                    case slsfs::pack::msg_t::trigger_reject:
                    {
                        slsfs::log::log<slsfs::log::level::error>("packet error from endpoint {}", boost::lexical_cast<std::string>(self->socket_.remote_endpoint()));
                        slsfs::pack::packet_pointer resp = std::make_shared<slsfs::pack::packet>();
                        resp->header = pack->header;
                        resp->header.type = slsfs::pack::msg_t::err;
                        self->start_write(resp);
                        self->start_read_header();
                        break;
                    }
                    }
                });
        }

        void start_trigger(slsfs::pack::packet_pointer pack)
        {
            slsfs::log::log("start_trigger ");
            auto read_buf = std::make_shared<std::vector<slsfs::pack::unit_t>>(pack->header.datasize);
            boost::asio::async_read(
                socket_,
                boost::asio::buffer(read_buf->data(), read_buf->size()),
                [self = this->shared_from_this(), read_buf, pack](boost::system::error_code ec, std::size_t length)
                {
                    if (ec)
                    {
                        slsfs::log::log<slsfs::log::level::error>("start read body: ");
                        return;
                    }

                    pack->data.parse(length, read_buf->data());

                    auto const start = std::chrono::high_resolution_clock::now();

                    self->proxy_command_.start_job(
                        pack,
                        [start, pack, self = self->shared_from_this()](slsfs::base::buf buf)
                        {
                            slsfs::pack::packet_pointer response = std::make_shared<slsfs::pack::packet>();
                            response->header = pack->header;
                            response->header.type = slsfs::pack::msg_t::worker_response;
                            response->data.buf = std::move(buf);

                            slsfs::jsre::request_parser<slsfs::base::byte> input{pack};
                            bool is_open_file_operation = input.type() == slsfs::jsre::type_t::metadata && input.meta_operation() == slsfs::jsre::meta_operation_t::open;
                            bool is_rename_file_operation = input.type() == slsfs::jsre::type_t::metadata && input.meta_operation() == slsfs::jsre::meta_operation_t::rename_sameproxy;
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
                    self->start_read_header();
                });
        }

        void start_write(slsfs::pack::packet_pointer pack)
        {
            auto next = std::make_shared<slsfs::socket_writer::boost_callback>(
                [self = this->shared_from_this()](boost::system::error_code ec, std::size_t /*length*/)
                {
                    if (ec)
                        slsfs::log::log<slsfs::log::level::error>("tcp conn start write error: ");
                    else
                        slsfs::log::log("tcp conn wrote msg");
                });

            writer_.start_write_socket(pack, next);
        }
    };

    class proxy_command;

    class tcp_server : public std::enable_shared_from_this<tcp_server>
    {
        boost::asio::io_context &io_context_;
        tcp::acceptor acceptor_;
        proxy_command &proxy_command_;

    public:
        tcp_server(boost::asio::io_context &io_context, proxy_command &pc, boost::asio::ip::port_type port)
            : io_context_(io_context),
              acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
              proxy_command_{pc}
        {
            slsfs::log::log<slsfs::log::level::info>("Creating listener on :{}", port);
        }

        void start_accept()
        {
            slsfs::log::log("tcp server start accepting :{}", acceptor_.local_endpoint().port());
            acceptor_.async_accept(
                [self = shared_from_this()](boost::system::error_code const &error, tcp::socket socket)
                {
                    if (error)
                    {
                        slsfs::log::log<slsfs::log::level::error>("error accepting connections: {}.", error.message());
                        return;
                    }

                    self->start_accept();

                    auto accepted = std::make_shared<direct_client_connection<proxy_command>>(
                        self->io_context_,
                        std::move(socket),
                        self->proxy_command_);

                    accepted->start_read_header();
                });
        }
    };

} // namespace slsfsdf::server

#endif
