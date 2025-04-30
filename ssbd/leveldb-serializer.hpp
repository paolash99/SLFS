#pragma once
#ifndef CPP_LEVELDB_SERIALIZER_OBJECTPACK_HPP__
#define CPP_LEVELDB_SERIALIZER_OBJECTPACK_HPP__

#include <arpa/inet.h>

#include <ios>
#include <iostream>
#include <vector>
#include <memory>
#include <cstring>
#include <array>
#include <bitset>
#include <tuple>
#include <algorithm>
#include <random>

namespace slsfs::leveldb_pack
{

namespace
{

template <typename Integer>
void hash_combine(std::size_t& seed, Integer value)
{
    seed ^= value + 0x9e3779b9 + (seed<<6) + (seed>>2);
}

template <typename It>
void hash_range(std::size_t& seed, It first, It last)
{
    for(; first != last; ++first)
        hash_combine(seed, *first);
}

} // namespace

using unit_t = unsigned char; // exp
static_assert(sizeof(unit_t) == 8/8);
using buffer_t = std::vector<unit_t>;
using versionint_t = std::uint32_t;

// key = [32] byte main key // sha256 bit
using key_t = std::array<unit_t, 256 / 8 / sizeof(unit_t)>;
enum class msg_t: std::uint16_t
{
    err = 0b00000000,
    ack = 0b00000001,
    get = 0b00000010,
    two_pc_prepare         = 0b00001000,
    two_pc_prepare_quick   = 0b00001001,
    two_pc_prepare_agree   = 0b00001010,
    two_pc_prepare_abort   = 0b00001011,
    two_pc_commit_execute  = 0b00001100,
    two_pc_commit_rollback = 0b00001101,
    two_pc_commit_ack      = 0b00001110,
    replication            = 0b00001111,
};

auto operator << (std::ostream &os, msg_t const& msg) -> std::ostream&
{
    switch (msg)
    {
    case msg_t::err:
        os << "ERR";
        break;
    case msg_t::ack:
        os << "ACK";
        break;
    case msg_t::get:
        os << "GET";
        break;
    case msg_t::two_pc_prepare:
        os << "2PPPR";
        break;
    case msg_t::two_pc_prepare_quick:
        os << "2PPRQ";
        break;
    case msg_t::two_pc_prepare_agree:
        os << "2PAGR";
        break;
    case msg_t::two_pc_prepare_abort:
        os << "2PABO";
        break;
    case msg_t::two_pc_commit_execute:
        os << "2CEXE";
        break;
    case msg_t::two_pc_commit_rollback:
        os << "2CROL";
        break;
    case msg_t::two_pc_commit_ack:
        os << "2CACK";
        break;
    case msg_t::replication:
        os << "REPLI";
        break;
    }

    //using under_t = std::underlying_type<msg_t>::type;
    //std::bitset<sizeof(under_t) * 8> m (static_cast<char>(msg));
    //os << m;
    return os;
}

bool is_merge_request(msg_t msg) {
    return static_cast<unit_t>(msg) & 0b00001000;
}

template<typename Integer>
auto hton(Integer i) -> Integer
{
    if constexpr (sizeof(Integer) == sizeof(decltype(htonl(i))))
        return htonl(i);
    else if constexpr (sizeof(Integer) == sizeof(decltype(htons(i))))
        return htons(i);
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
    if constexpr (sizeof(Integer) == sizeof(decltype(ntohl(i))))
        return ntohl(i);
    else if constexpr (sizeof(Integer) == sizeof(decltype(ntohs(i))))
        return ntohs(i);
    else if constexpr (sizeof(Integer) == 1)
        return i;
    else
    {
        static_assert("not supported conversion");
        return -1;
    }
}

struct packet_header;
auto operator << (std::ostream &os, packet_header const& pd) -> std::ostream&;

struct packet_header
{
    msg_t type;
    key_t uuid;
    std::uint32_t blockid;
    std::uint16_t position;
    std::uint32_t datasize;
    versionint_t version;
    std::array<unit_t, 4> salt;

    static constexpr int bytesize =
        sizeof(type) +
        std::tuple_size<decltype(uuid)>::value +
        sizeof(blockid) +
        sizeof(position) +
        sizeof(datasize) +
        sizeof(version) +
        sizeof(salt);

    auto as_string() -> std::string
    {
        std::string key;
        std::copy(uuid.begin(), uuid.end(), std::back_inserter(key));
        key += std::to_string(blockid);
        return key;
    }

    inline
    auto print() -> std::string
    {
        std::stringstream ss;
        ss << (*this);
        return ss.str();
    }

    void parse(unit_t *pos)
    {
        // |type|
        std::memcpy(std::addressof(type), pos, sizeof(type));
        pos += sizeof(type);

        // |uuid|
        std::memcpy(uuid.data(), pos, uuid.size());
        pos += uuid.size();

        // |blockid|
        std::memcpy(std::addressof(blockid), pos, sizeof(blockid));
        pos += sizeof(blockid);
        blockid = ntoh(blockid);

        // |position|
        std::memcpy(std::addressof(position), pos, sizeof(position));
        pos += sizeof(position);
        position = ntoh(position);

        // |datasize|
        std::memcpy(std::addressof(datasize), pos, sizeof(datasize));
        pos += sizeof(datasize);
        datasize = ntoh(datasize);

        // |version|
        std::memcpy(std::addressof(version), pos, sizeof(version));
        pos += sizeof(version);
        version = ntoh(version);

        // |salt|
        std::memcpy(salt.data(), pos, salt.size());
        pos += sizeof(salt);
    }

    auto dump(unit_t *pos) -> unit_t*
    {
        // |type|
        std::memcpy(pos, std::addressof(type), sizeof(type));
        pos += sizeof(type);

        // |uuid|
        std::memcpy(pos, uuid.data(), uuid.size());
        pos += uuid.size();

        // |blockid|
        decltype(blockid) blockid_copy = hton(blockid);
        std::memcpy(pos, std::addressof(blockid_copy), sizeof(blockid_copy));
        pos += sizeof(blockid_copy);

        // |position|
        decltype(position) position_copy = hton(position);
        std::memcpy(pos, std::addressof(position_copy), sizeof(position_copy));
        pos += sizeof(position_copy);

        // |datasize|
        decltype(datasize) datasize_copy = hton(datasize);
        std::memcpy(pos, std::addressof(datasize_copy), sizeof(datasize_copy));
        pos += sizeof(datasize);

        // |version|
        decltype(version) version_copy = hton(version);
        std::memcpy(pos, std::addressof(version_copy), sizeof(version_copy));
        pos += sizeof(version);

        std::memcpy(pos, salt.data(), salt.size());
        pos += salt.size();

        return pos;
    }

    auto static_rand_engine() -> std::mt19937&
    {
        static thread_local std::random_device rd;
        static thread_local std::mt19937 gen(rd());
        return gen;
    }

    void gen_salt()
    {
        std::uniform_int_distribution<unit_t> distrib(1, 0xFF);
        std::mt19937& gen = static_rand_engine();
        std::generate(salt.begin(), salt.end(), [&] { return distrib(gen); });
    }

    void gen() { gen_salt(); }
};


struct packet_header_key_hash
{
    auto operator() (packet_header const& k) const -> std::size_t
    {
        std::size_t seed = 0x1b873593 + k.blockid;
        hash_range(seed, k.uuid.begin(), k.uuid.end());
        hash_range(seed, k.salt.begin(), k.salt.end());
        return seed;
    }
};

struct packet_header_key_compare
{
    bool operator() (packet_header const& key1, packet_header const& key2) const
    {
        return (std::tie(key1.uuid, key1.blockid, key1.salt) ==
                std::tie(key2.uuid, key2.blockid, key2.salt));
    }
};

struct packet_header_key_hash_compare
{
    static
    auto hash (packet_header const& key) -> std::size_t {
        return packet_header_key_hash{}(key);
    }

    static
    bool equal (packet_header const& key1, packet_header const& key2) {
        return packet_header_key_compare{}(key1, key2);
    }
};

auto operator << (std::ostream &os, packet_header const& pd) -> std::ostream&
{
    os << "[t=" << pd.type << "|k=";
    for (key_t::value_type v: pd.uuid)
        os << std::hex << static_cast<int>(v);
    os << ",blkid=" << std::hex << pd.blockid;
    os << ",position=" << std::hex << pd.position;
    os << ",version=" << std::hex << pd.version;
    os << ",salt=";
    for (int i : pd.salt)
        os << std::hex << i;
    os << "|datasize=" << std::dec << pd.datasize << "]";
    return os;
}

struct packet_data
{
    buffer_t buf;

    auto as_string() -> std::string
    {
        std::string data;
        std::copy(buf.begin(), buf.end(), std::back_inserter(data));
        return data;
    }

    void parse(std::uint32_t const& size, unit_t *pos)
    {
        buf.resize(size);
        std::memcpy(buf.data(), pos, size);
    }

    template<typename CharType>
    void parse(std::uint32_t const& size, CharType *pos)
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

    packet() = default;
    packet(leveldb_pack::key_t const& name,
           leveldb_pack::msg_t const  type,
           versionint_t        const  version,
           std::size_t         const  partition,
           std::size_t         const  location,
           std::size_t         const  size)
    {
        header.gen();

        header.type     = type;
        header.version  = version;
        header.uuid     = name;
        header.blockid  = partition;
        header.position = location;
        header.datasize = size;
    }

    auto serialize() -> std::shared_ptr<buffer_t>
    {
        header.datasize = data.buf.size();
        auto r = std::make_shared<buffer_t>(packet_header::bytesize + header.datasize);

        unit_t* pos = header.dump(r->data());
        data.dump(pos);

        return r;
    }

    auto serialize_header() -> std::shared_ptr<buffer_t>
    {
        auto r = std::make_shared<buffer_t>(packet_header::bytesize);
        header.dump(r->data());
        return r;
    }
};

using packet_pointer = std::shared_ptr<packet>;

auto create_request(leveldb_pack::key_t const& name,
                    leveldb_pack::msg_t const  type,
                    versionint_t        const  version,
                    std::size_t         const  partition,
                    std::size_t         const  location,
                    std::size_t         const  size) -> packet_pointer {
    return std::make_shared<packet>(name, type, version, partition, location, size);
}

} // namespace slsfs::leveldb_pack

#endif // CPP_LEVELDB_SERIALIZER_OBJECTPACK_HPP__
