
// Copyright (c) 2003-2021 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Heavy modification by Tim Hsu, Sharp Point Ltd. 2022.

#include "SPSocketServer.h"

namespace SPSocket
{
    TCP_Session::TCP_Session(tcp::socket socket, Channel& ch, SPSocketServerPtr sp)
        : channel_(ch), socket_(std::move(socket)), socket_server_(sp)
    {
        input_deadline_.expires_at(steady_timer::time_point::max());
        output_deadline_.expires_at(steady_timer::time_point::max());

        // The non_empty_output_queue_ steady_timer is set to the maximum time
        // point whenever the output queue is empty. This ensures that the output
        // actor stays asleep until a message is put into the queue.
        non_empty_output_queue_.expires_at(steady_timer::time_point::max());
    }

    void TCP_Session::UseReadUntil(char terminator)
    {
        read_terminator = terminator;
    }

    void TCP_Session::Start()
    {
        channel_.Join(shared_from_this());

        std::string chost = socket_.remote_endpoint().address().to_string();
        unsigned short cport = socket_.remote_endpoint().port();

        socket_server_->OnClientConnected(chost, cport);

        read_line();
        check_deadline(input_deadline_);

        await_output();
        check_deadline(output_deadline_);
    }

    void TCP_Session::stop()
    {
        channel_.Leave(shared_from_this());

        std::string chost = socket_.remote_endpoint().address().to_string();
        unsigned short cport = socket_.remote_endpoint().port();

        socket_server_->OnClientDisconnected(chost, cport);

        boost::system::error_code ignored_error;
        socket_.close(ignored_error);
        input_deadline_.cancel();
        non_empty_output_queue_.cancel();
        output_deadline_.cancel();
    }

    bool TCP_Session::stopped() const
    {
        return !socket_.is_open();
    }

    void TCP_Session::deliver(const std::string& msg)
    {
        output_queue_.push_back(msg);

        // Signal that the output queue contains messages. Modifying the expiry
        // will wake the output actor, if it is waiting on the timer.
        non_empty_output_queue_.expires_at(steady_timer::time_point::min());
    }

    void TCP_Session::read_line()
    {
        // Set a deadline for the read operation.     
        if (rw_timeout > 0)
        {
            input_deadline_.expires_after(std::chrono::seconds(rw_timeout));
        }

        // Start an asynchronous operation to read a newline-delimited message.
        auto self(shared_from_this());
        boost::asio::async_read_until(socket_,
            boost::asio::dynamic_buffer(input_buffer_), read_terminator,
            [this, self](const boost::system::error_code& error, std::size_t n)
        {
            // Check if the session was stopped while the operation was pending.
            if (stopped())
                return;

            if (!error)
            {
                // Extract the delimited message from the buffer.
                std::string str_recv(input_buffer_.substr(0, n - 1));
                input_buffer_.erase(0, n);

                if (!str_recv.empty())
                {
                    socket_server_->OnReceive(str_recv);

                    // Send data to connecting client only
                    //Send(str_recv);
                }
                else
                {
                    // We received a heartbeat message from the client. If there's
                    // nothing else being sent or ready to be sent, send a heartbeat
                    // right back.
                    if (output_queue_.empty())
                    {
                        output_queue_.push_back("HB" + read_terminator);

                        // Signal that the output queue contains messages. Modifying
                        // the expiry will wake the output actor, if it is waiting on
                        // the timer.
                        non_empty_output_queue_.expires_at(steady_timer::time_point::min());
                    }
                }
                read_line();
            }
            else
            {
                socket_server_->OnReceiveError(error.message());
                stop();
            }
        });
    }

    void TCP_Session::await_output()
    {
        auto self(shared_from_this());
        non_empty_output_queue_.async_wait(
            [this, self](const boost::system::error_code& /*error*/)
        {
            // Check if the session was stopped while the operation was pending.
            if (stopped())
                return;

            if (output_queue_.empty())
            {
                // There are no messages that are ready to be sent. The actor goes
                // to sleep by waiting on the non_empty_output_queue_ timer. When a
                // new message is added, the timer will be modified and the actor
                // will wake.
                non_empty_output_queue_.expires_at(steady_timer::time_point::max());
                await_output();
            }
            else
            {
                write_line();
            }
        });
    }

    void TCP_Session::write_line()
    {
        // Set a deadline for the write operation.
        if (rw_timeout > 0)
        {
            output_deadline_.expires_after(std::chrono::seconds(rw_timeout));
        }

        // Start an asynchronous operation to send a message.
        auto self(shared_from_this());
        boost::asio::async_write(socket_,
            boost::asio::buffer(output_queue_.front()),
            [this, self](const boost::system::error_code& error, std::size_t /*n*/)
        {
            // Check if the session was stopped while the operation was pending.
            if (stopped())
                return;

            if (!error)
            {
                output_queue_.pop_front();
                await_output();
            }
            else
            {
                stop();
            }
        });
    }

    void TCP_Session::check_deadline(steady_timer& deadline)
    {
        auto self(shared_from_this());
        deadline.async_wait(
            [this, self, &deadline](const boost::system::error_code& /*error*/)
        {
            // Check if the session was stopped while the operation was pending.
            if (stopped())
                return;

            // Check whether the deadline has passed. We compare the deadline
            // against the current time since a new asynchronous operation may
            // have moved the deadline before this actor had a chance to run.
            if (deadline.expiry() <= steady_timer::clock_type::now())
            {
                // The deadline has passed. Stop the session. The other actors will
                // terminate as soon as possible.
                stop();
            }
            else
            {
                // Put the actor back to sleep.
                check_deadline(deadline);
            }
        });
    }

    //----------------------------------------------------------------------

    UDP_Broadcaster::UDP_Broadcaster(boost::asio::io_context& io_context,
        const udp::endpoint& broadcast_endpoint)
        : socket_(io_context)
    {
        socket_.connect(broadcast_endpoint);
        socket_.set_option(udp::socket::broadcast(true));
    }

    void UDP_Broadcaster::deliver(const std::string& msg)
    {
        boost::system::error_code ignored_error;
        socket_.send(boost::asio::buffer(msg), 0, ignored_error);
    }

    //----------------------------------------------------------------------

    SPSocketServer::SPSocketServer(boost::asio::io_context& io_context,
        const tcp::endpoint& listen_endpoint,
        const udp::endpoint& broadcast_endpoint)
        : io_context_(io_context),
        acceptor_(io_context, listen_endpoint)
    {
        channel_.Join(std::make_shared<UDP_Broadcaster>(io_context_, broadcast_endpoint));        
    }

    void SPSocketServer::accept()
    {
        acceptor_.async_accept(
            [this](const boost::system::error_code& error, tcp::socket socket)
        {
            if (!error)
            {
                auto tcp_ptr = std::make_shared<TCP_Session>(std::move(socket), channel_, this);
                tcp_ptr->UseReadUntil(read_terminator);
                tcp_ptr->UseReadWriteTimeOut(read_write_timeout);
                tcp_ptr->Start();
            }
            accept();
        });
    }

    void SPSocketServer::StopServer()
    {
        if (acceptor_.is_open())
        {
            acceptor_.cancel();
            acceptor_.close();
        }
        OnServerStopped();
    }
}