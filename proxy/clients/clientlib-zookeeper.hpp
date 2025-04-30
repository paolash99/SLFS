#pragma once

#ifndef CLIENT_CLIENTLIB_ZOOKEEPER_HPP__
#define CLIENT_CLIENTLIB_ZOOKEEPER_HPP__

#include "../uuid.hpp"
#include "../basic.hpp"

#include <zk/client.hpp>
#include <zk/results.hpp>
#include <zookeeper/zookeeper.h>
#include <boost/signals2.hpp>
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <boost/thread/future.hpp>
#include <oneapi/tbb/concurrent_hash_map.h>

namespace slsfs::client
{

class zookeeper : public std::enable_shared_from_this<zookeeper>
{
    net::io_context& io_context_;
    zk::client client_;
    oneapi::tbb::concurrent_hash_map<std::string, net::ip::tcp::endpoint> uuid_cache_;
    boost::launch pool_ = boost::launch::async;
    bool closed_ = false;
    std::chrono::system_clock::duration local_zk_diff_ = std::chrono::nanoseconds::zero();
    boost::signals2::signal<void(std::vector<uuid::uuid>&)> on_reconfigure_;

#ifdef NDEBUG
    constexpr static ZooLogLevel loglevel = ZOO_LOG_LEVEL_ERROR;
#else
    //constexpr static ZooLogLevel loglevel = ZOO_LOG_LEVEL_DEBUG;
    constexpr static ZooLogLevel loglevel = ZOO_LOG_LEVEL_ERROR;
#endif // NDEBUG

public:
    zookeeper(net::io_context& io, std::string const& zkhost):
        io_context_{io},
        client_{zk::client::connect(zkhost).get()} {
        ::zoo_set_debug_level(loglevel);
    }

    template<typename OnReconfigure>
    void bind_reconfigure(OnReconfigure reconf) {
        on_reconfigure_.connect(reconf);
    }

    void start_watch()
    {
        //BOOST_LOG_TRIVIAL(trace) << "watch /slsfs/proxy start";

        client_.watch_children("/slsfs/proxy").then(
            pool_,
            [self=shared_from_this()] (zk::future<zk::watch_children_result> children) {
                auto&& res = children.get();
               // BOOST_LOG_TRIVIAL(trace) << "set watch ok";

                res.next().then(
                    self->pool_,
                    [self=self->shared_from_this(), children=std::move(children)] (zk::future<zk::event> event) {
                        zk::event const & e = event.get();
                        BOOST_LOG_TRIVIAL(trace) << "watch event get: " << e.type();
                        self->start_watch();
                        self->reconfigure();
                    });
            });
    }

void reconfigure()
    {
        BOOST_LOG_TRIVIAL(trace) << "Client::start_reconfigure";

        std::vector<std::string> list = client_.get_children("/slsfs/proxy").get().children();

        std::vector<std::string> new_list;
        size_t new_size = 0;
        for (const auto& entry : list) {
            auto [host, port, liveliness] = get_details(entry);
            if (liveliness == 1) {
                new_list.push_back(entry);
                // Increment the size by the size of the entry
                new_size += entry.size();
        }
    }

        std::vector<uuid::uuid> new_proxy_list;
        std::transform (new_list.begin(), new_list.end(),
                        std::back_inserter(new_proxy_list),
                        uuid::decode_base64);

        std::sort(new_proxy_list.begin(), new_proxy_list.end());
        on_reconfigure_(new_proxy_list);
    }

     auto get_details(std::string const &child) -> std::tuple<std::string, std::string, int>
        {
            using namespace std::string_literals;

            decltype(uuid_cache_)::accessor result;
            if (uuid_cache_.find(result, child))
            {
                // Return the cached result if available. Test this
                net::ip::tcp::endpoint cachedEndpoint = result->second;
                return {cachedEndpoint.address().to_string(), std::to_string(cachedEndpoint.port()), 0};
            }

            zk::future<zk::get_result> resp = client_.get("/slsfs/proxy/"s + child);
            zk::buffer const buf = resp.get().data();

            auto const colon1 = std::find(buf.begin(), buf.end(), ':');
            auto const colon2 = std::find(colon1 + 1, buf.end(), ':');

            // Extract host
            std::string host(buf.begin(), colon1);

            // Extract port
            std::string port(colon1 + 1, colon2);

            std::string liveliness_str(colon2 + 1, buf.end());
            int liveliness = std::stoi(liveliness_str);

            // Updated here: Provide the length of the port substring
            net::ip::tcp::resolver resolver(io_context_);
            for (net::ip::tcp::endpoint resolved : resolver.resolve(host, port))
            {
                uuid_cache_.emplace(child, resolved);
                return {resolved.address().to_string(), std::to_string(resolved.port()), liveliness};
            }

            // Return an empty pair if resolution fails
            return {};
        }

    auto get_uuid (std::string const& child) -> net::ip::tcp::endpoint
    {
    using namespace std::string_literals;

            decltype(uuid_cache_)::accessor result;
            if (uuid_cache_.find(result, child))
                return result->second;

            zk::future<zk::get_result> resp = client_.get("/slsfs/proxy/"s + child);
            zk::buffer const buf = resp.get().data();


	    auto const colon1 = std::find(buf.begin(), buf.end(), ':');
    		auto const colon2 = std::find(colon1 + 1, buf.end(), ':');

    	// Extract host
    	std::string host(buf.begin(), colon1);

    	// Extract port
    	std::string port(colon1 + 1, colon2);

	   // BOOST_LOG_TRIVIAL(info) << "Host: " << host << ", Port: " << port;
            net::ip::tcp::resolver resolver(io_context_);
            for (net::ip::tcp::endpoint resolved : resolver.resolve(host, port))
            {
                // net::ip::tcp::endpoint resolved = resolver.resolve(host, port);
                uuid_cache_.emplace(child, resolved);
                return resolved;
            }
            return {};
    }
};


} // namespace client

#endif // CLIENT_CLIENTLIB_ZOOKEEPER_HPP__
