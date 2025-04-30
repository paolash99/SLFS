#pragma once

#ifndef CLIENT_CLIENTLIB_PACKET_CREATE_HPP__
#define CLIENT_CLIENTLIB_PACKET_CREATE_HPP__

#include "../uuid.hpp"
#include <string>

namespace slsfs::client::packet_create
{
    std::pair<std::string, std::string> split_path(const std::string &path)
    {
        // Remove trailing '/' if present
        std::string normalized_path = path;
        if (normalized_path == "/")
        {
            return {"", "/"};
        }
        if (!normalized_path.empty() && normalized_path.back() == '/')
        {
            normalized_path.pop_back();
        }

        // Find the last occurrence of '/'
        size_t last_separator = normalized_path.find_last_of("/");

        if (last_separator == std::string::npos)
        {
            return {"", path}; // No '/' found, return empty directory and full path as file name
        }
        if (last_separator == 0)
        {
            // The path is in the root directory
            return {"/", normalized_path.substr(1)};
        }

        // Normal case: path/file_name
        return {normalized_path.substr(0, last_separator + 1),
                normalized_path.substr(last_separator + 1)};
    }

    std::string get_parent_path(const std::string &path)
    {
        auto [parent_path, file_name] = split_path(path);
        return parent_path;
    }

    auto init(pack::key_t const &key)
    {
        pack::packet_pointer cptr = std::make_shared<pack::packet>();
        cptr->header.type = pack::msg_t::trigger;
        cptr->header.key = key;

        jsre::request r{
            .type = jsre::type_t::metadata,
            .operation = static_cast<jsre::operation_t>(jsre::meta_operation_t::init),
            .position = 0,
            .size = 0};
        r.to_network_format();

        cptr->data.buf.resize(sizeof(r));
        std::memcpy(cptr->data.buf.data(), &r, sizeof(r));

        cptr->header.gen();
        //BOOST_LOG_TRIVIAL(debug) << "creating init request " << cptr->header;
        return cptr;
    }

    auto init(std::string const &root_directory)
        -> pack::packet_pointer
    {
        return init(uuid::get_uuid(root_directory));
    }

    auto lock(pack::key_t const &key)
    {
        pack::packet_pointer cptr = std::make_shared<pack::packet>();
        cptr->header.type = pack::msg_t::trigger;
        cptr->header.key = key;

        jsre::request r{
            .type = jsre::type_t::metadata,
            .operation = static_cast<jsre::operation_t>(jsre::meta_operation_t::init),
            .position = 0,
            .size = 0};
        r.to_network_format();

        cptr->data.buf.resize(sizeof(r));
        std::memcpy(cptr->data.buf.data(), &r, sizeof(r));

        cptr->header.gen();
        //BOOST_LOG_TRIVIAL(debug) << "creating lock request " << cptr->header;
        return cptr;
    }

    auto lock(std::string const &path)
        -> pack::packet_pointer
    {
        return lock(uuid::get_uuid(path));
    }

    auto unlock(pack::key_t const &key)
    {
        pack::packet_pointer cptr = std::make_shared<pack::packet>();
        cptr->header.type = pack::msg_t::trigger;
        cptr->header.key = key;

        jsre::request r{
            .type = jsre::type_t::metadata,
            .operation = static_cast<jsre::operation_t>(jsre::meta_operation_t::init),
            .position = 0,
            .size = 0};
        r.to_network_format();

        cptr->data.buf.resize(sizeof(r));
        std::memcpy(cptr->data.buf.data(), &r, sizeof(r));

        cptr->header.gen();
        //BOOST_LOG_TRIVIAL(debug) << "creating lock request " << cptr->header;
        return cptr;
    }

    auto unlock(std::string const &path)
        -> pack::packet_pointer
    {
        return unlock(uuid::get_uuid(path));
    }

    auto mkdir(pack::key_t const &key, std::string const &dir_name, pack::key_t const &dir_key)
    {
        pack::packet_pointer cptr = std::make_shared<pack::packet>();

        jsre::meta::dirmeta dir_meta{
            .owner = 0,
            .permission = 040777};

        dir_meta.to_network_format();

        cptr->header.type = pack::msg_t::trigger;
        cptr->header.key = key;

        jsre::request r{
            .type = jsre::type_t::metadata,
            .operation = static_cast<jsre::operation_t>(jsre::meta_operation_t::mkdir),
            .position = 0,
            .size = sizeof(dir_meta),
            .file_name = {},
            .file_key = {}};

        std::memcpy(r.file_name.data(),
                    dir_name.data(), std::min(dir_name.size(), sizeof(r)));
        std::memcpy(r.file_key.data(),
                    dir_key.data(), std::min(dir_key.size(), sizeof(r)));

        r.to_network_format();

        cptr->data.buf.resize(sizeof(r) + sizeof(r));
        std::memcpy(cptr->data.buf.data(), &r, sizeof(r));
        std::memcpy(cptr->data.buf.data() + sizeof(r), &dir_meta, sizeof(dir_meta));

        cptr->header.gen();
        //BOOST_LOG_TRIVIAL(debug) << "creating mkdir request " << cptr->header;
        return cptr;
    }

    auto mkdir(std::string directory_path)
        -> pack::packet_pointer
    {

        if (!directory_path.empty() && directory_path.back() != '/')
        {
            directory_path += '/'; // Append '/' if it is not present
        }

        auto [parent_path, dir_name] = split_path(directory_path);
        return mkdir(uuid::get_uuid(parent_path), dir_name, uuid::get_uuid(directory_path));
    }

    auto delete_file(pack::key_t const &key, std::string const &file_name, pack::key_t const &file_key)
        -> pack::packet_pointer
    {
        pack::packet_pointer cptr = std::make_shared<pack::packet>();

        cptr->header.type = pack::msg_t::trigger;
        cptr->header.key = key;

        jsre::request r{
            .type = jsre::type_t::metadata,
            .operation = static_cast<jsre::operation_t>(jsre::meta_operation_t::deletefile),
            .position = 0,
            .size = 0,
            .file_name = {},
            .file_key = {}};

        std::memcpy(r.file_key.data(), file_key.data(), std::min(file_key.size(), sizeof(r)));
        std::memcpy(r.file_name.data(), file_name.data(), std::min(file_name.size(), sizeof(r)));

        r.to_network_format();

        cptr->data.buf.resize(sizeof(r));
        std::memcpy(cptr->data.buf.data(), &r, sizeof(r));

        cptr->header.gen();
        //BOOST_LOG_TRIVIAL(debug) << "creating delete file request " << cptr->header;

        return cptr;
    }

    auto delete_file(std::string const &file_path)
        -> pack::packet_pointer
    {
        auto [parent_path, file_name] = split_path(file_path);
        return delete_file(uuid::get_uuid(parent_path), file_name, uuid::get_uuid(file_path));
    }

    auto rename_file(pack::key_t const &key, pack::key_t const &file_key,
                     pack::key_t const &new_file_key, std::string const &file_name, pack::key_t const &file_parent_key, std::string const &new_file_name)
        -> pack::packet_pointer
    {
        pack::packet_pointer cptr = std::make_shared<pack::packet>();

        jsre::meta::rename_info meta{
            .new_file_key = {},
            .file_parent_key = {},
            .new_file_name = {}};

        std::memcpy(meta.new_file_key.data(),
                    new_file_key.data(), std::min(new_file_key.size(), sizeof(meta)));
        std::memcpy(meta.file_parent_key.data(),
                    file_parent_key.data(), std::min(file_parent_key.size(), sizeof(meta)));
        std::memcpy(meta.new_file_name.data(),
                    new_file_name.data(), std::min(new_file_name.size(), sizeof(meta)));

        cptr->header.type = pack::msg_t::trigger;
        cptr->header.key = key;

        jsre::request r{
            .type = jsre::type_t::metadata,
            .operation = static_cast<jsre::operation_t>(jsre::meta_operation_t::rename_sameproxy),
            .position = 0,
            .size = sizeof(meta),
            .file_name = {},
            .file_key = {}};

        std::memcpy(r.file_key.data(), file_key.data(), std::min(file_key.size(), sizeof(r)));
        std::memcpy(r.file_name.data(), file_name.data(), std::min(file_name.size(), sizeof(r)));

        r.to_network_format();

        cptr->data.buf.resize(sizeof(r) + sizeof(meta));
        std::memcpy(cptr->data.buf.data(), &r, sizeof(r));
        std::memcpy(cptr->data.buf.data() + sizeof(r), &meta, sizeof(meta));

        cptr->header.gen();
        //BOOST_LOG_TRIVIAL(debug) << "creating rename request " << cptr->header;

        return cptr;
    }

    auto rename_file(std::string const &file_path, std::string const &new_file_path)
        -> pack::packet_pointer
    {
        auto [new_parent_path, new_file_name] = split_path(new_file_path);
        auto [parent_path, file_name] = split_path(file_path);
        return rename_file(uuid::get_uuid(new_parent_path), uuid::get_uuid(file_path), uuid::get_uuid(new_file_path), file_name, uuid::get_uuid(parent_path), new_file_name);
    }

    auto add_file(pack::key_t const &key, std::string const &file_name)
        -> pack::packet_pointer
    {
        pack::packet_pointer cptr = std::make_shared<pack::packet>();

        jsre::meta::filemeta filemeta{
            .owner = 0,
            .permission = 040777};

        filemeta.to_network_format();

        cptr->header.type = pack::msg_t::trigger;
        cptr->header.key = key;

        jsre::request r{
            .type = jsre::type_t::metadata,
            .operation = static_cast<jsre::operation_t>(jsre::meta_operation_t::addfile),
            .position = 0,
            .size = sizeof(filemeta),
            .file_name = {}};
        r.to_network_format();

        std::memcpy(r.file_name.data(),
                    file_name.data(), std::min(file_name.size(), sizeof(r)));

        cptr->data.buf.resize(sizeof(r) + sizeof(filemeta));
        std::memcpy(cptr->data.buf.data(), &r, sizeof(r));
        std::memcpy(cptr->data.buf.data() + sizeof(r), &filemeta, sizeof(filemeta));

        cptr->header.gen();
        //BOOST_LOG_TRIVIAL(debug) << "creating add file request " << cptr->header;

        return cptr;
    }

    auto addfile(std::string const &directory, std::string const file_name)
        -> pack::packet_pointer
    {
        return add_file(uuid::get_uuid(directory), file_name);
    }

    auto add_file_v2(std::string const &file_path)
        -> pack::packet_pointer
    {
        auto [parent_path, file_name] = split_path(file_path);
        return add_file(uuid::get_uuid(parent_path), file_name);
    }

    auto open(pack::key_t const &key, std::string const &file_name, pack::key_t const &file_key)
        -> pack::packet_pointer
    {
        pack::packet_pointer cptr = std::make_shared<pack::packet>();

        cptr->header.type = pack::msg_t::trigger;
        cptr->header.key = key;

        jsre::request r{
            .type = jsre::type_t::metadata,
            .operation = static_cast<jsre::operation_t>(jsre::meta_operation_t::open),
            .position = 0,
            .size = 0,
            .file_name = {},
            .file_key = {}};

        std::memcpy(r.file_key.data(), file_key.data(), std::min(file_key.size(), sizeof(r)));
        std::memcpy(r.file_name.data(), file_name.data(), std::min(file_name.size(), sizeof(r)));

        r.to_network_format();

        cptr->data.buf.resize(sizeof(r));
        std::memcpy(cptr->data.buf.data(), &r, sizeof(r));

        cptr->header.gen();
        //BOOST_LOG_TRIVIAL(debug) << "creating open request " << cptr->header;

        return cptr;
    }

    auto open(std::string const &file_path)
        -> pack::packet_pointer
    {
        auto [parent_path, file_name] = split_path(file_path);
        return open(uuid::get_uuid(parent_path), file_name, uuid::get_uuid(file_path));
    }

    auto close(pack::key_t const &key, pack::key_t const &file_key)
        -> pack::packet_pointer
    {
        pack::packet_pointer cptr = std::make_shared<pack::packet>();

        cptr->header.type = pack::msg_t::trigger;
        cptr->header.key = key;

        jsre::request r{
            .type = jsre::type_t::metadata,
            .operation = static_cast<jsre::operation_t>(jsre::meta_operation_t::close),
            .position = 0,
            .size = 0,
            .file_key = {}};

        std::memcpy(r.file_key.data(), file_key.data(), std::min(file_key.size(), sizeof(r)));

        r.to_network_format();

        cptr->data.buf.resize(sizeof(r));
        std::memcpy(cptr->data.buf.data(), &r, sizeof(r));

        cptr->header.gen();
        //BOOST_LOG_TRIVIAL(debug) << "creating close file_meta request " << cptr->header;

        return cptr;
    }

    auto close(std::string const &file_path)
        -> pack::packet_pointer
    {
        auto [parent_path, file_name] = split_path(file_path);
        return close(uuid::get_uuid(parent_path), uuid::get_uuid(file_path));
    }

    auto ls(pack::key_t const &directory)
        -> pack::packet_pointer
    {
        pack::packet_pointer cptr = std::make_shared<pack::packet>();

        cptr->header.type = pack::msg_t::trigger;
        cptr->header.key = directory;

        jsre::request r{
            .type = jsre::type_t::metadata,
            .operation = static_cast<jsre::operation_t>(jsre::meta_operation_t::ls),
            .position = 0,
            .size = 0,
        };
        r.to_network_format();

        cptr->data.buf.resize(sizeof(r));
        std::memcpy(cptr->data.buf.data(), &r, sizeof(r));

        cptr->header.gen();
        //BOOST_LOG_TRIVIAL(debug) << "creating ls meta request " << cptr->header;
        return cptr;
    }

    auto ls(std::string directory)
        -> pack::packet_pointer
    {
        if (!directory.empty() && directory.back() != '/')
        {
            directory += '/'; // Append '/' if it is not present
        }
        return ls(uuid::get_uuid(directory));
    }

    auto get_stat(pack::key_t const &key, std::string const name, bool is_directory)
        -> pack::packet_pointer
    {
        pack::packet_pointer cptr = std::make_shared<pack::packet>();

        cptr->header.type = pack::msg_t::trigger;
        cptr->header.key = key;

        jsre::request r{
            .type = jsre::type_t::metadata,
            .operation = is_directory ? static_cast<jsre::operation_t>(jsre::meta_operation_t::getstatdir) : static_cast<jsre::operation_t>(jsre::meta_operation_t::getstatfile),
            .position = 0,
            .size = 0,
            .file_name = {}};
        r.to_network_format();

        std::memcpy(r.file_name.data(),
                    name.data(), std::min(name.size(), sizeof(r)));

        cptr->data.buf.resize(sizeof(r));
        std::memcpy(cptr->data.buf.data(), &r, sizeof(r));

        cptr->header.gen();
        //BOOST_LOG_TRIVIAL(debug) << "creating get stat request " << cptr->header;

        return cptr;
    }

    auto get_stat_file(std::string const &file_path)
        -> pack::packet_pointer
    {
        auto [parent_path, file_name] = split_path(file_path);
        return get_stat(uuid::get_uuid(parent_path), file_name, false);
    }

    auto get_stat_dir(std::string const &dir_path)
        -> pack::packet_pointer
    {
        auto [parent_path, dir_name] = split_path(dir_path);
        return get_stat(uuid::get_uuid(parent_path), dir_name, true);
    }

    template <typename BufContainer>
    auto write_v2(pack::key_t const &key, BufContainer const &buf, pack::key_t const &file_key, std::string const &file_name, std::uint32_t const position = 0)
        -> pack::packet_pointer
    {
        pack::packet_pointer ptr = std::make_shared<pack::packet>();
        ptr->header.gen();

        ptr->header.type = pack::msg_t::trigger;
        ptr->header.key = key;

        jsre::request r{
            .type = jsre::type_t::file,
            .operation = jsre::operation_t::write,
            .position = position,
            .size = static_cast<std::uint32_t>(buf.size()),
            .file_name = {},
            .file_key = {}};
        r.to_network_format();

        std::memcpy(r.file_name.data(),
                    file_name.data(), std::min(file_name.size(), sizeof(r)));
        std::memcpy(r.file_key.data(),
                    file_key.data(), std::min(file_key.size(), sizeof(r)));

        ptr->data.buf.resize(sizeof(r) + buf.size());
        std::memcpy(ptr->data.buf.data(), &r, sizeof(r));
        std::memcpy(ptr->data.buf.data() + sizeof(r), buf.data(), buf.size());

        //BOOST_LOG_TRIVIAL(debug) << "creating file write request " << ptr->header;
        return ptr;
    }

    // Old write implementation
    template <typename BufContainer>
    auto write(pack::key_t const &file_name, BufContainer const &buf, std::uint32_t const position = 0)
        -> pack::packet_pointer
    {
        pack::packet_pointer ptr = std::make_shared<pack::packet>();
        ptr->header.gen();

        ptr->header.type = pack::msg_t::trigger;
        ptr->header.key = file_name;

        jsre::request r{
            .type = jsre::type_t::file,
            .operation = jsre::operation_t::write,
            .position = position,
            .size = static_cast<std::uint32_t>(buf.size())};
        r.to_network_format();

        ptr->data.buf.resize(sizeof(r) + buf.size());
        std::memcpy(ptr->data.buf.data(), &r, sizeof(r));
        std::memcpy(ptr->data.buf.data() + sizeof(r), buf.data(), buf.size());

        return ptr;
    }

    template <typename BufContainer>
    auto write_v2(std::string const &file_path, BufContainer const &buf, std::uint32_t const position = 0)
        -> pack::packet_pointer
    {
        auto [parent_path, file_name] = split_path(file_path);
        return write_v2(uuid::get_uuid(parent_path), buf, uuid::get_uuid(file_path), file_name, position);
    }

    template <typename BufContainer>
    auto create_and_write_file_single_request(pack::key_t const &parent_key, BufContainer const &buf, pack::key_t const &file_key, std::string const &file_name)
        -> pack::packet_pointer
    {
        pack::packet_pointer ptr = std::make_shared<pack::packet>();
        ptr->header.gen();
        ptr->header.type = pack::msg_t::trigger;
        ptr->header.key = parent_key;

        jsre::meta::filemeta filemeta{
            .owner = 0,
            .permission = 040777};

        filemeta.to_network_format();

        jsre::request r{
            .type = jsre::type_t::metadata,
            .operation = static_cast<jsre::operation_t>(jsre::meta_operation_t::addfile_write),
            .position = 0,
            .size = static_cast<std::uint32_t>(sizeof(filemeta) + buf.size()),
            .file_name = {},
            .file_key = {}};

        std::memcpy(r.file_name.data(),
                    file_name.data(),
                    std::min(file_name.size(), sizeof(r)));

        std::memcpy(r.file_key.data(),
                    file_key.data(),
                    std::min(file_key.size(), sizeof(r)));

        r.to_network_format();

        ptr->data.buf.resize(sizeof(r) + sizeof(filemeta) + buf.size());

        std::memcpy(ptr->data.buf.data(), &r, sizeof(r));
        std::memcpy(ptr->data.buf.data() + sizeof(r), &filemeta, sizeof(filemeta));
        std::memcpy(ptr->data.buf.data() + sizeof(filemeta) + sizeof(r), buf.data(), buf.size());

        return ptr;
    }

    template <typename BufContainer>
    auto create_and_write_file_single_request(std::string const &file_path, BufContainer const &buf)
        -> pack::packet_pointer
    {
        auto [parent_path, file_name] = split_path(file_path);
        return create_and_write_file_single_request(uuid::get_uuid(parent_path), buf, uuid::get_uuid(file_path), file_name);
    }

    // old read
    auto read(pack::key_t const &key, std::uint32_t const size, std::uint32_t const position = 0)
        -> pack::packet_pointer
    {
        pack::packet_pointer ptr = std::make_shared<pack::packet>();

        ptr->header.type = pack::msg_t::trigger;
        ptr->header.key = key;

        jsre::request r{
            .type = jsre::type_t::file,
            .operation = jsre::operation_t::read,
            .position = position,
            .size = size};

        r.to_network_format();

        ptr->data.buf.resize(sizeof(r));
        std::memcpy(ptr->data.buf.data(), &r, sizeof(r));

        ptr->header.gen();
        //BOOST_LOG_TRIVIAL(debug) << "creating file old read request";
        return ptr;
    }

    auto read_v2(pack::key_t const &key, pack::key_t const &file_key, std::uint32_t const size, std::uint32_t const position = 0)
        -> pack::packet_pointer
    {
        pack::packet_pointer ptr = std::make_shared<pack::packet>();

        ptr->header.type = pack::msg_t::trigger;
        ptr->header.key = key;

        jsre::request r{
            .type = jsre::type_t::file,
            .operation = jsre::operation_t::read,
            .position = position,
            .size = size,
            .file_key = {}};

        std::memcpy(r.file_key.data(),
                    file_key.data(),
                    std::min(file_key.size(), sizeof(r.file_key)));

        r.to_network_format();

        ptr->data.buf.resize(sizeof(r));
        std::memcpy(ptr->data.buf.data(), &r, sizeof(r));

        ptr->header.gen();
        //BOOST_LOG_TRIVIAL(debug) << "creating file read request";
        return ptr;
    }

    auto read_v2(std::string const &file_path, std::uint32_t const size, std::uint32_t const position = 0)
        -> pack::packet_pointer
    {
        auto [parent_path, file_name] = split_path(file_path);
        return read_v2(uuid::get_uuid(parent_path), uuid::get_uuid(file_path), size, position);
    }

} // namespace client::packat_create

#endif // CLIENT_CLIENTLIB_PACKET_CREATE_HPP__
