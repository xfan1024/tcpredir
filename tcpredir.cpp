/*
 * Copyright (c) 2022, xiaofan <xfan1024@live.com>
 *
 * SPDX-License-Identifier: MIT
 */
#include <iostream>
#include <optional>
#include <memory>
#include <thread>
#include <list>
#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/intrusive/list.hpp>
#include "socket_util.h"
#include "circular_buffer.h"

/* TODO: add log system */
/* TODO: add mechanism to read current connections */

/* TODO: use command argument instead of compile-time value */
constexpr size_t client_buffer_size = 1460;
constexpr size_t server_buffer_size = 1460;
constexpr uint16_t listen_port = 1000;
constexpr unsigned int expired_seconds = 300;
constexpr unsigned int thread_number = 0; // zero means auto calcuate best thread_number

/* for some convenience */
namespace asio = boost::asio;
using io_service_t = asio::io_service;
using deadline_timer_t = asio::deadline_timer;
namespace ip = asio::ip;
using tcp = ip::tcp;
using socket_t = tcp::socket;
using endpoint_t = tcp::endpoint;
using error_code_t = boost::system::error_code;

class tcpredir_connection;
class tcpredir_pair;
class tcpredir_worker;

class tcpredir_connection
{
public:

    tcpredir_connection(io_service_t &ioc, tcpredir_pair &pair, socket_t socket, size_t receive_buffer)
    : _pair{pair}, _socket{std::move(socket)}, _buffer{receive_buffer}, _timer{ioc}
    {
        _sending = false;
        _receiving = false;
        _peer = nullptr;
        reset_timer();
    }

    ~tcpredir_connection()
    {
        if (_peer)
            _peer->_peer = nullptr;
    }

    auto make_buffer_for_read()
    {
        size_t max;
        uint8_t *buf = _buffer.readptr(&max);
        return asio::buffer(buf, max);
    }

    auto make_buffer_for_write()
    {
        size_t max;
        uint8_t *buf = _buffer.writeptr(&max);
        return asio::buffer(buf, max);
    }

    void start_receive()
    {
        do_receive();
    }

    void set_peer(struct tcpredir_connection *peer)
    {
        peer->_peer = this;
        _peer = peer;
        do_send();
        _peer->do_send();
    }

private:
    bool have_data_to_send()
    {
        if (_sending)
            return true;
        if (_peer && !_peer->_buffer.empty())
            return true;
        return false;
    }

    void reset_timer();
    void close();
    void do_send();
    void do_receive();
    void receive_cb(const error_code_t &ec, size_t transfered);
    void send_cb(const error_code_t &ec, size_t transfered);

    bool _sending;
    bool _receiving;

    tcpredir_pair &_pair;
    socket_t _socket;
    circular_buffer _buffer;
    struct tcpredir_connection *_peer;
    deadline_timer_t _timer;
};

class tcpredir_pair : public boost::intrusive::list_base_hook<>, public std::enable_shared_from_this<tcpredir_pair>
{
public:
    tcpredir_pair(io_service_t &ioc, socket_t &&client_socket, endpoint_t client_endpoint, endpoint_t server_endpoint)
    : _ioc(ioc), _server_socket_tmp(ioc)
    {
        _client.emplace(_ioc, *this, std::move(client_socket), client_buffer_size);
        _client_endpoint = client_endpoint;
        _server_endpoint = server_endpoint;
    }

    void start()
    {
        _self_reference = shared_from_this();
        _server_socket_tmp.async_connect(server_endpoint(), 
            [this, pair = weak_from_this()](const error_code_t &ec)
            {
                if (pair.expired())
                    return;
                connected(ec);
            }
        );
        _client->start_receive();
    }

    void close()
    {
        _client.reset();
        _server.reset();
        _self_reference = nullptr;
    }

    void connected(const error_code_t &ec)
    {
        if (ec)
        {
            close();
            return;
        }
        _server.emplace(_ioc, *this, std::move(_server_socket_tmp), server_buffer_size);
        _server->set_peer(&*_client);
        _server->start_receive();
    }

    const endpoint_t& client_endpoint()
    {
        return _client_endpoint;
    }

    const endpoint_t& server_endpoint()
    {
        return _server_endpoint;
    }

private:
    friend class tcpredir_connection;
    io_service_t& _ioc;
    std::shared_ptr<tcpredir_pair> _self_reference;
    socket_t _server_socket_tmp;
    std::optional<tcpredir_connection> _client;
    std::optional<tcpredir_connection> _server;
    endpoint_t _client_endpoint;
    endpoint_t _server_endpoint;
};

void tcpredir_connection::close()
{
    _socket.close();
    if (!_peer || !_peer->have_data_to_send())
    {
        _pair.close();
        return;
    }
}

void tcpredir_connection::reset_timer()
{
    _timer.expires_from_now(boost::posix_time::seconds(expired_seconds));
    _timer.async_wait([this, pair = _pair.weak_from_this()](const error_code_t& ec)
        {
            if (pair.expired() || ec)
                return;
            // const endpoint_t &ep = (this == &*_pair._client) ? _pair._client_endpoint : _pair._server_endpoint; 
            // std::cout << "[TMO] " << ep << std::endl;
            close();
        }
    );
}

void tcpredir_connection::do_send()
{
    if (_sending || !_socket.is_open())
        return;
    if (_peer == nullptr || (!_peer->_socket.is_open() && _peer->_buffer.empty()))
    {
        _pair.close();
        return;
    }
    if (_peer->_buffer.empty())
        return;
    _sending = true;
    _socket.async_send(_peer->make_buffer_for_read(), 
        [this, pair = _pair.weak_from_this()](const error_code_t &ec, size_t transfered)
        {
            if (pair.expired())
                return;
            send_cb(ec, transfered);
        }
    );
}

void tcpredir_connection::do_receive()
{
    if (_receiving || !_socket.is_open() || _buffer.full())
        return;
    _receiving = true;
    _socket.async_receive(make_buffer_for_write(),
        [this, pair = _pair.weak_from_this()](const error_code_t &ec, size_t transfered)
        {
            if (pair.expired())
                return;
            receive_cb(ec, transfered);
        }
    );
}

void tcpredir_connection::receive_cb(const error_code_t &ec, size_t transfered)
{
    _receiving = false;
    if (ec)
    {
        if (ec.category() == asio::error::get_misc_category() && ec.value() == asio::error::misc_errors::eof)
            close();
        else
        {
            _pair.close();
            return;
        }
    }
    else
    {
        reset_timer();
        _buffer.produce(transfered);
        do_receive();
    }
    
    if (_peer)
        _peer->do_send();
}

void tcpredir_connection::send_cb(const error_code_t &ec, size_t transfered)
{
    _sending = false;
    if (ec)
    {
        _pair.close();
        return;
    }
    reset_timer();
    if (_peer)
    {
        _peer->_buffer.consume(transfered);
        _peer->do_receive();
    }
    do_send();
}

class tcpredir_worker
{
public:
    tcpredir_worker(io_service_t &ioc) : _ioc{ioc}, _acceptor{ioc}, _client_socket_tmp{ioc}
    {}

    ~tcpredir_worker()
    {
        stop();
    }

    void start(endpoint_t ep)
    {
        typedef boost::asio::detail::socket_option::boolean<SOL_SOCKET, SO_REUSEPORT> reuse_reuseport;
        _acceptor.open(ep.protocol());
        if (_acceptor.local_endpoint().address().is_v6())
            _acceptor.set_option(ip::v6_only(true));
        _acceptor.set_option(reuse_reuseport(true));
        _acceptor.bind(ep);
        _acceptor.listen(1);
        do_accept();
    }

    void  stop()
    {
        _acceptor.close();
        _connection_pairs.clear_and_dispose(
            [](tcpredir_pair* pair)
            {
                pair->close();
            }
        );
    }

private:
    void do_accept()
    {
        _acceptor.async_accept(_client_socket_tmp,
            [this](const error_code_t &ec)
            {
                accept_cb(ec, std::move(_client_socket_tmp));
            }
        );
    }
    
    void accept_cb(const error_code_t &ec, socket_t socket)
    {
        if (ec)
        {
            stop();
            return;
        }
        endpoint_t client_endpoint;
        endpoint_t server_endpoint;
        client_endpoint = socket.remote_endpoint();
        if (get_original_destination(socket.native_handle(), &server_endpoint) && socket.local_endpoint() != server_endpoint)
        {
            std::shared_ptr<tcpredir_pair> pair{new tcpredir_pair{_ioc, std::move(socket), client_endpoint, server_endpoint}, 
                [this](tcpredir_pair* p)
                {
                    if (p->is_linked())
                    {
                        auto it = decltype(_connection_pairs)::s_iterator_to(*p);
                        _connection_pairs.erase(it);
                    }
                    delete p;
                    // std::cout << "[DEL] " << std::setw(4) << _connection_pairs.size() << " | "
                    //     << p->client_endpoint() << " <-> " << p->server_endpoint() << std::endl;
                }
            };
            pair->start();
            _connection_pairs.push_back(*pair);
            // std::cout << "[ADD] " << std::setw(4) << _connection_pairs.size() << " | " 
            //     << client_endpoint << " <-> " << server_endpoint << std::endl;
        }
        do_accept();
    }

    io_service_t &_ioc;
    boost::intrusive::list<tcpredir_pair> _connection_pairs; 
    tcp::acceptor _acceptor;
    socket_t _client_socket_tmp;
};

static unsigned int calcuate_best_threads()
{
    unsigned int nproc = std::thread::hardware_concurrency();
    if (!nproc)
        return 2;
    return 2 * nproc;
}

struct worker_thread : std::thread
{
    worker_thread() = default;
    worker_thread(const worker_thread&) = delete;
    worker_thread& operator=(const worker_thread&) = delete;

    void thread_entry()
    {
        worker_v4.start(endpoint_t(tcp::v4(), listen_port));
        worker_v6.start(endpoint_t(tcp::v6(), listen_port));
        ioc.run();
    }

    void start(std::function<void()> done_callback = nullptr)
    {
        std::thread &thread = *this;
        thread = std::thread([this, _done_callback = std::move(done_callback)]()
            {
                thread_entry();
                if (_done_callback)
                    _done_callback();
            }
        );
    }

    void cancel()
    {
        ioc.post([this]()
            {
                worker_v4.stop();
                worker_v6.stop();
            }
        );
    }

    io_service_t ioc;
    tcpredir_worker worker_v4 {ioc};
    tcpredir_worker worker_v6 {ioc};
};

void main_for_multiple_thread(unsigned int n)
{
    std::list<worker_thread> threads;
    io_service_t ioc;
    std::optional<io_service_t::work> work {ioc};
    unsigned int downcounter = n;

    for (unsigned int i = 0; i < n; ++i)
    {
        threads.emplace_back().start([&ioc, &work, &downcounter]()
            {
                ioc.post([&work, &downcounter]()
                    {
                        --downcounter;
                        // std::cout << "thread exited. remaining " << downcounter << " threads" << std::endl;
                        if (downcounter == 0)
                        {
                            work.reset();
                        }
                    }
                );
            }
        );
    }

    asio::signal_set sigint(ioc, SIGINT);
    sigint.async_wait([&threads](const error_code_t &ec, int)
        {
            if (ec)
                return;
            std::cout << '\n';
            // std::cout << "stopping threads: " << threads.size() << std::endl;
            for (auto &thread : threads)
                thread.cancel();
        }
    );
    ioc.run();

    for (auto &t : threads)
        t.join();
}

void main_for_signal_thread()
{
    io_service_t ioc;
    tcpredir_worker worker_v4 {ioc};
    tcpredir_worker worker_v6 {ioc};

    worker_v4.start(endpoint_t(tcp::v4(), listen_port));
    worker_v6.start(endpoint_t(tcp::v6(), listen_port));
    asio::signal_set sigint(ioc, SIGINT);
    sigint.async_wait([&worker_v4, &worker_v6](const error_code_t &ec, int)
        {
            if (ec)
                return;
            std::cout << '\n';
            // std::cout << "stopping server" << std::endl;
            worker_v4.stop();
            worker_v6.stop();
        }
    );
    ioc.run();
}

int main()
{
    unsigned int nthread = thread_number;
    if (!nthread)
        nthread = calcuate_best_threads();
    if (nthread > 1)
        main_for_multiple_thread(nthread);
    else
        main_for_signal_thread();
    return 0;
}
