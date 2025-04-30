#pragma once

#ifndef SLSFS_BASETYPES_HPP__
#define SLSFS_BASETYPES_HPP__

#include <vector>
#include <cstdint>
#include <sstream>
#include <chrono>

#include <nlohmann/json.hpp>
#include "cppcodec/base64_url.hpp"

namespace slsfs::base
{

using byte = std::uint8_t;
using buf = std::vector<byte>;

using json = nlohmann::json;

auto decode(std::string const& v) -> buf
{
    using base64 = cppcodec::base64_url;
    return base64::decode(v);
}

template<typename Container>
auto encode(Container const& v) -> std::string
{
    using base64 = cppcodec::base64_url;
    return base64::encode(v);
}

auto decode_kafkajson(std::string const& v) -> nlohmann::json
{
    using base64 = cppcodec::base64_url;
    buf b = base64::decode(v);
    std::string origin_json;
    std::copy(b.begin(), b.end(), std::back_inserter(origin_json));

    return nlohmann::json::parse(origin_json);
}

auto encode_kafkajson(nlohmann::json const& v) -> std::string
{
    using base64 = cppcodec::base64_url;
    return base64::encode(v.dump());
}

auto to_string(buf const& v) -> std::string
{
    std::stringstream ss;
    //ss << "[ ";
    for (byte b : v)
        ss << b;
    //ss << "]";
    return ss.str();
}

buf to_buf(std::string const& s)
{
    buf b;
    b.reserve(s.size());
    std::copy(s.begin(), s.end(), std::back_inserter(b));
    return b;
}

template<typename T>
buf to_buf(T const& t)
{
    buf b(sizeof(T), 0);
    std::memcpy(b.data(), &t, sizeof(T));
    return b;
}

template<typename T>
T   from_buf(buf const& b)
{
    T t;
    std::memcpy(&t, b.data(), b.size());
    return t;
}

auto parsename(std::string const& path) -> std::pair<std::string, std::string>
{
    auto pos = path.find_last_of('/') + 1;
    return {path.substr(0, pos), path.substr(pos)};
}

using time_point = std::chrono::high_resolution_clock::time_point;

auto now() -> time_point {
    return std::chrono::high_resolution_clock::now();
}

} // namespace base

#endif // SLSFS_BASETYPES_HPP__
