#pragma once
#ifndef SLSFS_HPP__
#define SLSFS_HPP__

#include "slsfs/uuid.hpp"
#include "slsfs/basetypes.hpp"
#include "slsfs/http-verb.hpp"
#include "slsfs/storage-cassandra.hpp"
#include "slsfs/base64-conv.hpp"
#include "slsfs/storage.hpp"
#include "slsfs/backend-ssbd.hpp"
#include "slsfs/switchstring.hpp"
#include "slsfs/debuglog.hpp"
#include "slsfs/serializer.hpp"
#include "slsfs/json-replacement.hpp"
#include "slsfs/socket-writer.hpp"

#include <kafka/KafkaConsumer.h>
#include <kafka/KafkaProducer.h>

#include <boost/asio.hpp>

#include <algorithm>
#include <memory>
#include <thread>

namespace slsfs
{

using off_t = std::uint64_t;

namespace v1
{

void send_kafka(std::string const& uuid, base::json const& data)
{
    slsfs::log::log("send_kafka start");

    kafka::Properties props ({
        {"bootstrap.servers",  "zion01:9092"},
        {"enable.idempotence", "true"},
    });

    kafka::clients::KafkaProducer producer(props);
    producer.setLogLevel(0);

    auto line = std::make_shared<std::string>(base::encode_kafkajson(data));
    auto record = kafka::clients::producer::ProducerRecord(uuid,
                                                           kafka::NullKey,
                                                           kafka::Value(line->c_str(), line->size()));

    slsfs::log::log("send_kafka producer.send()");
    producer.send(
        record,
        // The delivery report handler
        // Note: Here we capture the shared_pointer of `line`,
        //       which holds the content for `record.value()`.
        //       It makes sure the memory block is valid until the lambda finishes.
        [line](kafka::clients::producer::RecordMetadata const & metadata, kafka::Error const & error) {
            if (!error)
                std::cerr << "% Message delivered: " << metadata.toString() << "\n";
            else
                std::cerr << "% Message delivery failed: " << error.message() << "\n";
        });
    slsfs::log::log("send_kafka end");
}

auto listen_kafka(std::string const& channel) -> base::json
{
    slsfs::log::log("listen_kafka start");

    kafka::Properties props ({
        {"bootstrap.servers",  "zion01:9092"},
        {"enable.auto.commit", "true"}
    });

    slsfs::log::log("listen_kafka start subscribe channel");
    kafka::clients::KafkaConsumer consumer(props);
    consumer.subscribe({channel});
    consumer.setLogLevel(0);
    std::cerr << "listen on " << channel << "\n";

    slsfs::log::log("listen_kafka start poll()");
    std::vector<kafka::clients::consumer::ConsumerRecord> records = consumer.poll(std::chrono::milliseconds(10000));

    //assert(records.size() == 1);

    for (kafka::clients::consumer::ConsumerRecord const& record: records)
    {
        if (record.value().size() == 0)
            return "";

        if (!record.error())
        {
            std::cerr << "% Got a new message..." << std::endl;
            std::cerr << "    Topic    : " << record.topic() << std::endl;
            std::cerr << "    Partition: " << record.partition() << std::endl;
            std::cerr << "    Offset   : " << record.offset() << std::endl;
            std::cerr << "    Timestamp: " << record.timestamp().toString() << std::endl;
            std::cerr << "    Headers  : " << kafka::toString(record.headers()) << std::endl;
            std::cerr << "    Key   [" << record.key().toString() << "]" << std::endl;
            std::cerr << "    Value [" << record.value().toString() << "]" << std::endl;
            slsfs::log::log("listen_kafka end");
            return base::decode_kafkajson(record.value().toString());
        }
        else
            std::cerr << record.toString() << std::endl;

    }
    return "";
}

int create(char const * filename)
{
    slsfs::log::log("create start");

    std::string const uuid = filename;// uuid::gen_uuid(filename);

    using namespace std::literals;
    base::json triggerdata;
    std::string const triggername = "trigger-"s + uuid;
    triggerdata["name"] = triggername;

    base::json k;
    k["key"] = "feed";
    k["value"] = "/whisk.system/messaging/kafkaFeed";

    triggerdata["annotations"] = base::json::array();
    triggerdata["annotations"].push_back(k);

    std::string const triggerurl = "https://zion01/api/v1/namespaces/_/triggers/"s + triggername + "?overwrite=false";

    slsfs::log::log("create put trigger");
    httpdo::put(triggerurl, triggerdata.dump());

    std::string const providerurl="https://zion01/api/v1/namespaces/whisk.system/actions/messaging/kafkaFeed?blocking=true&result=false";
    base::json providerjson;

    providerjson["authKey"] = "789c46b1-71f6-4ed5-8c54-816aa4f8c502:abczO3xZCLrMN6v2BKK1dXYFpXlPkccOFqm12CdAsMgRU4VrNZ9lyGVCGuMDGIwP";
    providerjson["brokers"] = base::json::array();
    providerjson["brokers"].push_back("zion01:9092");
    providerjson["isJSONData"] = false;
    providerjson["lifecycleEvent"] = "CREATE";
    providerjson["topic"] = uuid;
    using namespace std::literals;
    providerjson["triggerName"] = "/_/"s + triggername;

    slsfs::log::log("create post provider");
    httpdo::post(providerurl, providerjson.dump());
    slsfs::log::log("create post provider end");

    std::cerr << providerjson << "post data \n";

    base::json ruledata;
    std::string const rulename = "kaf2df-" + uuid;
    ruledata["name"] = rulename;
    ruledata["status"] = "";
    ruledata["trigger"] = "/whisk.system/trigger-" + uuid;
    ruledata["action"] = "/whisk.system/slsfs-datafunction";

    //"https://zion01/api/v1/namespaces/_/rules/kaf2cpp?overwrite=false";
    std::string const ruleurl = "https://zion01/api/v1/namespaces/_/rules/"s + rulename + "?overwrite=false";

    slsfs::log::log("create put rule");
    httpdo::put(ruleurl, ruledata.dump());

    slsfs::log::log("create end");
    return 0;
}

} // namespace v1

namespace v2
{

namespace
{

using boost::asio::ip::tcp;

}

void send_kafka(pack::packet_pointer payload)
{
    slsfs::log::log("send_kafka start");
    return;

    boost::asio::io_context io_context;
    tcp::socket s(io_context);
    tcp::resolver resolver(io_context);
    boost::asio::connect(s, resolver.resolve("zion01", "12000"));

    auto buf = payload->serialize();

    slsfs::log::log("send_kafka producer.send()");
    boost::asio::write(s, boost::asio::buffer(buf->data(), buf->size()));
    slsfs::log::log("send_kafka write end");

    pack::packet_pointer resp = std::make_shared<pack::packet>();
    std::vector<pack::unit_t> headerbuf(pack::packet_header::bytesize);
    boost::asio::read(s, boost::asio::buffer(headerbuf.data(), headerbuf.size()));

    resp->header.parse(headerbuf.data());
    slsfs::log::log("send_kafka write confirmed");
}

auto listen_kafka(pack::packet_pointer response) -> base::json
{
    slsfs::log::log("listen_kafka start");
    return {};

    boost::asio::io_context io_context;
    tcp::socket s(io_context);
    tcp::resolver resolver(io_context);
    boost::asio::connect(s, resolver.resolve("zion01", "12000"));

    response->header.type = pack::msg_t::get;
    auto buf = response->serialize();
    boost::asio::write(s, boost::asio::buffer(buf->data(), buf->size()));

    // read
    pack::packet_pointer resp = std::make_shared<pack::packet>();
    std::vector<pack::unit_t> headerbuf(pack::packet_header::bytesize);

    boost::asio::read(s, boost::asio::buffer(headerbuf.data(), headerbuf.size()));
    resp->header.parse(headerbuf.data());

    std::vector<pack::unit_t> bodybuf(resp->header.datasize);
    boost::asio::read(s, boost::asio::buffer(bodybuf.data(), bodybuf.size()));

    resp->data.parse(resp->header.datasize, bodybuf.data());

    std::string v (resp->data.buf.begin(), resp->data.buf.end());
    base::json vj = base::json::parse(v);
    slsfs::log::log("listen_kafka get response resp: " + v);
    return vj;
}

int create(char const * filename)
{
    return 0;
//    slsfs::log::log("create start");
//
//    auto ptr = std::make_shared<pack::packet>();
//    std::string const url="http://zion01:2016/api/v1/namespaces/_/actions/slsfs-datafunction?blocking=false&result=false";
//    std::copy(url.begin(), url.end(), std::back_inserter(ptr->data.buf));
//
//    boost::asio::io_context io_context;
//    tcp::socket s(io_context);
//    tcp::resolver resolver(io_context);
//    boost::asio::connect(s, resolver.resolve("zion01", "12000"));
//
//    // request
//    ptr->header.type = pack::msg_t::call_register;
//    ptr->header.buf = uuid::get_uuid(filename);
//
//    auto buf = ptr->serialize();
//
//    boost::asio::write(s, boost::asio::buffer(buf->data(), buf->size()));
//
//    slsfs::log::log("create end");
//    return 0;
}

auto write(char const * filename, char const *data, std::size_t size, off_t off, /*struct fuse_file_info*/ void* info)
    -> std::size_t
{
    slsfs::log::log("write start");

    pack::packet_pointer request = std::make_shared<pack::packet>();
    pack::key_t const uuid = uuid::get_uuid(filename);
    request->header.type = pack::msg_t::put;
    request->header.key = uuid;
    request->header.gen_sequence();

    pack::packet_pointer response = std::make_shared<pack::packet>();
    response->header.key = request->header.key;
    response->header.type = pack::msg_t::get;
    response->header.gen_random_salt();

    std::stringstream ss;
    ss << response->header;
    slsfs::log::log(ss.str());

    base::json jsondata;
    jsondata["filename"] = filename;
    jsondata["data"] = data;
    jsondata["size"] = size;
    jsondata["offset"] = off;
    jsondata["type"] = "file";
    jsondata["operation"] = "write";
    jsondata["returnchannel"] = base::encode(response->header.random_salt);

    std::string v = jsondata.dump();
    request->data.buf = std::vector<pack::unit_t>(v.begin(), v.end());

    slsfs::log::log("write send_kafka");
    send_kafka(request);

    slsfs::log::log("write listen_kafka");
    base::json const ret = listen_kafka(response);
    slsfs::log::log("write end");
    return 0;
}

auto read(char const * filename, char *data, std::size_t size, off_t off, /*struct fuse_file_info*/ void* info)
    -> std::size_t
{
    slsfs::log::log("read start");
    pack::packet_pointer request = std::make_shared<pack::packet>();

    pack::key_t const uuid = uuid::get_uuid(filename);
    request->header.type = pack::msg_t::put;
    request->header.key = uuid;
    request->header.gen_sequence();

    pack::packet_pointer response = std::make_shared<pack::packet>();
    response->header = request->header;
    response->header.gen_random_salt();

    base::json jsondata;
    jsondata["filename"] = filename;
    jsondata["size"] = size;
    jsondata["offset"] = off;
    jsondata["type"] = "file";
    jsondata["operation"] = "read";
    jsondata["returnchannel"] = base::encode(response->header.random_salt);

    std::string v = jsondata.dump();
    request->data.buf = std::vector<pack::unit_t>(v.begin(), v.end());

    slsfs::log::log("read send_kafka");
    send_kafka(request);

    slsfs::log::log("read listen_kafka");
    base::json const ret = listen_kafka(response);
    std::string const read_data = ret["data"].get<std::string>();

    std::size_t readsize = std::min(size, read_data.size());
    std::copy_n(read_data.begin(), readsize, data);

    slsfs::log::log("read end");
    return readsize;
}

} // namespace v2

namespace v3
{

namespace
{

using boost::asio::ip::tcp;

}

auto write(char const * filename,
           char const *data, std::size_t size,
           off_t off, /*struct fuse_file_info*/ void* info,
           tcp::socket * connection = nullptr)
    -> std::size_t
{
    boost::asio::io_context io_context;
    tcp::socket tmpsocket(io_context);

    if (connection == nullptr)
    {
        connection = std::addressof(tmpsocket);
        tcp::resolver resolver(io_context);
        boost::asio::connect(*connection, resolver.resolve("ow-ctrl", "12000"));
    }

    pack::packet_pointer request = std::make_shared<pack::packet>();
    pack::key_t const uuid = uuid::get_uuid(filename);
    request->header.type = pack::msg_t::trigger;
    request->header.key = uuid;
    request->header.gen_sequence();

    //char const * filename, char const *data, std::size_t size, off_t off,
    std::string const payload =
        fmt::format("{{ \"operation\": \"write\", \"filename\": \"{}\", \"type\": \"file\", \"position\": {}, \"size\": {}, \"data\": \"{}\" }}",
                    filename, off, size, data);
//    std::copy(payload.begin(), payload.end(), std::back_inserter(ptr->data.buf));
    return 0;
}

auto read(char const * filename,
          char *data, std::size_t size,
          off_t off, /*struct fuse_file_info*/ void* info,
          tcp::socket * connection = nullptr)
    -> std::size_t
{
    return 0;
}

void send_kafka(pack::packet_pointer payload)
{
}

auto listen_kafka(pack::packet_pointer response) -> base::json
{
    return {};
}

int create(char const * filename)
{
    return 0;
}


} // namespace v3

using namespace v3;

} // namespace slsfs

#endif // SLSFS_HPP__
