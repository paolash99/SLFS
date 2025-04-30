#pragma once
#ifndef JSON_REPLACEMENT_HPP__
#define JSON_REPLACEMENT_HPP__

#include "serializer.hpp"

namespace slsfs::jsre
{

namespace meta
{

constexpr int filemeta_size = 64;

struct stats
{
    std::uint32_t file_count;
    std::array<slsfs::pack::unit_t,
               filemeta_size - sizeof(file_count)> reserved;

    void to_network_format() {
        file_count = slsfs::pack::hton(file_count);
    }

    void to_host_format() {
        file_count = slsfs::pack::ntoh(file_count);
    }
};

struct filemeta
{
    std::uint16_t owner;
    std::uint16_t permission;
    std::uint16_t reserved;
    std::array<slsfs::pack::unit_t,
               filemeta_size - sizeof(owner) - sizeof(permission)> filename;

    void to_network_format()
    {
        owner      = slsfs::pack::hton(owner);
        permission = slsfs::pack::hton(permission);
        reserved   = slsfs::pack::hton(reserved);
    }

    void to_host_format()
    {
        owner      = slsfs::pack::ntoh(owner);
        permission = slsfs::pack::ntoh(permission);
        reserved   = slsfs::pack::ntoh(reserved);
    }
};

} // namespace meta

enum class type_t : std::int8_t
{
    file,
    metadata,
    wakeup,
    storagetest,
};

enum class operation_t : std::int8_t
{
    write,
    read,
};

enum class meta_operation_t : std::int8_t
{
    addfile,
    ls,
    mkdir,
};

using key_t = pack::key_t;

struct request
{
    type_t        type;
    operation_t   operation;
    std::uint32_t position;
    std::uint32_t size;

    void to_network_format()
    {
        type     = pack::hton<type_t>(type);
        operation= pack::hton<operation_t>(operation);
        position = pack::hton(position);
        size     = pack::hton(size);
    }

    void to_host_format()
    {
        type     = pack::ntoh<type_t>(type);
        operation= pack::ntoh<operation_t>(operation);
        position = pack::ntoh(position);
        size     = pack::ntoh(size);
    }
};

template<typename CharType>
struct request_parser
{
    pack::packet_pointer pack;
    CharType *refdata;
    request_parser(pack::packet_pointer p): pack {p}, refdata {p->data.buf.data()} {}

    auto copy_request() -> request
    {
        return request {
            .type      = type(),
            .operation = operation(),
            .position  = position(),
            .size      = size(),
        };
    }

    auto uuid() const -> key_t const & {
        return pack->header.key;
    }

    auto create_uuid() const -> key_t {
        return pack->header.key;
    }

    auto uuid_shared() const -> std::shared_ptr<key_t>
    {
        auto k = std::make_shared<key_t>();
        *k = pack->header.key;
        return k;
    }

    auto type() const -> type_t
    {
        std::underlying_type_t<type_t> t;
        std::memcpy(&t, refdata + offsetof(request, type), sizeof(t));
        return static_cast<type_t>(pack::ntoh(t));
    }

    auto operation() const -> operation_t
    {
        std::underlying_type_t<operation_t> op;
        std::memcpy(&op, refdata + offsetof(request, operation), sizeof(op));
        return static_cast<operation_t>(pack::ntoh(op));
    }

    auto meta_operation() const -> meta_operation_t {
        return static_cast<meta_operation_t>(operation());
    }

    auto position() const -> std::uint32_t
    {
        std::uint32_t pos;
        std::memcpy(&pos, refdata + offsetof(request, position), sizeof(pos));
        return pack::ntoh(pos);
    }

    auto size() const -> std::uint32_t
    {
        std::uint32_t s;
        std::memcpy(&s, refdata + offsetof(request, size), sizeof(s));
        return pack::ntoh(s);
    }

    auto data() const -> const CharType* {
        return refdata + sizeof(request);
    }

    auto data() -> CharType* {
        return refdata + sizeof(request);
    }
};

} // namespace jsre

#endif // JSON_REPLACEMENT_HPP__
