#pragma once
#ifndef CPP_SERIALIZER_OBJECTPACK_HPP__
#define CPP_SERIALIZER_OBJECTPACK_HPP__

#include <arpa/inet.h>

#include <ios>
#include <iostream>
#include <vector>
#include <memory>
#include <cstring>
#include <array>
#include <tuple>
#include <algorithm>
#include <sstream>
#include <random>

namespace slsfs::pack
{

namespace hash
{

template <typename Integer>
void combine(std::size_t& seed, Integer value)
{
    seed ^= value + 0x9e3779b9 + (seed<<6) + (seed>>2);
}

template <typename It>
void range(std::size_t& seed, It first, It last)
{
    for(; first != last; ++first)
        combine(seed, *first);
}

} // namespace


using unit_t = std::uint8_t;
//using unit_t = char;

// key = [32] byte main key // sha256 bit
using key_t = std::array<unit_t, 256 / 8 / sizeof(unit_t)>;
enum class msg_t: unit_t
{
    err = 0,
    put = 1,
    get = 2,
    ack = 3,
    proxyjoin = 4,
    set_timer = 5,
    cache_transfer = 6,

    worker_reg = 8,
    worker_dereg = 9,
    worker_push_request = 10,
    worker_response = 11,

    trigger = 14,
    trigger_reject = 15,
    proxy_send_error = 16,
    open_file_success = 17,
    open_file_not_found = 18,
    rename_success = 19,
    rename_fail = 20,
    proxy_send_ok = 21,
    proxy_send_ip = 22
    //proxy_rename_lock_send = 21
};

auto operator << (std::ostream &os, msg_t const& msg) -> std::ostream &
{
    switch (msg)
    {
    case msg_t::err:
    {
        os << "ERR";
        break;
    }
    case msg_t::put:
    {
        os << "PUT";
        break;
    }
    case msg_t::get:
    {
        os << "GET";
        break;
    }
    case msg_t::ack:
    {
        os << "ACK";
        break;
    }
    case msg_t::proxyjoin:
    {
        os << "PXJON";
        break;
    }
    case msg_t::set_timer:
    {
        os << "STTMR";
        break;
    }
    case msg_t::worker_reg:
    {
        os << "W_REG";
        break;
    }
    case msg_t::cache_transfer:
    {
        os << "CACHE";
        break;
    }
    case msg_t::worker_dereg:
    {
        os << "W_DRG";
        break;
    }
    case msg_t::worker_push_request:
    {
        os << "W_REQ";
        break;
    }
    case msg_t::worker_response:
    {
        os << "W_RES";
        break;
    }
    case msg_t::trigger:
    {
        os << "TRIGG";
        break;
    }
    case msg_t::trigger_reject:
    {
        os << "TRIGF";
        break;
    }
    case msg_t::proxy_send_error:{
        os << "PROXY_ERROR";
        break;
    }    
    case msg_t::proxy_send_ip:{
        os << "PROXY_IP";
        break;
    }
    case msg_t::proxy_send_ok:{
        os << "PROXY_OK";
        break;
    }
    case msg_t::open_file_success:{
        os << "O_FILE_SUC";
        break;
    }
    case msg_t::open_file_not_found:{
        os << "O_FILE_NF";
        break;
    }
    case msg_t::rename_success:{
        os << "REN_FILE_SUC";
        break;
    }
    case msg_t::rename_fail:{
        os << "REN_FILE_F";
        break;
    }
    }
    return os;
}


using waittime_type = std::uint32_t;

inline uint64_t htonll(uint64_t x) {
    #if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        return ((uint64_t)htonl((uint32_t)x) << 32) | htonl((uint32_t)(x >> 32));
    #else
        return x;
    #endif
}

inline uint64_t ntohll(uint64_t x) {
    return htonll(x);
}

template<typename Integer>
auto hton(Integer i) -> Integer
{
    if constexpr (std::is_enum_v<Integer>)
        return static_cast<Integer>(hton(static_cast<std::underlying_type_t<Integer>>(i)));
    else if constexpr (sizeof(Integer) == sizeof(uint64_t))
        return htonll(static_cast<uint64_t>(i));
    else if constexpr (sizeof(Integer) == sizeof(uint32_t))
        return htonl(static_cast<uint32_t>(i));
    else if constexpr (sizeof(Integer) == sizeof(uint16_t))
        return htons(static_cast<uint16_t>(i));
    else if constexpr (sizeof(Integer) == 1)
        return i;
    else
    {
        static_assert("not supported conversion");
        return -1;
    }
}

template<typename Integer>
auto ntoh(Integer i) -> Integer
{
    if constexpr (std::is_enum_v<Integer>)
        return static_cast<Integer>(ntoh(static_cast<std::underlying_type_t<Integer>>(i)));
    else if constexpr (sizeof(Integer) == sizeof(uint64_t))
        return ntohll(static_cast<uint64_t>(i));
    else if constexpr (sizeof(Integer) == sizeof(uint32_t))
        return ntohl(static_cast<uint32_t>(i));
    else if constexpr (sizeof(Integer) == sizeof(uint16_t))
        return ntohs(static_cast<uint16_t>(i));
    else if constexpr (sizeof(Integer) == 1)
        return i;
    else
    {
        static_assert("not supported conversion");
        return -1;
    }
}

struct packet_header;
auto operator <<(std::ostream &os, packet_header const& pd) -> std::ostream&;

struct packet_header
{
    msg_t type;
    std::uint8_t direct_communication = 0;
    key_t key;
    std::uint32_t datasize; // not in byte form
    std::array<unit_t, 4> sequence{};
    std::array<unit_t, 4> random_salt{};

    static constexpr int bytesize =
        sizeof(datasize) + sizeof(type) + sizeof(direct_communication) + 
        std::tuple_size<decltype(key)>::value +
        std::tuple_size<decltype(sequence)>::value +
        std::tuple_size<decltype(random_salt)>::value;

    auto static_rand_engine() -> std::mt19937&
    {
        static thread_local std::random_device rd;
        static thread_local std::mt19937 gen(rd());
        return gen;
    }

    void gen_random_salt()
    {
        std::uniform_int_distribution<unit_t> distrib(1, 0xFF);
        std::mt19937& gen = static_rand_engine();
        std::generate(random_salt.begin(), random_salt.end(), [&] { return distrib(gen); });
    }

    void gen_sequence()
    {
        std::uniform_int_distribution<unit_t> distrib(1, 0xFF);
        std::mt19937& gen = static_rand_engine();
        std::generate(sequence.begin(), sequence.end(), [&] { return distrib(gen); });
    }

    void gen()
    {
        gen_random_salt();
        gen_sequence();
    }

    void set_direct_communication(bool enable_direct_communication)
    {
        direct_communication = enable_direct_communication ? 1 : 0;
    }

    bool is_direct_communication_enabled()
    {
        return direct_communication == 1;
    }

    inline
    auto print() -> std::string
    {
        std::stringstream ss;
        ss << (*this);
        return ss.str();
    }

    bool empty()
    {
        for (unit_t c : key)
            if (c != 0)
                return false;

        for (unit_t c : sequence)
            if (c != 0)
                return false;
        return true;
    }

    void parse(unit_t *pos)
    {
        // |type|
        std::memcpy(std::addressof(type), pos, sizeof(type));
        pos += sizeof(type);

        // |direct_communication|
        std::memcpy(std::addressof(direct_communication), pos, sizeof(direct_communication));
        pos += sizeof(direct_communication);

        // |key|
        std::memcpy(key.data(), pos, key.size());
        pos += key.size();

        // |sequence|
        std::memcpy(sequence.data(), pos, sequence.size());
        pos += sequence.size();

        // |random_salt|
        std::memcpy(random_salt.data(), pos, random_salt.size());
        pos += random_salt.size();

        // |datasize|
        std::memcpy(std::addressof(datasize), pos, sizeof(datasize));
        datasize = ntoh(datasize);
    }

    auto dump(unit_t *pos) -> unit_t*
    {
        // |type|
        std::memcpy(pos, std::addressof(type), sizeof(type));
        pos += sizeof(type);

        // |direct_communication|
        std::memcpy(pos, std::addressof(direct_communication), sizeof(direct_communication));
        pos += sizeof(direct_communication);

        // |key|
        std::memcpy(pos, key.data(), key.size());
        pos += key.size();

        // |sequence|
        std::memcpy(pos, sequence.data(), sequence.size());
        pos += sequence.size();

        // |random_salt|
        std::memcpy(pos, random_salt.data(), random_salt.size());
        pos += random_salt.size();

        // |datasize|
        decltype(datasize) datasize_copy = hton(datasize);
        std::memcpy(pos, std::addressof(datasize_copy), sizeof(datasize_copy));
        return pos + sizeof(datasize_copy);
    }

    bool is_trigger() { return random_salt.back() == 0; } // change
};

auto packet_header_key_hash(packet_header const& k) -> std::size_t
{
    std::size_t seed = 0x1b873593;
    hash::range(seed, k.key.begin(), k.key.end());
//    hash::range(seed, k.random_salt.begin(), k.random_salt.end());
    return seed;
}

auto key_hash(key_t const& k) -> std::size_t
{
    std::size_t seed = 0x1b873593;
    hash::range(seed, k.begin(), k.end());
//    hash::range(seed, k.random_salt.begin(), k.random_salt.end());
    return seed;
}

bool key_compare(key_t const& key1, key_t const& key2) {
    return (std::tie(key1) == std::tie(key2));
};

bool packet_header_key_compare(packet_header const& key1, packet_header const& key2) {
    return (std::tie(key1.key) == std::tie(key2.key));
};

struct packet_header_key_hash_compare
{
    static
    auto hash (packet_header const& key) -> std::size_t {
        return packet_header_key_hash(key);
    }

    static
    bool equal (packet_header const& key1, packet_header const& key2) {
        return packet_header_key_compare(key1, key2);
    }
};

struct key_hash_compare
{
    static
    auto hash (key_t const& key) -> std::size_t {
        return key_hash(key);
    }

    static
    bool equal (key_t const& key1, key_t const& key2) {
        return key_compare(key1, key2);
    }
};

struct string_hash_compare {
    std::size_t hash(const std::string& key) const {
        return std::hash<std::string>()(key);
    }
    
    bool equal(const std::string& key1, const std::string& key2) const {
        return std::equal_to<std::string>()(key1, key2);
    }
};

auto packet_header_full_key_hash(packet_header const& k) -> std::size_t
{
    std::size_t seed = 0x1b873593;
    hash::range(seed, k.key.begin(), k.key.end());
    hash::range(seed, k.random_salt.begin(), k.random_salt.end());
    hash::range(seed, k.sequence.begin(), k.sequence.end());
    return seed;
};

bool packet_header_full_key_compare(packet_header const& key1, packet_header const& key2)
{
    return (std::tie(key1.key, key1.random_salt, key1.sequence) ==
            std::tie(key2.key, key2.random_salt, key2.sequence));
}

struct packet_header_full_key_hash_compare
{
    static
    auto hash (packet_header const& key) -> std::size_t {
        return packet_header_full_key_hash(key);
    }

    static
    bool equal (packet_header const& key1, packet_header const& key2) {
        return packet_header_full_key_compare(key1, key2);
    }
};

bool operator == (packet_header const& key1, packet_header const& key2) {
    return packet_header_full_key_compare(key1, key2);
}

template<typename ContainerType>
auto basic_container_stream (std::ostream &os, ContainerType const& key) -> std::ostream&
{
    for (key_t::value_type const& v: key)
        os << std::hex << static_cast<int>(v);
    return os;
}

auto operator <<(std::ostream &os, packet_header const& pd) -> std::ostream&
{
    os << "[t=" << pd.type << "|k=";
    basic_container_stream(os, pd.key);
    os << ",seq=";
    basic_container_stream(os, pd.sequence);
    os << ",salt=";
    basic_container_stream(os, pd.random_salt);
    os << "|d=" << std::dec << pd.datasize << "]";
    return os;
}

struct packet_data
{
    std::vector<unit_t> buf;
    //boost::asio::const_buffer buffers;

    void parse(std::uint32_t const& size, unit_t *pos)
    {
        buf.resize(size);
        std::memcpy(buf.data(), pos, size);
    }

    auto dump(unit_t *pos) -> unit_t*
    {
        std::memcpy(pos, buf.data(), buf.size());
        return pos + buf.size();
    }
};


struct packet
{
    packet_header header;
    packet_data data;

    auto serialize() -> std::shared_ptr<std::vector<unit_t>>
    {
        header.datasize = data.buf.size();
        auto r = std::make_shared<std::vector<unit_t>>(packet_header::bytesize + header.datasize);

        unit_t* pos = header.dump(r->data());
        data.dump(pos);

        return r;
    }

    bool empty() { return header.empty(); }
};

using packet_pointer = std::shared_ptr<packet>;

} // namespace pack

#endif // CPP_SERIALIZER_OBJECTPACK_HPP__
