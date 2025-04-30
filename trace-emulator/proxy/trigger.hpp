#pragma once
#ifndef TRIGGER_HPP__
#define TRIGGER_HPP__

#include "basic.hpp"
#include "serializer.hpp"

#include <boost/beast/ssl.hpp>
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wuninitialized"
#include <oneapi/tbb/concurrent_unordered_map.h>
#pragma GCC diagnostic pop

#include <boost/signals2.hpp>

#include <Poco/URI.h>
#include <oneapi/tbb/concurrent_queue.h>

#include <random>
#include <concepts>

namespace slsfs::trigger
{

struct httphost
{
    std::string host;
    std::string port;
    std::string target;

    httphost(Poco::URI const& uriparser):
        host {uriparser.getHost()},
        port {std::to_string(uriparser.getPort())},
        target {uriparser.getPathEtc()} { }

    auto gen_request() -> std::shared_ptr<http::request<http::string_body>>
    {
        auto req = std::make_shared<http::request<http::string_body>>();
        req->set(http::field::host, host);
        req->set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);
        req->set(http::field::content_type, "application/json");
//        req->set(http::field::authorization, "Basic Nzg5YzQ2YjEtNzFmNi00ZWQ1LThjNTQtODE2YWE0ZjhjNTAyOmFiY3pPM3haQ0xyTU42djJCS0sxZFhZRnBYbFBrY2NPRnFtMTJDZEFzTWdSVTRWck5aOWx5R1ZDR3VNREdJd1A="); admin
        req->set(http::field::authorization, "Basic MjNiYzQ2YjEtNzFmNi00ZWQ1LThjNTQtODE2YWE0ZjhjNTAyOjEyM3pPM3haQ0xyTU42djJCS0sxZFhZRnBYbFBrY2NPRnFtMTJDZEFzTWdSVTRWck5aOWx5R1ZDR3VNREdJd1A="); // guest
        req->target(target);
        return req;
    }
};

// StreamType = beast::ssl_stream<beast::tcp_stream> or beast::tcp_stream
using ssl_type = beast::ssl_stream<beast::tcp_stream>;

template <typename T>
concept IsSupportedStream =
    std::is_same_v<T, ssl_type> ||
    std::is_same_v<T, beast::tcp_stream>;

template<typename StreamType> requires IsSupportedStream<StreamType>
class trigger : public std::enable_shared_from_this<trigger<StreamType>>
{
    net::io_context&        io_context_;
    net::io_context::strand io_strand_;
    tcp::resolver           resolver_;

    std::unique_ptr<StreamType> stream_ptr_;
    std::atomic<bool> stream_is_connected_ = false;
    std::atomic<bool> stream_is_starting_  = false;
    std::atomic<bool> stream_is_writing_   = false;
    oneapi::tbb::concurrent_queue<std::shared_ptr<http::request<http::string_body>>> pending_requests_;

    Poco::URI          uriparser_;
    httphost           httphost_;
    std::size_t        retried_ = 0;

    boost::signals2::signal<void(std::shared_ptr<http::response<http::string_body>>)> on_read_;

public:
    template<typename ... Arguments>
    trigger (net::io_context& io, std::string const& url, Arguments && ... args):
        io_context_{io}, io_strand_{io}, resolver_{io},
        stream_ptr_{std::make_unique<StreamType>(io, std::forward<Arguments>(args)...)},
        uriparser_{url}, httphost_ {uriparser_}
    {
        using namespace std::literals;
        beast::get_lowest_layer(*stream_ptr_).expires_after(300s);
    }

    template<typename Function>
    auto register_on_read (Function &&f) -> trigger<StreamType>&
    {
        on_read_.connect(std::forward<Function>(f));
        return *this;
    }

    void start_post (std::string const &body)
    {
        BOOST_LOG_TRIVIAL(trace) << "trigger start post";
        auto req = httphost_.gen_request();
        req->body() = body;
        req->method(http::verb::post);

        start_resolve(req);
    }

private:
    void start_resolve(std::shared_ptr<http::request<http::string_body>> req)
    {
        if constexpr (std::is_same_v<StreamType, ssl_type>)
        {
            if (not SSL_set_tlsext_host_name(stream_ptr_->native_handle(), httphost_.host.c_str()))
            {
                beast::error_code ec {static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
                BOOST_LOG_TRIVIAL(error) << ec.message() << "\n";
                return;
            }
        }

        resolver_.async_resolve(
            httphost_.host, httphost_.port,
            net::bind_executor(
                io_strand_,
                [self=this->shared_from_this(), req](beast::error_code ec, tcp::resolver::results_type results) {
                if (not ec)
                    self->start_connect(results, req);
                else
                    BOOST_LOG_TRIVIAL(error) << "start_connect error: " << ec.message();
                }));
    }

    void start_connect(tcp::resolver::results_type results, std::shared_ptr<http::request<http::string_body>> req)
    {
        BOOST_LOG_TRIVIAL(trace) << "start connect";
        beast::get_lowest_layer(*stream_ptr_).async_connect(
            results,
            net::bind_executor(
                io_strand_,
                [self=this->shared_from_this(), req](beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
                    if (not ec)
                    {
                        if constexpr (std::is_same_v<StreamType, beast::ssl_stream<beast::tcp_stream>>)
                        {
                            self->stream_ptr_->async_handshake(
                                ssl::stream_base::client,
                                net::bind_executor(
                                    self->io_strand_,
                                    [self=self->shared_from_this(), req] (beast::error_code ec) {
                                        if (not ec)
                                            self->start_write(req);
                                        else
                                            BOOST_LOG_TRIVIAL(error) << "start_handshake(ssl) error: " << ec.message();
                                    }));
                        }
                        else
                            self->start_write(req);

                        self->stream_is_connected_.store(true);
                    }
                    else
                    {
                        BOOST_LOG_TRIVIAL(error) << "start_connect error: " << ec.message();
                        self->stream_is_connected_.store(false);

                        if constexpr (std::is_same_v<StreamType, beast::ssl_stream<beast::tcp_stream>>)
                        {
                            std::make_unique<StreamType>(self->io_context_, basic::ssl_ctx()).swap(self->stream_ptr_);
                            self->start_resolve(req);
                        }
                    }

                }));
    }

    void start_write(std::shared_ptr<http::request<http::string_body>> req)
    {
        BOOST_LOG_TRIVIAL(trace) << "start sending http request: " << *req;

        if (stream_is_writing_)
        {
            pending_requests_.push(req);
            return;
        }
        stream_is_writing_.store(true);

        req->prepare_payload();

        http::async_write(
            *stream_ptr_, *req,
            net::bind_executor(
                io_strand_,
                [self=this->shared_from_this(), req](beast::error_code ec, std::size_t /*bytes_transferred*/) {
                    if (not ec)
                        self->start_read();
                    else if (self->retried_ < 3)
                    {
                        self->start_resolve(req);
                        BOOST_LOG_TRIVIAL(warning) << "trigger(" << self->retried_++ << "); start_write error: " << ec.message();
                    }
                    else
                    {
                        BOOST_LOG_TRIVIAL(error) << "trigger start_write error: " << ec.message();
                        self->retried_ = 0;
                    }
                }));
    }

    void start_read()
    {
        auto res = std::make_shared<http::response<http::string_body>>();
        auto buffer = std::make_shared<beast::flat_buffer>();
        http::async_read(
            *stream_ptr_, *buffer, *res,
            net::bind_executor(
                io_strand_,
                [self=this->shared_from_this(), buffer, res]
                (beast::error_code ec, std::size_t /*bytes_transferred*/) {
                    if (ec)
                        BOOST_LOG_TRIVIAL(error) << "trigger start_read error: " << ec.message();
                    else
                    {
                        self->on_read_(res);
                        self->on_read_.disconnect_all_slots();

                        self->stream_is_connected_.store(true);
                        self->stream_is_writing_.store(false);
                        std::shared_ptr<http::request<http::string_body>> pending_request = nullptr;

                        if ((not self->pending_requests_.empty()) &&
                            self->pending_requests_.try_pop(pending_request))
                            self->start_write(pending_request);
                    }
                }));
    }
};

auto make_trigger(net::io_context& io, int const max_func_count = 0)
    -> std::shared_ptr<trigger<beast::ssl_stream<beast::tcp_stream>>>
{
    static std::mt19937 rng;
    std::random_device rd;
    rng.seed(rd());
    std::uniform_int_distribution<> dist(0, max_func_count);

    std::string const url = fmt::format("https://ow-ctrl/api/v1/namespaces/_/actions/slsfs-datafunction-{}?blocking=false&result=false", dist(rng));
    return std::make_shared<
               trigger<
                   beast::ssl_stream<
                       beast::tcp_stream>>>(
                           io, url, basic::ssl_ctx());
}

auto make_trigger(net::io_context& io, std::string const url)
    -> std::shared_ptr<trigger<beast::ssl_stream<beast::tcp_stream>>>
{
    return std::make_shared<
               trigger<
                   beast::ssl_stream<
                       beast::tcp_stream>>>(
                           io, url, basic::ssl_ctx());
}

} // namespace trigger

#endif // TRIGGER_HPP__
