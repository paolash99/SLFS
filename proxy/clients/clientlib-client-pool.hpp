#pragma once
#ifndef CLIENTLIB_CLIENT_POOL_HPP__
#define CLIENTLIB_CLIENT_POOL_HPP__

#include "clientlib.hpp"
#include "../scope_exit.hpp"

#include <boost/asio.hpp>

#include <vector>
#include <mutex>

namespace slsfs::client
{

template<typename T>
concept SlsfsClient = requires(T client)
{
    { client.send(std::declval<slsfs::pack::packet_pointer>()) }
        -> std::convertible_to<std::string>;
};


template<SlsfsClient ClientType>
class client_pool
{
    std::vector<boost::asio::io_context> io_contexts_;
    std::vector<ClientType> slsfs_clients_;
    std::vector<std::mutex> slsfs_client_mutexes_;

    std::mt19937 engine_{42};
    std::uniform_int_distribution<int> dist_;

public:
    client_pool(int pool_size, std::string const& zookeeper_host):
        io_contexts_(pool_size),
        slsfs_client_mutexes_(pool_size),
        dist_(0, pool_size - 1)
    {
        slsfs_clients_.reserve(pool_size);
        for (int i = 0; i < pool_size; i++)
            slsfs_clients_.emplace_back(io_contexts_.at(i), zookeeper_host);
    }

    auto send(slsfs::pack::packet_pointer request) -> std::string
    {
        for (;;)
        {
            int i = dist_(engine_);
            if (slsfs_client_mutexes_.at(i).try_lock())
            {
                SCOPE_DEFER([&m=slsfs_client_mutexes_.at(i)] { m.unlock(); });
                return slsfs_clients_.at(i).send(request);
            }
        }
        return "";
    }
};

} // namespace slsfs::client
#endif // CLIENTLIB_CLIENT_POOL_HPP__
