#pragma once

#ifndef SLSFS_STORAGE_SWIFTKV_HPP__
#define SLSFS_STORAGE_SWIFTKV_HPP__

#include "basetypes.hpp"
#include "serializer.hpp"
#include "storage.hpp"
#include "scope-exit.hpp"
#include "uuid.hpp"

#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>

#include <miniocpp/client.h>
// #include "/home/ubuntu/dep/minio-cpp/include/client.h"

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <future>


namespace slsfs::storage
{

namespace { using namespace std; }

class swiftkv : public interface
{
private:
    minio::s3::BaseUrl base_url = minio::s3::BaseUrl("stack.nerc.mghpcc.org:13808");
    minio::creds::StaticProvider provider = minio::creds::StaticProvider("994dde2d21f24b498455a14611d9dfbd", "3b3817d6435f4590b252bc99ba95c8d4");
    minio::s3::Client client = minio::s3::Client(base_url, &provider);
    string kv_store = "";


public:
    swiftkv(string kvstore_name)
    {
        // Check if kvstore exists
        bool exist = false;
        {
            minio::s3::BucketExistsArgs args;
            args.bucket = kvstore_name;

            minio::s3::BucketExistsResponse resp = client.BucketExists(args);
            if (!resp) {
                std::cout << "unable to do bucket existence check; " << resp.Error().String()
                        << std::endl;
                throw runtime_error("unable to check if kvstore exists");
            }

            exist = resp.exist;
        }
        if (!exist) {
            throw runtime_error("kvstore does not exist on server");
        }

        kv_store = kvstore_name;
    }

    void connect() override {}

    // key interface   [str] -> buf
    auto read_key (pack::key_t const& name, std::size_t partition, std::size_t location, std::size_t size) -> base::buf override
    {
        minio::s3::GetObjectArgs args;
        args.bucket = kv_store;
        args.object = uuid::encode_base64(name);

        promise<string> ret_val;

        args.datafunc = [&](minio::http::DataFunctionArgs args) -> bool {
                            ret_val.set_value(args.datachunk);
                            return true;
                        };

        minio::s3::GetObjectResponse resp = client.GetObject(args);

        if (resp) {
            return base::to_buf(ret_val.get_future().get());

        } else {
            return base::to_buf("KEYNOTFOUND");
        }
    }

    // TODO: Should this return a response, What if this fails?
    void write_key(pack::key_t const& name, std::size_t partition, base::buf const& buffer, std::size_t location, std::uint32_t version) override
    {
        std::stringstream stream;
        std::copy(buffer.begin(), buffer.end(), std::ostream_iterator<std::uint8_t>(stream));

        minio::s3::PutObjectArgs args(stream, buffer.size(), 0);
        args.bucket = kv_store;
        std::cout << uuid::encode_base64(name);
        args.object = uuid::encode_base64(name);

        minio::s3::PutObjectResponse resp = client.PutObject(args);
    }

    bool check_version_ok(pack::key_t const & name, std::size_t partition, std::uint32_t& version) override {return false;}

    // list interface  [str] -> buf
    auto get_list_key(pack::key_t const& name) -> base::buf override
    {
        minio::s3::ListObjectsArgs args;
        args.bucket = kv_store;

        // Call list objects.
        minio::s3::ListObjectsResult result = client.ListObjects(args);
        for (; result; result++) {
            minio::s3::Item item = *result;
            if (item) {
                std::cout << "Name: " << item.name << std::endl;
                std::cout << "Version ID: " << item.version_id << std::endl;
                std::cout << "ETag: " << item.etag << std::endl;
                std::cout << "Size: " << item.size << std::endl;
                std::cout << "Last Modified: " << item.last_modified << std::endl;
                std::cout << "Delete Marker: "
                            << minio::utils::BoolToString(item.is_delete_marker)
                            << std::endl;
                std::cout << "User Metadata: " << std::endl;
                for (auto& [key, value] : item.user_metadata) {
                    std::cout << "  " << key << ": " << value << std::endl;
                }
                std::cout << "Owner ID: " << item.owner_id << std::endl;
                std::cout << "Owner Name: " << item.owner_name << std::endl;
                std::cout << "Storage Class: " << item.storage_class << std::endl;
                std::cout << "Is Latest: " << minio::utils::BoolToString(item.is_latest)
                            << std::endl;
                std::cout << "Is Prefix: " << minio::utils::BoolToString(item.is_prefix)
                            << std::endl;
                std::cout << "---" << std::endl;
            } else {
            std::cout << "unable to list keys; " << item.Error().String()
                            << std::endl;
            break;
            }
        }
        return {};
    }
};

} // namespace slsfs::storage

#endif // SLSFS_STORAGE_SWIFTKV_HPP__
