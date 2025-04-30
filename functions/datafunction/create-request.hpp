#pragma once
#ifndef CREATE_REQUEST_HPP__
#define CREATE_REQUEST_HPP__

#include <slsfs.hpp>

namespace slsfsdf::client_request
{

auto create_read (slsfs::pack::key_t const& name,
                  std::uint32_t const position,
                  std::uint32_t const size)
    -> slsfs::pack::packet_pointer
{
    slsfs::jsre::operation_t const operation = slsfs::jsre::operation_t::read;
    slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
    ptr->header.gen();
    ptr->header.key = name;

    std::string buf (size, 'A');
    slsfs::jsre::request request {
        .type      = slsfs::jsre::type_t::file,
        .operation = operation,
        .position  = position,
        .size      = static_cast<std::uint32_t>(buf.size()),
    };
    request.to_network_format();
    ptr->data.buf.resize(sizeof (request));
    std::memcpy(ptr->data.buf.data(), &request, sizeof (request));

    return ptr;
}

template <typename ContainerType>
auto create_write (slsfs::pack::key_t const& name,
                   std::uint32_t const position,
                   ContainerType const buf)
    -> slsfs::pack::packet_pointer
{
    slsfs::jsre::operation_t const operation = slsfs::jsre::operation_t::write;
    slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();
    ptr->header.gen();
    ptr->header.key = name;

    slsfs::jsre::request request {
        .type      = slsfs::jsre::type_t::file,
        .operation = operation,
        .position  = position,
        .size      = static_cast<std::uint32_t>(buf.size()),
    };
    request.to_network_format();
    ptr->data.buf.resize(sizeof (request) + buf.size());
    std::memcpy(ptr->data.buf.data(), &request, sizeof (request));
    std::memcpy(ptr->data.buf.data() + sizeof (request), buf.data(), buf.size());

    return ptr;
}

} // namespace slsfsdf::client_request

#endif //CREATE_REQUEST_HPP__
