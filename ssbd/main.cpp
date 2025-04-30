#include "basic.hpp"
#include "leveldb-serializer.hpp"
#include "rawblocks.hpp"
#include "socket-writer.hpp"
#include "persistent-log.hpp"

#include <boost/program_options.hpp>
#include <boost/log/trivial.hpp>
#include <boost/asio.hpp>
#include <boost/signals2.hpp>

#include <oneapi/tbb/concurrent_unordered_map.h>
#include <oneapi/tbb/concurrent_queue.h>

#include <leveldb/db.h>
#include <leveldb/cache.h>

#include <algorithm>
#include <iostream>
#include <memory>
#include <array>
#include <list>
#include <thread>
#include <vector>

namespace ssbd
{

class tcp_connection : public std::enable_shared_from_this<tcp_connection>
{
    net::io_context& io_context_;
    tcp::socket      socket_;

    slsfs::socket_writer::socket_writer<slsfs::leveldb_pack::packet, std::vector<slsfs::leveldb_pack::unit_t>> writer_;
    leveldb::DB&    db_;
    persistent_log& db_log_;
    std::chrono::steady_clock::time_point start_read_header_timestamp_ = std::chrono::steady_clock::now();

public:
    using pointer = std::shared_ptr<tcp_connection>;

    tcp_connection(net::io_context& io, tcp::socket socket, leveldb::DB& db, persistent_log& db_log):
        io_context_{io},
        socket_{std::move(socket)},
        writer_{io, socket_},
        db_{db},
        db_log_{db_log} {}

    void start_read_header()
    {
//        BOOST_LOG_TRIVIAL(trace) << "start_read_header. delta=" << std::chrono::steady_clock::now() - start_read_header_timestamp_;
        start_read_header_timestamp_ = std::chrono::steady_clock::now();
        auto read_buf = std::make_shared<std::array<slsfs::leveldb_pack::unit_t, slsfs::leveldb_pack::packet_header::bytesize>>();
        net::async_read(
            socket_,
            net::buffer(read_buf->data(), read_buf->size()),
            [self=shared_from_this(), read_buf] (boost::system::error_code ec, std::size_t /*length*/) {
                if (ec)
                    BOOST_LOG_TRIVIAL(error) << "start_read_header err: " << ec.message();
                else
                {
                    slsfs::leveldb_pack::packet_pointer pack = std::make_shared<slsfs::leveldb_pack::packet>();
                    pack->header.parse(read_buf->data());
                    //BOOST_LOG_TRIVIAL(trace) << "start_read_header start with header: " << pack->header;

                    slsfs::leveldb_pack::packet_pointer resp2 = std::make_shared<slsfs::leveldb_pack::packet>();
                    switch (pack->header.type)
                    {
                    case slsfs::leveldb_pack::msg_t::two_pc_prepare:
                        self->start_two_pc_prepare(pack);
                        break;

                    case slsfs::leveldb_pack::msg_t::two_pc_prepare_quick:
                        self->start_two_pc_prepare_quick(pack);
                        break;

                    case slsfs::leveldb_pack::msg_t::two_pc_commit_execute:
                        self->start_two_pc_commit_execute(pack);
                        break;

                    case slsfs::leveldb_pack::msg_t::two_pc_commit_rollback:
                        self->start_two_pc_commit_rollback(pack);
                        break;

                    case slsfs::leveldb_pack::msg_t::replication:
                        self->start_replication(pack);
                        break;

                    case slsfs::leveldb_pack::msg_t::get:
                        self->start_db_read(pack);
                        break;

                    case slsfs::leveldb_pack::msg_t::err:
                    case slsfs::leveldb_pack::msg_t::ack:
                    case slsfs::leveldb_pack::msg_t::two_pc_commit_ack:
                    case slsfs::leveldb_pack::msg_t::two_pc_prepare_agree:
                    case slsfs::leveldb_pack::msg_t::two_pc_prepare_abort:
                    {
                        BOOST_LOG_TRIVIAL(error) << "server should not get (" << pack->header.type << "). " << pack->header;
                        slsfs::leveldb_pack::packet_pointer resp = std::make_shared<slsfs::leveldb_pack::packet>();
                        resp->header = pack->header;
                        resp->header.type = slsfs::leveldb_pack::msg_t::err;
                        self->start_write_socket(resp);
                        self->start_read_header();
                        break;
                    }
                    }
                }
            });
    }

    void start_two_pc_prepare(slsfs::leveldb_pack::packet_pointer pack)
    {
        BOOST_LOG_TRIVIAL(trace) << "start_two_pc_prepare " << pack->header;
        auto read_buf = std::make_shared<std::string>(pack->header.datasize, 0);
        net::async_read(
            socket_,
            net::buffer(read_buf->data(), read_buf->size()),
            [self=shared_from_this(), read_buf, pack] (boost::system::error_code ec, std::size_t length) {
                if (ec)
                {
                    BOOST_LOG_TRIVIAL(error) << "start_two_pc_prepare: " << ec.message();
                    return;
                }

                BOOST_LOG_TRIVIAL(trace) << "start_two_pc_prepare process packet: " << pack->header;

                pack->data.parse(length, read_buf->data());

                slsfs::leveldb_pack::packet_pointer resp = std::make_shared<slsfs::leveldb_pack::packet>();
                resp->header = pack->header;

                std::string const key = pack->header.as_string();
                slsfs::leveldb_pack::rawblocks rb;

                resp->header.type = slsfs::leveldb_pack::msg_t::two_pc_prepare_agree;

                if (not rb.bind(self->db_, key).ok())
                {
                    std::string empty;
                    self->db_.Put(leveldb::WriteOptions(), key, empty);
                    self->db_log_.put_committed_version(key, 0);
                    self->db_log_.put_pending_prepare(key, "", 0);
                }

                if (rb.bind(self->db_, key).ok())
                {
                    BOOST_LOG_TRIVIAL(debug) << "start_two_pc_prepare committed version: " << self->db_log_.get_committed_version(key);
                    BOOST_LOG_TRIVIAL(debug) << "start_two_pc_prepare pending version:   " << self->db_log_.get_pending_prepare_version(key);
                    BOOST_LOG_TRIVIAL(debug) << "req: " << pack->header.version;

                    if (self->db_log_.have_pending_log(key))
                    {
                        // failed
                        resp->header.type = slsfs::leveldb_pack::msg_t::two_pc_prepare_abort;
                    }
                    else
                    {
                        // OK
                        //BOOST_LOG_TRIVIAL(debug) << "request buf: " << *read_buf;
                        std::string old_block (std::move(rb.buf_)); // move the unused data in .buf_ to here
                        old_block.resize(
                            std::max<std::uint32_t>(pack->header.position + pack->header.datasize,
                                                    old_block.size())); // make sure all buffer can write to old_block

                        std::copy(read_buf->begin(), read_buf->end(),
                                  std::next(old_block.begin(), pack->header.position));

                        //BOOST_LOG_TRIVIAL(trace) << "final block: " << old_block;

                        self->db_log_.put_pending_prepare(key, old_block, pack->header.version);
                    }
                    pack->header.version = self->db_log_.get_committed_version(key);
                }

                BOOST_LOG_TRIVIAL(trace) << "start_two_pc_prepare return packet: " << pack->header;
                self->start_write_socket(resp);
                self->start_read_header();
            });
    }

    void start_two_pc_prepare_quick(slsfs::leveldb_pack::packet_pointer pack)
    {
        BOOST_LOG_TRIVIAL(trace) << "start_two_pc_prepare_quick " << pack->header;
        auto read_buf = std::make_shared<std::string>(pack->header.datasize, 0);
        net::async_read(
            socket_,
            net::buffer(read_buf->data(), read_buf->size()),
            [self=shared_from_this(), read_buf, pack] (boost::system::error_code ec, std::size_t length) {
                if (ec)
                {
                    BOOST_LOG_TRIVIAL(error) << "start_two_pc_prepare_quick: " << ec.message();
                    return;
                }
                self->start_read_header();
                BOOST_LOG_TRIVIAL(trace) << "start_two_pc_prepare_quick process packet: " << pack->header;

                pack->data.parse(length, read_buf->data());

                slsfs::leveldb_pack::packet_pointer resp = std::make_shared<slsfs::leveldb_pack::packet>();
                resp->header = pack->header;

                std::string const key = pack->header.as_string();
                slsfs::leveldb_pack::rawblocks rb;

                resp->header.type = slsfs::leveldb_pack::msg_t::two_pc_prepare_agree;

                if (not rb.bind(self->db_, key).ok())
                {
                    std::string empty;
                    self->db_.Put(leveldb::WriteOptions(), key, empty);
                    self->db_log_.put_committed_version(key, 0);
                    self->db_log_.put_pending_prepare(key, "", 0);
                }

                if (rb.bind(self->db_, key).ok())
                {
                    std::string old_block (std::move(rb.buf_)); // move the unused data in .buf_ to here
                    old_block.resize(
                        std::max<std::uint32_t>(pack->header.position + pack->header.datasize,
                                                old_block.size())); // make sure all buffer can write to old_block

                    std::copy(read_buf->begin(), read_buf->end(),
                              std::next(old_block.begin(), pack->header.position));

                    self->db_log_.put_pending_prepare(key, old_block, pack->header.version);
                    pack->header.version = self->db_log_.get_committed_version(key);
                }

                BOOST_LOG_TRIVIAL(trace) << "start_two_pc_prepare_quick return packet: " << pack->header;
                self->start_write_socket(resp);
            });
    }

    void start_two_pc_commit_execute(slsfs::leveldb_pack::packet_pointer pack)
    {
        BOOST_LOG_TRIVIAL(trace) << "start_two_pc_commit_execute " << pack->header;
        auto read_buf = std::make_shared<std::string>(pack->header.datasize, 0);
        net::async_read(
            socket_,
            net::buffer(read_buf->data(), read_buf->size()),
            [self=shared_from_this(), read_buf, pack] (boost::system::error_code ec, std::size_t length) {
                if (ec)
                {
                    BOOST_LOG_TRIVIAL(error) << "error start_two_pc_commit_execute: " << ec.message();
                    return;
                }
                self->start_read_header();

                pack->data.parse(length, read_buf->data());
                std::string const key = pack->header.as_string();

                self->db_log_.commit_pending_prepare(key, self->db_);

                slsfs::leveldb_pack::packet_pointer resp = std::make_shared<slsfs::leveldb_pack::packet>();
                resp->header = pack->header;
                resp->header.type = slsfs::leveldb_pack::msg_t::two_pc_commit_ack;

                self->start_write_socket(resp);
            });
    }

    void start_two_pc_commit_rollback(slsfs::leveldb_pack::packet_pointer pack)
    {
        BOOST_LOG_TRIVIAL(trace) << "start_two_pc_commit_rollback " << pack->header;
        std::string const key = pack->header.as_string();

        db_log_.put_pending_prepare(key, "", 0);

        slsfs::leveldb_pack::packet_pointer resp = std::make_shared<slsfs::leveldb_pack::packet>();
        resp->header = pack->header;
        resp->header.type = slsfs::leveldb_pack::msg_t::two_pc_commit_ack;

        start_write_socket(resp);
        start_read_header();
    }

    // note: assumes the every replication are stored in different SSBD
    void start_replication(slsfs::leveldb_pack::packet_pointer pack)
    {
        //BOOST_LOG_TRIVIAL(trace) << "start_replication " << pack->header;
        auto read_buf = std::make_shared<std::string>(pack->header.datasize, 0);
        net::async_read(
            socket_,
            net::buffer(read_buf->data(), read_buf->size()),
            [self=shared_from_this(), read_buf, pack] (boost::system::error_code ec, std::size_t length) {
                if (ec)
                {
                    BOOST_LOG_TRIVIAL(error) << "start_replication: " << ec.message();
                    return;
                }

                self->start_read_header();
                pack->data.parse(length, read_buf->data());

                slsfs::leveldb_pack::packet_pointer resp = std::make_shared<slsfs::leveldb_pack::packet>();
                resp->header = pack->header;
                resp->header.type = slsfs::leveldb_pack::msg_t::ack;

                std::string const key = pack->header.as_string() + "repl";
                std::string old_block;
                self->db_.Get(leveldb::ReadOptions(), key, &old_block);

                old_block.resize(
                    std::max<std::uint32_t>(pack->header.position + pack->header.datasize,
                                            old_block.size())); // make sure all buffer can write to old_block

                std::copy(read_buf->begin(), read_buf->end(),
                          std::next(old_block.begin(), pack->header.position));

                self->db_.Put(leveldb::WriteOptions(), key, old_block);
                self->start_write_socket(resp);
            });
    }

    void start_db_read (slsfs::leveldb_pack::packet_pointer pack)
    {
        BOOST_LOG_TRIVIAL(trace) << "start_db_read";
        net::post(
            io_context_,
            [self=shared_from_this(), pack] {
                slsfs::leveldb_pack::rawblocks rb;
                leveldb::Status status = rb.bind(self->db_, pack->header.as_string());

                slsfs::leveldb_pack::packet_pointer resp = std::make_shared<slsfs::leveldb_pack::packet>();
                resp->header = pack->header;
                if (status.ok())
                {
                    resp->header.type = slsfs::leveldb_pack::msg_t::ack;

                    std::vector<slsfs::leveldb_pack::unit_t> buf(resp->header.datasize);

                    rb.read(pack->header.position, buf.begin(), resp->header.datasize);
                    BOOST_LOG_TRIVIAL(trace) << "start_db_read return : " << rb.buf_;

                    resp->data.buf.swap(buf);
                }
                else
                    resp->header.type = slsfs::leveldb_pack::msg_t::err;

                self->start_write_socket(resp);
                self->start_read_header();
            });
    }

    void start_write_socket(slsfs::leveldb_pack::packet_pointer pack)
    {
        auto next = std::make_shared<slsfs::socket_writer::boost_callback>(
            [self=shared_from_this(), pack] (boost::system::error_code ec, std::size_t transferred_size) {
                if (ec)
                {
                    BOOST_LOG_TRIVIAL(error) << "write error " << ec.message() << " while " << pack->header << " size=" << transferred_size;
                    return;
                }

                BOOST_LOG_TRIVIAL(trace) << "write sent " << transferred_size << " bytes, count=" << self.use_count() << " " << pack->header;
                //BOOST_LOG_TRIVIAL(trace) << "write took " << std::chrono::system_clock::now() - start;
            });

        writer_.start_write_socket(pack, next);
    }
};

class tcp_server
{
    net::io_context& io_context_;
    tcp::acceptor acceptor_;
    std::unique_ptr<leveldb::Cache> cache_ = nullptr;
    std::unique_ptr<leveldb::DB> db_ = nullptr;

    persistent_log db_log_;

public:
    tcp_server(net::io_context& io_context, net::ip::port_type const port, std::string const dbname, std::size_t const cache_size)
        : io_context_(io_context),
          acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
          db_log_{dbname + "_log", cache_size}
    {
        leveldb::DB* db = nullptr;
        leveldb::Options options;

        cache_.reset(leveldb::NewLRUCache(cache_size));

        options.create_if_missing = true;
        options.write_buffer_size = 32 * 1024 * 1024;
        options.block_cache = cache_.get();

        leveldb::Status status = leveldb::DB::Open(options, dbname, &db);
        if (not status.ok())
        {
            BOOST_LOG_TRIVIAL(error) << status.ToString() << "\n";
            throw std::runtime_error("cannot open db");
        }

        BOOST_LOG_TRIVIAL(debug) << "open db ptr: " << db << "\n";
        db_.reset(db);
        start_accept();
    }

    void start_accept()
    {
        acceptor_.async_accept(
            [this] (boost::system::error_code const& error, tcp::socket socket) {
                if (error)
                    BOOST_LOG_TRIVIAL(error) << "accept error: " << error.message() << "\n";
                else
                {
                    socket.set_option(tcp::no_delay(true));
                    auto accepted = std::make_shared<tcp_connection>(
                        io_context_,
                        std::move(socket),
                        *db_,
                        db_log_);
                    accepted->start_read_header();
                    start_accept();
                }
            });
    }
};

} // namespace ssbd

int main(int argc, char* argv[])
{
    ssbd::basic::init_log();

    namespace po = boost::program_options;
    po::options_description desc{"Options"};
    desc.add_options()
        ("help,h", "Print this help messages")
        ("listen,l", po::value<unsigned short>()->default_value(12000),             "listen on this port")
        ("db,d",     po::value<std::string>()->default_value("/tmp/haressbd/db"),   "leveldb save path")
        ("blocksize,b", po::value<std::size_t>()->default_value(4 * 1024),          "set block size (in bytes)")
        ("cachesize,c", po::value<std::size_t>()->default_value(100 * 1024 * 1024), "set leveldb cachesize (in bytes)" );
    po::positional_options_description pos_po;
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv)
              .options(desc)
              .positional(pos_po).run(), vm);
    po::notify(vm);

    if (vm.count("help"))
    {
        BOOST_LOG_TRIVIAL(info) << desc;
        return EXIT_SUCCESS;
    }

    int const worker = std::thread::hardware_concurrency();
    ssbd::net::io_context ioc {worker};
    ssbd::net::signal_set listener(ioc, SIGINT, SIGTERM);
    listener.async_wait(
        [&ioc](boost::system::error_code const&, int signal_number) {
            BOOST_LOG_TRIVIAL(info) << "Stopping... sig=" << signal_number;
            ioc.stop();
        });

    unsigned short const port = vm["listen"].as<unsigned short>();
    std::size_t    const size = vm["blocksize"].as<std::size_t>();
    std::string    const path = vm["db"].as<std::string>();
    std::size_t    const cachesize = vm["cachesize"].as<std::size_t>();

    slsfs::leveldb_pack::rawblocks {}.fullsize() = size;

    ssbd::tcp_server server{ioc, port, path, cachesize};
    BOOST_LOG_TRIVIAL(info) << "listen :" << port << " blocksize=" << size << " thread=" << worker;
    BOOST_LOG_TRIVIAL(trace) << "trace enabled";

    std::vector<std::thread> v;
    v.reserve(worker);
    for(int i = 1; i < worker; i++)
        v.emplace_back([&ioc] { ioc.run(); });
    ioc.run();

    for (std::thread& th : v)
        th.join();

    return EXIT_SUCCESS;
}
