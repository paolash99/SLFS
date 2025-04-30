#include "proxy/basic.hpp"
#include "emulation_node.hpp"
#include "proxy/json-replacement.hpp"
#include "proxy/serializer.hpp"

#define BOOST_NO_CXX11_SCOPED_ENUMS
#include <boost/filesystem.hpp>
#undef BOOST_NO_CXX11_SCOPED_ENUMS
#include <boost/asio.hpp>
#include <fmt/core.h>
#include <Poco/SHA2Engine.h>
#include <Poco/DigestStream.h>

#include <iostream>
#include <fstream>
#include <string>
#include <chrono>
#include <cmath>

using namespace std;

auto get_uuid(std::string const& buffer) -> slsfs::pack::key_t
{
    slsfs::pack::key_t k;
    Poco::SHA2Engine eng;
    Poco::DigestOutputStream outstr(eng);
    outstr << buffer;
    outstr.flush();

    Poco::DigestEngine::Digest const& digest = eng.digest();
    std::memcpy(k.data(), digest.data(), digest.size());
    return k;
}


class slsfs_emulation_node : public emulation_node
{
    public:
        slsfs_emulation_node() {};

        int read_request(string filename, double bytes, shared_ptr<promise<string>> result) {
            boost::asio::io_context io_context;
            slsfs::tcp::socket s(io_context);
            slsfs::tcp::resolver resolver(io_context);
            boost::asio::connect(s, resolver.resolve("192.168.0.135", "12001"));

            auto const start = chrono::high_resolution_clock::now();
            std::string const buf(bytes, 'A');

            slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();

            ptr->header.type = slsfs::pack::msg_t::trigger;
            ptr->header.key = get_uuid(filename);

            //std::string const payload = fmt::format("{{\"operation\": \"read\", \"filename\": \"/embl1.txt\", \"type\": \"file\", \"position\": {}, \"size\": {} }}", genpos(i), buf.size());
            slsfs::jsre::request r;
            r.type = slsfs::jsre::type_t::file;
            r.operation = slsfs::jsre::operation_t::read;
            r.position = 0;
            r.size = buf.size();

            r.to_network_format();

            ptr->data.buf.resize(sizeof (r));
            std::memcpy(ptr->data.buf.data(), &r, sizeof (r));

            //std::copy(payload.begin(), payload.end(), std::back_inserter(ptr->data.buf));

            ptr->header.gen();

            auto sendbuf = ptr->serialize();
            boost::asio::write(s, boost::asio::buffer(sendbuf->data(), sendbuf->size()));

            slsfs::pack::packet_pointer resp = std::make_shared<slsfs::pack::packet>();
            std::vector<slsfs::pack::unit_t> headerbuf(slsfs::pack::packet_header::bytesize);
            // std::cout << "(read_op) read head " << filename << std::endl;
            boost::asio::read(s, boost::asio::buffer(headerbuf.data(), headerbuf.size()));
            // std::cout << "(read_op) read head finish " << filename << std::endl;

            resp->header.parse(headerbuf.data());

            std::string data(resp->header.datasize, '\0');
            // std::cout << "(read_op) read body " << filename  << std::endl;
            boost::asio::read(s, boost::asio::buffer(data.data(), data.size()));
            // std::cout << "(read_op) read body finish " << filename  << std::endl;
            //BOOST_LOG_TRIVIAL(info) << data;

            auto const now = chrono::high_resolution_clock::now();
            auto relativetime = chrono::duration_cast<std::chrono::microseconds>(now - start).count();
            auto throughput = (bytes * 0.000001) / (relativetime * 0.000001);

            result->set_value("," + to_string(relativetime) + "," + to_string(lround(bytes)) + "," + "read" + "," + to_string(throughput));
            return 0;
        }

        int write_request(string filename, double bytes, shared_ptr<promise<string>> result) {
            boost::asio::io_context io_context;
            slsfs::tcp::socket s(io_context);
            slsfs::tcp::resolver resolver(io_context);
            boost::asio::connect(s, resolver.resolve("192.168.0.135", "12001"));

            auto const start = chrono::high_resolution_clock::now();
            std::string const buf(bytes, filename[rand() % filename.size()]);

            slsfs::pack::packet_pointer ptr = std::make_shared<slsfs::pack::packet>();

            ptr->header.type = slsfs::pack::msg_t::trigger;
            ptr->header.key = get_uuid(filename);

            slsfs::jsre::request r;
            r.type = slsfs::jsre::type_t::file;
            r.operation = slsfs::jsre::operation_t::write;
            r.position = 0;
            r.size = buf.size();

            r.to_network_format();

            ptr->data.buf.resize(sizeof (r) + buf.size());
            std::memcpy(ptr->data.buf.data(), &r, sizeof (r));
            std::memcpy(ptr->data.buf.data() + sizeof (r), buf.data(), buf.size());

            ptr->header.gen();
            auto net_buf = ptr->serialize();

            boost::asio::write(s, boost::asio::buffer(net_buf->data(), net_buf->size()));

            slsfs::pack::packet_pointer resp = std::make_shared<slsfs::pack::packet>();
            std::vector<slsfs::pack::unit_t> headerbuf(slsfs::pack::packet_header::bytesize);
            // std::cout << "(write_op) read head " << filename  << std::endl;
            boost::asio::read(s, boost::asio::buffer(headerbuf.data(), headerbuf.size()));
            // std::cout << "(write_op) read head finish " << filename  << std::endl;

            resp->header.parse(headerbuf.data());

            std::string data(resp->header.datasize, '\0');
            // std::cout << "(write_op) read body " << filename  << std::endl;
            boost::asio::read(s, boost::asio::buffer(data.data(), data.size()));
            // std::cout << "(write_op) read body finish " << filename  << std::endl;

            auto const now = chrono::high_resolution_clock::now();
            auto relativetime = chrono::duration_cast<std::chrono::microseconds>(now - start).count();
            auto throughput = (bytes * 0.000001) / (relativetime * 0.000001);

            result->set_value("," + to_string(relativetime) + "," + to_string(lround(bytes)) + "," + "write" + "," + to_string(throughput));
            return 0;
        }
};
