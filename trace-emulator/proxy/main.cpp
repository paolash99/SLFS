#include "basic.hpp"
#include "serializer.hpp"
#include "trigger.hpp"
#include "launcher.hpp"
#include "zookeeper.hpp"
#include "socket-writer.hpp"
#include "uuid.hpp"

#include <boost/program_options.hpp>
#include <boost/log/trivial.hpp>
#include <boost/asio.hpp>
#include <boost/signals2.hpp>
#include <boost/format.hpp>

#include <oneapi/tbb/concurrent_unordered_map.h>
#include <oneapi/tbb/concurrent_queue.h>

#include <algorithm>
#include <iostream>

#include <memory>
#include <array>
#include <list>
#include <thread>
#include <vector>
#include <fstream>
#include <string>
#include <regex>

using slsfs::net::ip::tcp;
namespace net = slsfs::net;

class bucket
{
    net::io_context& io_context_;
    net::io_context::strand event_io_strand_;

    // for receiving messages
    oneapi::tbb::concurrent_queue<slsfs::pack::packet_data> message_queue_;

    // to issue a request to binded http url when a message comes in
    std::shared_ptr<slsfs::trigger::trigger<boost::beast::ssl_stream<boost::beast::tcp_stream>>> binding_;

    // holds callbacks of listeners //
    boost::signals2::signal<void (slsfs::pack::packet_pointer)> listener_;

public:
    bucket(net::io_context& io):
        io_context_{io},
        event_io_strand_{io} {}

    void to_trigger()
    {
        static std::string const url = "https://zion01/api/v1/namespaces/_/actions/slsfs-metadatafunction?blocking=false&result=false";
        if (binding_ == nullptr)
            binding_ = std::make_shared<slsfs::trigger::trigger<boost::beast::ssl_stream<boost::beast::tcp_stream>>>(io_context_, url, slsfs::basic::ssl_ctx());
    }

    void start_trigger_post(std::string const& body)
    {
        to_trigger();
        binding_->start_post(body);
    }

    template<typename Function>
    void get_connect(Function &&f)
    {
        listener_.connect(std::forward<Function>(f));
    }

    template<typename Msg>
    void push_message(Msg && m) { message_queue_.push(std::forward<Msg>(m)); }

    void start_handle_events(slsfs::pack::packet_pointer key)
    {
        BOOST_LOG_TRIVIAL(trace) << "start_handle_events starts";
        net::post(
            io_context_,
            net::bind_executor(
                event_io_strand_,
                [this, key] {
                    BOOST_LOG_TRIVIAL(trace) << "start_handle_events runed";
                    slsfs::pack::packet_pointer resp = std::make_shared<slsfs::pack::packet>();
                    resp->header = key->header;
                    resp->header.type = slsfs::pack::msg_t::ack;

                    if (resp->header.is_trigger())
                    {
                        BOOST_LOG_TRIVIAL(trace) << "post as trigger";
                        while (message_queue_.try_pop(resp->data))
                        {
                            // trigger
                            std::string body;
                            std::copy(key->data.buf.begin(),
                                      key->data.buf.end(),
                                      std::back_inserter(body));
                            start_trigger_post(body);
                        }
                    }
                    else
                    {
                        BOOST_LOG_TRIVIAL(trace) << "start listener events. listener empty=" << listener_.empty() << ", mqueue empty=" << message_queue_.empty();
                        if (listener_.empty() or message_queue_.empty())
                            return;

                        BOOST_LOG_TRIVIAL(trace) << "running listener events";
                        while (message_queue_.try_pop(resp->data))
                            listener_(resp);

                        BOOST_LOG_TRIVIAL(trace) << "clear listener_ ";
                        listener_.disconnect_all_slots();
                    }
                }));
    }
};

using topics =
    oneapi::tbb::concurrent_hash_map<
        slsfs::pack::packet_header,
        bucket,
        slsfs::pack::packet_header_full_key_hash_compare>;

using topics_accessor = topics::accessor;

class tcp_connection : public std::enable_shared_from_this<tcp_connection>
{
    net::io_context& io_context_;
    topics& topics_;
    tcp::socket socket_;
    slsfs::socket_writer::socket_writer<slsfs::pack::packet, std::vector<slsfs::pack::unit_t>> writer_;
    slsfs::launcher::launcher& launcher_;

public:
    using pointer = std::shared_ptr<tcp_connection>;

    tcp_connection(net::io_context& io, topics& s, tcp::socket socket, slsfs::launcher::launcher &l):
        io_context_{io},
        topics_{s},
        socket_{std::move(socket)},
        writer_{io, socket_},
        launcher_{l} {}

    auto socket() -> tcp::socket& { return socket_; }

    auto get_bucket(slsfs::pack::packet_header &h) -> bucket&
    {
        topics_accessor it;
        if (bool found = topics_.find(it, h); not found)
            topics_.emplace(it, h, io_context_);
        return it->second;
    }

    void start_read_header()
    {
        BOOST_LOG_TRIVIAL(trace) << "start_read_header";
        auto read_buf = std::make_shared<std::array<slsfs::pack::unit_t, slsfs::pack::packet_header::bytesize>>();
        net::async_read(
            socket_,
            net::buffer(read_buf->data(), read_buf->size()),
            [self=shared_from_this(), read_buf] (boost::system::error_code ec, std::size_t /*length*/) {
                if (not ec)
                {
                    slsfs::pack::packet_pointer pack = std::make_shared<slsfs::pack::packet>();
                    pack->header.parse(read_buf->data());

                    switch (pack->header.type)
                    {
                    case slsfs::pack::msg_t::put:
                        BOOST_LOG_TRIVIAL(debug) << "put " << pack->header;
                        self->start_read_body(pack);
                        break;

                    case slsfs::pack::msg_t::get:
                        BOOST_LOG_TRIVIAL(debug) << "get " << pack->header;
                        self->start_load(pack);
                        self->start_read_header();
                        break;

                    case slsfs::pack::msg_t::ack:
                    {
                        BOOST_LOG_TRIVIAL(error) << "server should not get ack. error: " << pack->header;
                        slsfs::pack::packet_pointer resp = std::make_shared<slsfs::pack::packet>();
                        resp->header = pack->header;
                        resp->header.type = slsfs::pack::msg_t::ack;
                        self->start_write(resp);
                        self->start_read_header();
                        break;
                    }

                    case slsfs::pack::msg_t::worker_reg:
                        BOOST_LOG_TRIVIAL(trace) << "server add worker" << pack->header;
                        self->launcher_.add_worker(std::move(self->socket_), pack);
                        break;

                    case slsfs::pack::msg_t::trigger:
                        BOOST_LOG_TRIVIAL(trace) << "server get new trigger " << pack->header;
                        self->start_trigger(pack);
                        break;

                    case slsfs::pack::msg_t::set_timer:
                    case slsfs::pack::msg_t::proxyjoin:
                    case slsfs::pack::msg_t::err:
                    case slsfs::pack::msg_t::worker_dereg:
                    case slsfs::pack::msg_t::worker_push_request:
                    case slsfs::pack::msg_t::worker_response:
                    case slsfs::pack::msg_t::trigger_reject:
                    {
                        BOOST_LOG_TRIVIAL(error) << "packet error " << pack->header << " from endpoint: " << self->socket_.remote_endpoint();
                        slsfs::pack::packet_pointer resp = std::make_shared<slsfs::pack::packet>();
                        resp->header = pack->header;
                        resp->header.type = slsfs::pack::msg_t::err;
                        self->start_write(resp);
                        self->start_read_header();
                        break;
                    }
                    }
                }
                else
                {
                    if (ec != boost::asio::error::eof)
                        BOOST_LOG_TRIVIAL(error) << "start_read_header err: " << ec.message();
                }
            });
    }

    void start_trigger(slsfs::pack::packet_pointer pack)
    {
        BOOST_LOG_TRIVIAL(trace) << "start_trigger";
        auto read_buf = std::make_shared<std::string>(pack->header.datasize, 0);
        net::async_read(
            socket_,
            net::buffer(read_buf->data(), read_buf->size()),
            [self=shared_from_this(), read_buf, pack] (boost::system::error_code ec, std::size_t /*length*/) {
                if (not ec)
                {
                    self->launcher_.start_trigger_post(
                        *read_buf, pack,
                        [self, pack] (slsfs::pack::packet_pointer resp) {
                            self->start_write(resp);
                        });
                    self->start_read_header();
                }
                else
                    BOOST_LOG_TRIVIAL(error) << "start_trigger: " << ec.message();
            });
    }

    void start_read_body(slsfs::pack::packet_pointer pack)
    {
        BOOST_LOG_TRIVIAL(trace) << "start_read_body";
        auto read_buf = std::make_shared<std::vector<slsfs::pack::unit_t>>(pack->header.datasize);
        net::async_read(
            socket_,
            net::buffer(read_buf->data(), read_buf->size()),
            [self=shared_from_this(), read_buf, pack] (boost::system::error_code ec, std::size_t length) {
                if (not ec)
                {
                    pack->data.parse(length, read_buf->data());
                    self->start_store(pack);
                    self->start_read_header();
                }
                else
                    BOOST_LOG_TRIVIAL(error) << "start_read_body: " << ec.message();
            });
    }

    void start_store(slsfs::pack::packet_pointer pack)
    {
        net::post(
            io_context_,
            [self=shared_from_this(), pack] {
                bucket& buck = self->get_bucket(pack->header);
                buck.push_message(pack->data);
                buck.start_handle_events(pack);

                slsfs::pack::packet_pointer resp = std::make_shared<slsfs::pack::packet>();
                resp->header = pack->header;
                resp->header.type = slsfs::pack::msg_t::ack;
                self->start_write(resp);
            });
    }

    void start_load(slsfs::pack::packet_pointer pack)
    {
        BOOST_LOG_TRIVIAL(trace) << "start_load";
        net::post(
            io_context_,
            [self=shared_from_this(), pack] {
                BOOST_LOG_TRIVIAL(trace) << "load: register listener";

                self->get_bucket(pack->header).get_connect(
                    [self=self->shared_from_this()](slsfs::pack::packet_pointer pack) {
                        BOOST_LOG_TRIVIAL(trace) << "run signaled write";
                        self->start_write(pack);
                    });

                self->get_bucket(pack->header).start_handle_events(pack);
            });
    }

    void start_write(slsfs::pack::packet_pointer pack)
    {
        auto next = std::make_shared<slsfs::socket_writer::boost_callback>(
            [self=shared_from_this()] (boost::system::error_code ec, std::size_t /*length*/) {
                if (ec)
                    BOOST_LOG_TRIVIAL(error) << "tcp conn start write error: " << ec.message();
                else
                    BOOST_LOG_TRIVIAL(trace) << "tcp conn wrote msg";
            });

        writer_.start_write_socket(pack, next);
    }
};

class tcp_server
{
    slsfs::uuid::uuid id_;
    net::io_context& io_context_;
    tcp::acceptor acceptor_;
    topics topics_;
    slsfs::launcher::launcher launcher_;

public:
    tcp_server(net::io_context& io_context, net::ip::port_type port, slsfs::uuid::uuid & id, std::string const& announce, std::string const& save_report)
        : id_{id},
          io_context_(io_context),
          acceptor_(io_context, tcp::endpoint(tcp::v4(), port)),
          launcher_{io_context, id_, announce, port, save_report} {}

    template<typename PolicyType, typename ... Args>
    void set_policy_filetoworker(Args&& ... args) {
        launcher_.set_policy_filetoworker<PolicyType>(std::forward<Args>(args)...);
    }

    template<typename PolicyType, typename ... Args>
    void set_policy_launch(Args&& ... args) {
        launcher_.set_policy_launch<PolicyType>(std::forward<Args>(args)...);
    }

    template<typename PolicyType, typename ... Args>
    void set_policy_keepalive(Args&& ... args) {
        launcher_.set_policy_keepalive<PolicyType>(std::forward<Args>(args)...);
    }

    template<typename ... Args>
    void set_worker_config(Args&& ... args) {
        launcher_.set_worker_config (std::forward<Args>(args)...);
    }

    void start_accept()
    {
        acceptor_.async_accept(
            [this] (boost::system::error_code const& error, tcp::socket socket) {
                if (not error)
                {
                    auto accepted = std::make_shared<tcp_connection>(
                        io_context_,
                        topics_,
                        std::move(socket),
                        launcher_);
                    accepted->start_read_header();
                    start_accept();
                }
            });
    }

    auto launcher() -> slsfs::launcher::launcher& { return launcher_; }
};

void set_policy_filetoworker(tcp_server& server, std::string const& policy, [[maybe_unused]] std::string const& args)
{
    using namespace slsfs::basic::sswitcher;

    switch (hash(policy))
    {
    case "lowest-load"_:
        server.set_policy_filetoworker<slsfs::launcher::policy::lowest_load>();
        break;

    case "random-assign"_:
        server.set_policy_filetoworker<slsfs::launcher::policy::random_assign>();
        break;

    case "active-load-balance"_:
        server.set_policy_filetoworker<slsfs::launcher::policy::active_lowest_load>();
        break;

    default:
        using namespace std::string_literals;
        throw std::runtime_error("unknown filetoworker policy: "s + policy);
    }
}

void set_policy_launch(tcp_server& server, std::string const& policy, std::string const& args)
{
    using namespace slsfs::basic::sswitcher;
    switch (hash(policy))
    {
    case "const-average-load"_:
    {
        std::regex const pattern("(\\d+):(\\d+)");
        std::smatch match;
        if (std::regex_search(args, match, pattern))
        {
            int const max_outstanding_starting_request = std::stoi(match[1]);
            std::uint64_t const max_average_load = std::stoull(match[2]);
            server.set_policy_launch<slsfs::launcher::policy::const_average_load>(max_outstanding_starting_request, max_average_load);
        }
        else
            throw std::runtime_error("unable to parse args for launch policy; should be max_latency:min_process");
        break;
    }

    case "max-queue"_:
    {
        std::regex const pattern("(\\d+):(\\d+)");
        std::smatch match;
        if (std::regex_search(args, match, pattern))
        {
            int const max_outstanding_starting_request = std::stoi(match[1]);
            std::uint64_t const max_average_queue = std::stoull(match[2]); // queue size
            server.set_policy_launch<slsfs::launcher::policy::max_queue>(max_outstanding_starting_request, max_average_queue);
        }
        else
            throw std::runtime_error("unable to parse args for launch policy; should be max_latency:min_process");
        break;
    }
    default:
        using namespace std::string_literals;
        throw std::runtime_error("unknown launch policy: "s + policy);
    }
}

void set_policy_keepalive(tcp_server& server, std::string const& policy, std::string const& args)
{
    using namespace slsfs::basic::sswitcher;
    switch (hash(policy))
    {
    case "const-time"_:
        server.set_policy_keepalive<slsfs::launcher::policy::keepalive_const_time>(std::stoi(args) /* ms */);
        break;
    case "moving-interval"_:
    {
        std::regex const pattern("(\\d+):(\\d+):(\\d+):(\\d+)");
        std::smatch match;
        if (std::regex_search(args, match, pattern))
        {
            int const sma_buffer_size       = std::stoi(match[1]);
            int const default_wait_time     = std::stoi(match[2]);
            int const concurrency_threshold = std::stoi(match[3]);
            double const error_margin       = std::stoi(match[4]) * 0.01;
            BOOST_LOG_TRIVIAL(trace) << "Moving-Interval policy with arguments:" << sma_buffer_size << default_wait_time << concurrency_threshold << error_margin;

            server.set_policy_keepalive<slsfs::launcher::policy::keepalive_moving_interval>(
                sma_buffer_size,
                default_wait_time,
                concurrency_threshold,
                error_margin
            );
        }
        else
            throw std::runtime_error(
                "unable to parse args for keepalive policy; should be sma_buffer_size:default_keepalive:concurrency_threshold:error_margin");
        break;
    }
    case "moving-interval-global"_:
    {
        std::regex const pattern("(\\d+):(\\d+):(\\d+):(\\d+)");
        std::smatch match;
        if (std::regex_search(args, match, pattern))
        {
            int const sma_buffer_size       = std::stoi(match[1]);
            int const default_wait_time     = std::stoi(match[2]);
            int const concurrency_threshold = std::stoi(match[3]);
            double const error_margin       = std::stoi(match[4]) * 0.01;
            BOOST_LOG_TRIVIAL(trace) << "Moving-Interval-Global policy with arguments:" << sma_buffer_size << default_wait_time << concurrency_threshold << error_margin;

            server.set_policy_keepalive<slsfs::launcher::policy::keepalive_moving_interval_global>(
                sma_buffer_size,
                default_wait_time,
                concurrency_threshold,
                error_margin
            );
        }
        else
            throw std::runtime_error(
                "unable to parse args for keepalive policy; should be sma_buffer_size:default_keepalive:concurrency_threshold:error_margin");
        break;
    }

    default:
        using namespace std::string_literals;
        throw std::runtime_error("unknown keepalive policy: "s + policy);
    }
}

int main(int argc, char* argv[])
{
    boost::log::trivial::severity_level level = boost::log::trivial::info;

    std::string verbosity_values;
    namespace po = boost::program_options;
    po::options_description desc{"Options"};
    desc.add_options()
        ("help,h", "Print this help messages")
        ("listen,l",  po::value<unsigned short>()->default_value(12000),            "listen on this port")
        ("verbose,v", po::value<std::string>(&verbosity_values)->implicit_value(""),"log verbosity")
        ("init",      po::bool_switch(),                                            "reset all system (clear zookeeper entries)")
        ("initint",   po::value<int>()->default_value(0),                           "reset all system with 0, 1")
        ("thread",    po::value<int>()->default_value(std::thread::hardware_concurrency()), "# of thread")
        ("announce",  po::value<std::string>(),                                     "announce this ip address for other proxy to connect")
        ("report",    po::value<std::string>()->default_value("/dev/null"),         "path to save report every seconds")
        ("policy-filetoworker",      po::value<std::string>(),                      "file to worker policy name")
        ("policy-filetoworker-args", po::value<std::string>()->default_value(""),   "file to worker policy name extra args")
        ("policy-launch",            po::value<std::string>(),                      "launch policy name")
        ("policy-launch-args",       po::value<std::string>()->default_value(""),   "launch policy name extra args")
        ("policy-keepalive",         po::value<std::string>(),                      "keepalive policy name")
        ("policy-keepalive-args",    po::value<std::string>()->default_value(""),   "keepalive policy name extra args")
        ("worker-config",            po::value<std::string>(),                      "worker config json file path to use")
        ("max-function-count",       po::value<int>()->default_value(0),            "marks the max random function name to use")
        ("blocksize",                po::value<int>()->default_value(4096),         "worker config blocksize");
    po::positional_options_description pos_po;
    po::variables_map vm;
    po::store(po::command_line_parser(argc, argv)
              .options(desc)
              .positional(pos_po).run(), vm);
    po::notify(vm);

    if (vm.count("help"))
    {
        BOOST_LOG_TRIVIAL(info) << desc;
        return EXIT_FAILURE;
    }

    if (vm.count("verbosity"))
        verbosity_values += "v";

    int const verbosity = verbosity_values.size();

    slsfs::basic::init_log(static_cast<boost::log::trivial::severity_level>(level - static_cast<boost::log::trivial::severity_level>(verbosity)));
    BOOST_LOG_TRIVIAL(trace) << "set verbosity=" << verbosity;

    int const worker  = vm["thread"].as<int>();
    net::io_context ioc {worker};

    unsigned short const port  = vm["listen"].as<unsigned short>();
    std::string const announce = vm["announce"].as<std::string>();
    std::string const save_report = vm["report"].as<std::string>();
    int  const blocksize       = vm["blocksize"].as<int>();
    bool const init_cluster    = vm["init"].as<bool>();
    //slsfs::uuid::uuid server_id = slsfs::uuid::gen_uuid();
    slsfs::uuid::uuid server_id = slsfs::uuid::gen_uuid_static_seed(announce);

    std::string worker_config;
    {
        std::ifstream worker_configuration(vm["worker-config"].as<std::string>());

        if (!worker_configuration.is_open())
        {
            BOOST_LOG_TRIVIAL(fatal) << "config " <<  vm["worker-config"].as<std::string>() << "not found";
            return 1;
        }

        std::stringstream template_config;
        template_config << worker_configuration.rdbuf();
        worker_config = (boost::format(template_config.str()) % announce % port % blocksize).str();
    }

    tcp_server server{ioc, port, server_id, announce, save_report};

    set_policy_filetoworker(server, vm["policy-filetoworker"].as<std::string>(), vm["policy-filetoworker-args"].as<std::string>());
    set_policy_launch      (server, vm["policy-launch"]      .as<std::string>(), vm["policy-launch-args"]      .as<std::string>());
    set_policy_keepalive   (server, vm["policy-keepalive"]   .as<std::string>(), vm["policy-keepalive-args"]   .as<std::string>());
    server.set_worker_config(worker_config, vm["max-function-count"].as<int>());

    server.start_accept();
    BOOST_LOG_TRIVIAL(info) << server_id << " listen on " << port;

    std::vector<char> announce_buf;
    fmt::format_to(std::back_inserter(announce_buf), "{}:{}", announce, port);

    slsfs::zookeeper::zookeeper zoo {ioc, server.launcher(), server_id, announce_buf};

    if (init_cluster || vm["initint"].as<int>() != 0)
    {
        BOOST_LOG_TRIVIAL(info) << "init cluster + init zookeeper";
        zoo.reset();
    }
    else
        zoo.start_setup();

    net::signal_set listener(ioc, SIGINT, SIGTERM);
    listener.async_wait(
        [&ioc, &zoo] (boost::system::error_code const&, int signal_number) {
            BOOST_LOG_TRIVIAL(info) << "Stopping... sig=" << signal_number;
            zoo.shutdown();
            ioc.stop();
        });

    std::vector<std::thread> worker_threads;
    worker_threads.reserve(worker);
    for(int i = 1; i < worker; i++)
        worker_threads.emplace_back([&ioc] { ioc.run(); });
    ioc.run();

    for (std::thread& th : worker_threads)
        th.join();

    return EXIT_SUCCESS;
}
