#pragma once
#ifndef ZOOKEEPER_HPP__
#define ZOOKEEPER_HPP__

#include "basic.hpp"
#include "uuid.hpp"
#include "launcher.hpp"

#include <zk/client.hpp>
#include <zk/results.hpp>
#include <zookeeper/zookeeper.h>
#include <Poco/Base64Decoder.h>
#include <boost/asio.hpp>
#include <boost/chrono.hpp>
#include <boost/thread.hpp>
#include <boost/thread/executors/basic_thread_pool.hpp>
#include <Poco/Base64Decoder.h>
#include <oneapi/tbb/concurrent_hash_map.h>

#include <string>
#include <chrono>
#include <thread>
#include <vector>

namespace slsfs::zookeeper
{
    // assumes owner is main()
    // Zookeeper is responsible for consensus between multiple proxies.
    class zookeeper
    {
        net::io_context &io_context_;
        zk::client client_;
        launcher::launcher &launcher_;
        uuid::uuid uuid_;
        std::vector<char> announce_buf_;
        oneapi::tbb::concurrent_hash_map<std::string, net::ip::tcp::endpoint> uuid_cache_;
        //    boost::executors::basic_thread_pool pool_;
        boost::launch pool_ = boost::launch::async;
        bool closed_ = false;
        std::chrono::system_clock::duration local_zk_diff_ = std::chrono::nanoseconds::zero();

#ifdef NDEBUG
        constexpr static ZooLogLevel loglevel = ZOO_LOG_LEVEL_ERROR;
#else
        ////constexpr static ZooLogLevel loglevel = ZOO_LOG_LEVEL_DEBUG;
        constexpr static ZooLogLevel loglevel = ZOO_LOG_LEVEL_INFO;
#endif // NDEBUG

        void erase(zk::string_view sv)
        {
            try
            {
                BOOST_LOG_TRIVIAL(trace) << "erasing " << sv;
                if (client_.exists(sv).get())
                {
                    // zk::get_children_result::children_list_type
                    auto list = client_.get_children(sv).get().children();
                    for (std::string const &child : list)
                        erase(std::string(sv) + "/" + child);
                    client_.erase(sv).get();
                }
            }
            catch (std::exception &)
            {
            }
        }

    public:
        zookeeper(net::io_context &io, launcher::launcher &l, uuid::uuid &u, std::vector<char> &announce) : io_context_{io},
                                                                                                            client_{zk::client::connect("zk://172.31.6.209:2183").get()},
                                                                                                            launcher_{l},
                                                                                                            uuid_{u},
                                                                                                            announce_buf_{announce}
        {
            ::zoo_set_debug_level(loglevel);
        }

        ~zookeeper() { shutdown(); }

        void shutdown()
        {
            closed_ = true;
            remove_uuid(uuid_.encode_base64());
        }

        void reset()
        {
            erase("/slsfs");
            BOOST_LOG_TRIVIAL(info) << "inside reset:: after erase";
            std::vector<char> dummy{};
            BOOST_LOG_TRIVIAL(trace) << "creating /slsfs";

            client_.create("/slsfs", dummy).then(pool_, [this, dummy](auto v)
                                                 {
                BOOST_LOG_TRIVIAL(info) << "create /slsfs: " << v.get();
                client_.create("/slsfs/timeadjust", dummy).get();
                client_.create("/slsfs/proxy", dummy).then(
                    pool_,
                    [this] (auto v) {
                        BOOST_LOG_TRIVIAL(info) << "reset() create /slsfs/proxy: " << v.get();
                        start_setup();
                    }); });
        }

        void start_setup()
        {
            using namespace std::string_literals;
            BOOST_LOG_TRIVIAL(info) << "inside start_setup::zk setup start";
            BOOST_LOG_TRIVIAL(info) << "start_setup() UUID before encoding: " << uuid_;

            std::string path = "/slsfs/proxy/"s + uuid_.encode_base64();

            client_.exists(path).then(
                                    pool_,
                                    [this, path](boost::future<zk::exists_result> future)
                                    {
                                        try
                                        {
                                            zk::exists_result result = future.get();
                                            if (result)
                                            {
                                                BOOST_LOG_TRIVIAL(info) << "Node " << path << " already exists.";
                                                // Perform necessary actions here if the node already exists
                                                start_watch();
                                                start_heartbeat();
                                            }
                                            else
                                            {
                                                BOOST_LOG_TRIVIAL(info) << "Node " << path << " does not exist. Creating...";
                                                client_.create(path, announce_buf_).then(pool_, [this, path](boost::future<zk::create_result> v)
                                                                                         {
                            BOOST_LOG_TRIVIAL(info) << "start_setup() create on " << path << ": " << v.get();
                            start_watch();
                            start_heartbeat(); });
                                            }
                                        }
                                        catch (const std::exception &ex)
                                        {
                                            // Handle exception
                                            BOOST_LOG_TRIVIAL(error) << "Exception occurred: " << ex.what();
                                        }
                                    })
                .then(pool_, [this](boost::future<void>) {}); // Ensure lambda chain continuation
        }

        void start_watch()
        {
            if (closed_)
                return;

            BOOST_LOG_TRIVIAL(trace) << "watch /slsfs/proxy start";

            client_.watch_children("/slsfs/proxy").then(pool_, [this](zk::future<zk::watch_children_result> children)
                                                        {
                auto&& res = children.get();
                BOOST_LOG_TRIVIAL(trace) << "set watch ok";

                res.next().then(
                    pool_,
                    [this, children=std::move(children)] (zk::future<zk::event> event) {
                        zk::event const & e = event.get();
                        BOOST_LOG_TRIVIAL(info) << "watch event get: " << e.type();
                        start_reconfigure();
                        start_watch();
                    }); });
        }

        void start_reconfigure()
        {
            BOOST_LOG_TRIVIAL(info) << "start_reconfigure";

            client_.get_children("/slsfs/proxy").then(pool_, [this](zk::future<zk::get_children_result> children)
                                                      {
                std::vector<uuid::uuid> new_proxy_list;
                std::vector<std::string> list = children.get().children();
                std::transform (list.begin(), list.end(),
                                std::back_inserter(new_proxy_list),
                                uuid::decode_base64);

                std::sort(new_proxy_list.begin(), new_proxy_list.end());
                launcher_.reconfigure(new_proxy_list.begin(),
                                      new_proxy_list.end(),
                                      *this); });
        }

        void start_heartbeat()
        {
            //BOOST_LOG_TRIVIAL(info) << "inside start_hearbeat";
            if (closed_)
                return;

            using namespace std::string_literals;
            client_.set("/slsfs/proxy/"s + uuid_.encode_base64(), announce_buf_).then(pool_, [this](zk::future<zk::set_result>)
                                                                                      {
                //BOOST_LOG_TRIVIAL(trace) << "heartbeat set: " << result.get();
                auto timer = std::make_shared<net::deadline_timer>(io_context_, boost::posix_time::seconds(2));
                timer->async_wait(
                    [this, timer] (boost::system::error_code const& error) {
                        if (error)
                            BOOST_LOG_TRIVIAL(error) << "have error = " << error;
                        else
                        {
                            start_heartbeat();
                            start_check_alive();
			    //BOOST_LOG_TRIVIAL(info) << "start_hearbeat::after start_check_alive";
                        }
                    }); });
        }

        void start_check_alive()
        {
            //BOOST_LOG_TRIVIAL(info) << "inside start_check_alive";
            if (local_zk_diff_ == std::chrono::nanoseconds::zero())
            {
                std::vector<char> dummy{};
                client_.set("/slsfs/timeadjust", dummy).then(pool_, [this](auto v)
                                                             {
                    local_zk_diff_ = std::chrono::system_clock::now() - v.get().stat().modified_time;
                    start_check_alive(); });
                return;
            }

            client_.get_children("/slsfs/proxy").then(pool_, [this](zk::future<zk::get_children_result> children)
                                                      {
                                                          std::vector<uuid::uuid> new_proxy_list;
                                                          std::vector<std::string> list = children.get().children();

                                                          std::transform(list.begin(), list.end(),
                                                                         std::back_inserter(new_proxy_list),
                                                                         uuid::decode_base64);

                                                          std::sort(new_proxy_list.begin(), new_proxy_list.end());
                                                          auto it = std::upper_bound(new_proxy_list.begin(),
                                                                                     new_proxy_list.end(),
                                                                                     uuid_);
                                                          if (it == new_proxy_list.end())
                                                              it = new_proxy_list.begin();

                                                          if (*it == uuid_)
                                                              return;

                                                          using namespace std::string_literals;

                                                          zk::future<zk::get_result> resp = client_.get("/slsfs/proxy/"s + it->encode_base64());

                                                          zk::stat const &stat = resp.get().stat();

                                                          using namespace std::chrono_literals;
                                                          if (std::chrono::system_clock::now() + local_zk_diff_ - stat.modified_time > 20s)
                                                          {
                                                              BOOST_LOG_TRIVIAL(info) << "Inside the 20s check";
                                                              auto [resolvedHost, resolvedPort, liveliness] = get_details(it->encode_base64());
                                                              BOOST_LOG_TRIVIAL(info) << "Resolved Host: " << resolvedHost << ", Port: " << resolvedPort << ", Liveliness: " << liveliness;

                                                              uuid::uuid id = *it;
                                                              double remote_server_id = std::round(id[0] / 255.0 * 100) / 100;
                                                              BOOST_LOG_TRIVIAL(info) << "remote_server_id in start_check_alive:" << remote_server_id;

                                                              if (liveliness == 1)
                                                              {
                                                                  BOOST_LOG_TRIVIAL(info) << "Inside liveliness =1";
                                                                  // Set liveliness to 0 and update in Zookeeper
                                                                  std::string updatedAnnounce = resolvedHost + ":" + resolvedPort + ":0";

                                                                  zk::buffer buffer_announce(updatedAnnounce.begin(), updatedAnnounce.end());
                                                                  client_.set("/slsfs/proxy/"s + it->encode_base64(), buffer_announce).then(pool_, [this, it](auto v)
                                                                                                                                            { BOOST_LOG_TRIVIAL(info) << "Liveliness set to 0 for proxy: " << it->encode_base64(); });
                                                              }

                                                              if (liveliness == 0)
                                                              {
                                                                  BOOST_LOG_TRIVIAL(info) << "Inside liveliness =0";
                                                                  std::string name = "proxy" + std::to_string(remote_server_id);
                                                                  std::string sshCommand = "ssh " + resolvedHost + " '/bin/start-proxy-script.sh " + std::to_string(remote_server_id) + " " + resolvedHost + " " + resolvedPort + " " + name + "'";
                                                                  BOOST_LOG_TRIVIAL(info) << "command:" << sshCommand;
                                                                  system(sshCommand.c_str());

                                                                  // Set liveliness to -1 and update in Zookeeper
                                                                  std::string updatedAnnounce = resolvedHost + ":" + resolvedPort + ":-1";

                                                                  zk::buffer buffer_announce(updatedAnnounce.begin(), updatedAnnounce.end());
                                                                  client_.set("/slsfs/proxy/"s + it->encode_base64(), buffer_announce).then(pool_, [this, it](auto v)
                                                                                                                                            { BOOST_LOG_TRIVIAL(info) << "Liveliness set to -1 for proxy: " << it->encode_base64(); });

                                                                  BOOST_LOG_TRIVIAL(info) << "after start_proxy.sh";
                                                              }
                                                          }
                                                      });
        }

        void remove_uuid(std::string const &child)
        {
            try
            {
                using namespace std::string_literals;
                // BOOST_LOG_TRIVIAL(info) << "remove_uuid::child: " << child;
                client_.erase("/slsfs/proxy/"s + child).get();
            }
            catch (...)
            {
            }
        }

        auto get_uuid(std::string const &child) -> net::ip::tcp::endpoint
        {
            using namespace std::string_literals;

            decltype(uuid_cache_)::accessor result;
            if (uuid_cache_.find(result, child))
                return result->second;

            zk::future<zk::get_result> resp = client_.get("/slsfs/proxy/"s + child);
            zk::buffer const buf = resp.get().data();

            auto const colon = std::find(buf.begin(), buf.end(), ':');

            std::string host(std::distance(buf.begin(), colon), '\0');
            std::copy(buf.begin(), colon, host.begin());

            std::string port(std::distance(std::next(colon), buf.end()), '\0');
            std::copy(std::next(colon), buf.end(), port.begin());

            net::ip::tcp::resolver resolver(io_context_);
            for (net::ip::tcp::endpoint resolved : resolver.resolve(host, port))
            {
                // net::ip::tcp::endpoint resolved = resolver.resolve(host, port);
                uuid_cache_.emplace(child, resolved);
                return resolved;
            }
            return {};
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
    };

} // namespace launcher

#endif // ZOOKEEPER_HPP__