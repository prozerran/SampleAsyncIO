
#ifndef _SP_SOCKET_SERVER_H_
#define _SP_SOCKET_SERVER_H_

#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ip/udp.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <memory>
#include <set>
#include <string>

// https://www.boost.org/doc/libs/1_78_0/doc/html/boost_asio/example/cpp11/timeouts/server.cpp
// https://dens.website/tutorials/cpp-asio/async-tcp-server

namespace SPSocket
{
	using boost::asio::steady_timer;
	using boost::asio::ip::tcp;
	using boost::asio::ip::udp;

    //----------------------------------------------------------------------

    class Subscriber {
    public:
        virtual ~Subscriber() = default;
        virtual void deliver(const std::string& msg) = 0;
    };

    typedef std::shared_ptr<Subscriber> subscriber_ptr;

    //----------------------------------------------------------------------

    class Channel {
    public:
        void Join(subscriber_ptr subscriber)
        {
            subscribers_.insert(subscriber);
        }

        void Leave(subscriber_ptr subscriber)
        {
            subscribers_.erase(subscriber);
        }

        void Deliver(const std::string& msg)
        {
            for (const auto& s : subscribers_)
            {
                s->deliver(msg);
            }
        }

    private:
        std::set<subscriber_ptr> subscribers_;
    };

    //
    // This class manages socket timeouts by applying the concept of a deadline.
    // Some asynchronous operations are given deadlines by which they must complete.
    // Deadlines are enforced by two "actors" that persist for the lifetime of the
    // session object, one for input and one for output:
    //
    //  +----------------+                      +----------------+
    //  |                |                      |                |
    //  | check_deadline |<-------+             | check_deadline |<-------+
    //  |                |        |             |                |        |
    //  +----------------+        |             +----------------+        |
    //               |            |                          |            |
    //  async_wait() |    +----------------+    async_wait() |    +----------------+
    //   on input    |    |     lambda     |     on output   |    |     lambda     |
    //   deadline    +--->|       in       |     deadline    +--->|       in       |
    //                    | check_deadline |                      | check_deadline |
    //                    +----------------+                      +----------------+
    //
    // If either deadline actor determines that the corresponding deadline has
    // expired, the socket is closed and any outstanding operations are cancelled.
    //
    // The input actor reads messages from the socket, where messages are delimited
    // by the newline character:
    //
    //  +-------------+
    //  |             |
    //  |  read_line  |<----+
    //  |             |     |
    //  +-------------+     |
    //          |           |
    //  async_- |    +-------------+
    //   read_- |    |   lambda    |
    //  until() +--->|     in      |
    //               |  read_line  |
    //               +-------------+
    //
    // The deadline for receiving a complete message is 30 seconds. If a non-empty
    // message is received, it is delivered to all subscribers. If a heartbeat (a
    // message that consists of a single newline character) is received, a heartbeat
    // is enqueued for the client, provided there are no other messages waiting to
    // be sent.
    //
    // The output actor is responsible for sending messages to the client:
    //
    //  +----------------+
    //  |                |<---------------------+
    //  |  await_output  |                      |
    //  |                |<-------+             |
    //  +----------------+        |             |
    //    |            |          |             |
    //    |    async_- |  +----------------+    |
    //    |     wait() |  |     lambda     |    |
    //    |            +->|       in       |    |
    //    |               |  await_output  |    |
    //    |               +----------------+    |
    //    V                                     |
    //  +--------------+               +--------------+
    //  |              | async_write() |    lambda    |
    //  |  write_line  |-------------->|      in      |
    //  |              |               |  write_line  |
    //  +--------------+               +--------------+
    //
    // The output actor first waits for an output message to be enqueued. It does
    // this by using a steady_timer as an asynchronous condition variable. The
    // steady_timer will be signalled whenever the output queue is non-empty.
    //
    // Once a message is available, it is sent to the client. The deadline for
    // sending a complete message is 30 seconds. After the message is successfully
    // sent, the output actor again waits for the output queue to become non-empty.
    //
    typedef class SPSocketServer* SPSocketServerPtr;

    class TCP_Session : public Subscriber, public std::enable_shared_from_this<TCP_Session> {
    public:

        explicit TCP_Session(tcp::socket socket, Channel& ch, SPSocketServerPtr sp);

        // Called by the server object to initiate the four actors.
        void Start();

        // Async read until terminator detected, return string via OnReceive
        void UseReadUntil(char terminator = '\n');

        // Read timeout value in seconds, 0 = infinite (default)
        void UseReadWriteTimeOut(int rw_timeout_sec) { rw_timeout = rw_timeout_sec; }

        // Broadcast message to all clients
        void BroadCast(const std::string& msg) const { channel_.Deliver(msg); }

        // Send message to connecting client
        void Send(const std::string& msg) { deliver(msg); }

    private:
        
        void stop();
        bool stopped() const;
        void deliver(const std::string& msg) override;
        void read_line();
        void await_output();
        void write_line();
        void check_deadline(steady_timer& deadline);

    private:

        char read_terminator = '\n';
        int rw_timeout = 0;

        SPSocketServerPtr socket_server_;

        Channel& channel_;
        tcp::socket socket_;
        std::string input_buffer_;
        steady_timer input_deadline_{ socket_.get_executor() };
        std::deque<std::string> output_queue_;
        steady_timer non_empty_output_queue_{ socket_.get_executor() };
        steady_timer output_deadline_{ socket_.get_executor() };
    };

    typedef std::shared_ptr<TCP_Session> tcp_session_ptr;

    //----------------------------------------------------------------------

    class UDP_Broadcaster : public Subscriber {
    public:

        explicit UDP_Broadcaster(boost::asio::io_context& io_context, const udp::endpoint& broadcast_endpoint);

    private:

        void deliver(const std::string& msg);

        udp::socket socket_;
    };

    //----------------------------------------------------------------------

    class SPSocketServer {
    public:

        explicit SPSocketServer(boost::asio::io_context& io_context, const tcp::endpoint& listen_endpoint, const udp::endpoint& broadcast_endpoint);

        virtual ~SPSocketServer() noexcept {};

        // Start Server
        void StartServer() { OnServerStarted(); accept(); }

        // Async read until terminator detected, return string via OnReceive
        void UseReadUntil(char terminator = '\n') { read_terminator = terminator; }

        // Read timeout value in seconds, 0 = infinite (default)
        void UseReadWriteTimeOut(int rw_timeout_sec) { read_write_timeout = rw_timeout_sec; }

        // Broadcast messsage to all connecting clients
        void BroadCast(const std::string& msg) { channel_.Deliver(msg); }

        // Stop Server
        void StopServer();

    public:

        // Client shall extend from this class and will need to override following methods
        virtual void OnServerStarted() = 0;
        virtual void OnServerStopped() = 0;
        virtual void OnClientConnected(const std::string& host, unsigned short port) = 0;
        virtual void OnClientDisconnected(const std::string& host, unsigned short port) = 0;
        virtual void OnReceiveError(const std::string& msg) = 0;
        virtual void OnReceive(const std::string& msg) = 0;

    private:

        void accept();

        char read_terminator = '\n';
        int read_write_timeout = 0;

        boost::asio::io_context& io_context_;
        tcp::acceptor acceptor_;
        Channel channel_;
    };
}

#endif

