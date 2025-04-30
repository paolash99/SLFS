#pragma once
#ifndef SLSFS_DIRECT_EMULATION_NODE_HPP__
#define SLSFS_DIRECT_EMULATION_NODE_HPP__

#include "proxy/basic.hpp"
#include "emulation_node.hpp"
#include "proxy/json-replacement.hpp"
#include "proxy/serializer.hpp"
#include "proxy/base64-conv.hpp"
#include "proxy/trigger.hpp"

#define BOOST_NO_CXX11_SCOPED_ENUMS
#include <boost/filesystem.hpp>
#undef BOOST_NO_CXX11_SCOPED_ENUMS
#include <boost/asio.hpp>
#include <fmt/core.h>

#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <cmath>

class slsfs_direct_emulation_node : public emulation_node
{
    boost::asio::io_context& io_;
public:
    slsfs_direct_emulation_node (boost::asio::io_context& io): io_{io} {}

    int read_request (std::string filename, double bytes, std::shared_ptr<std::promise<std::string>> result)
    {
        auto const start = std::chrono::high_resolution_clock::now();
        std::string const buf(bytes, 'A');

        nlohmann::json request = R"({
            "type": "",
            "launch": "direct",
            "name": "",
            "pos": 0,
            "data": "",
            "size": 0,
            "blocksize":  4096,
            "storagetype": "ssbd",
            "storageconfig": {
                "hosts": [
                    {"host": "192.168.0.66",  "port": "12000"},
                    {"host": "192.168.0.111", "port": "12000"},
                    {"host": "192.168.0.45",  "port": "12000"},
                    {"host": "192.168.0.147", "port": "12000"},
                    {"host": "192.168.0.98",  "port": "12000"},
                    {"host": "192.168.0.159", "port": "12000"},
                    {"host": "192.168.0.175", "port": "12000"},
                    {"host": "192.168.0.75",  "port": "12000"},
                    {"host": "192.168.0.158", "port": "12000"}
                ],
                "replication_size": 3
            }
        })"_json;

        request["type"] = "read";
        request["name"] = slsfs::base64::encode(filename.begin(), filename.end());
        request["data"] = "";
        request["size"] = buf.size();

        std::string const url = fmt::format("https://ow-ctrl/api/v1/namespaces/_/actions/slsfs-datafunction-0?blocking=true&result=false");
        slsfs::trigger::make_trigger (io_, url)
            ->register_on_read(
                [this, result, bytes, start]
                (std::shared_ptr<slsfs::http::response<slsfs::http::string_body>> res) mutable {
                    try
                    {
                        auto const end = std::chrono::high_resolution_clock::now();
                        auto resp = nlohmann::json::parse(res->body());

                        std::string from = resp["response"]["result"]["data"].get<std::string>(), stringresp;
                        slsfs::base64::decode(from, std::back_inserter(stringresp));

                        auto relativetime = std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
                        auto throughput = (bytes * 0.000001) / (relativetime * 0.000001);

                        result->set_value("," + std::to_string(relativetime) + "," + std::to_string(std::lround(bytes)) + "," + "read" + "," + std::to_string(throughput));

                        BOOST_LOG_TRIVIAL(debug) << "response: " << resp["activationId"] << " result: " << stringresp;
                    }
                    catch (...)
                    {
                    }
                })
            .start_post(request.dump());
        return 0;
    }

    int write_request(string filename, double bytes, std::shared_ptr<std::promise<std::string>> result)
    {
        result->set_value(filename);
        return bytes;
    }
};

#endif // SLSFS_DIRECT_EMULATION_NODE_HPP__
