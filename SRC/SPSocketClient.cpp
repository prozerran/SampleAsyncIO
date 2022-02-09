
// Copyright (c) 2003-2021 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// Heavy modification by Tim Hsu, Sharp Point Ltd. 2022.

#include "SPSocketClient.h"

#include <iostream>

namespace SPSocket
{
	void SPSocketClient::Connect(const std::string& host, int port)
	{
		status = ConnectionStatus::S_CLOSED;
		start(resolver_.resolve(host, std::to_string(port)));
	}

	void SPSocketClient::start(tcp::resolver::results_type endpoints)
	{
		// Start the connect actor.
		endpoints_ = endpoints;
		start_connect(endpoints_.begin());

		// Start the deadline actor. You will note that we're not setting any
		// particular deadline here. Instead, the connect and input actors will
		// update the deadline prior to each asynchronous operation.
		deadline_.async_wait(std::bind(&SPSocketClient::check_deadline, this, _1));
	}

	void SPSocketClient::UseReadUntil(char terminator)
	{
		use_read_until = true;
		read_terminator = terminator;
	}

	void SPSocketClient::UseSendHeartBeat(int sec_interval, const std::string& heartbeat)
	{
		hb_interval = sec_interval;
		heartbeat_str_ = heartbeat;
	}

	void SPSocketClient::Disconnect()
	{
		if (running_)
		{
			status = ConnectionStatus::S_CLOSED;
			boost::system::error_code ignored_error;
			socket_.close(ignored_error);
			deadline_.cancel();
			heartbeat_timer_.cancel();

			OnDisconnected();
			running_ = false;
		}
	}

	void SPSocketClient::start_async_reading()
	{
		if (!IsConnected())
			return;

		if (!running_)
		{
			running_ = true;

			if (use_read_until)
				start_read_until();
			else
				start_read();

			if (heartbeat_str_.length() > 0)
				send_heartbeat();
		}
	}

	void SPSocketClient::start_connect(tcp::resolver::results_type::iterator endpoint_iter)
	{
		if (endpoint_iter != endpoints_.end())
		{
			status = ConnectionStatus::S_CONNECTING;
			OnConnecting(endpoint_iter->endpoint());

			// Set a deadline for the connect operation.
			if (read_timeout > 0)
			{
				deadline_.expires_after(std::chrono::seconds(read_timeout));
			}

			// Start the asynchronous connect operation.
			socket_.async_connect(endpoint_iter->endpoint(),
				std::bind(&SPSocketClient::handle_connect,
					this, _1, endpoint_iter));
		}
		else
		{
			// There are no more endpoints to try. Shut down the client.
			Disconnect();
		}
	}

	void SPSocketClient::handle_connect(const boost::system::error_code& error,
		tcp::resolver::results_type::iterator endpoint_iter)
	{
		// The async_connect() function automatically opens the socket at the start
		// of the asynchronous operation. If the socket is closed at this time then
		// the timeout handler must have run first.
		if (!socket_.is_open())
		{
			status = ConnectionStatus::S_CONNECT_ERROR;
			OnConnectTimedOut(endpoint_iter->endpoint());			
			return;

			// Try the next available endpoint.
			//start_connect(++endpoint_iter);
		}

		// Check if the connect operation failed before the deadline expired.
		else if (error)
		{
			status = ConnectionStatus::S_CONNECT_ERROR;
			OnConnectionError(error.message());

			// We need to close the socket used in the previous connection attempt
			// before starting a new one.
			socket_.close();

			return;
			// Try the next available endpoint.
			//start_connect(++endpoint_iter);
		}

		// Otherwise we have successfully established a connection.
		else
		{
			status = ConnectionStatus::S_CONNECTED;
			OnConnected(endpoint_iter->endpoint());

			// Start the input actor.
			start_async_reading();
		}
	}

	void SPSocketClient::start_read()
	{
		if (!IsConnected())
			return;
		
		if (read_timeout > 0)
		{
			// Set a deadline for the read operation.
			deadline_.expires_after(std::chrono::seconds(read_timeout));
		}

		// simulate memset 0
		recv_buffer_.consume(recv_buffer_.size() + 1);

		boost::asio::async_read(socket_, recv_buffer_,
			boost::asio::transfer_at_least(1),
			std::bind(&SPSocketClient::handle_read, this, _1, _2));
	}

	void SPSocketClient::start_read_until()
	{
		if (!IsConnected())
			return;

		if (read_timeout > 0)
		{
			// Set a deadline for the read operation.
			deadline_.expires_after(std::chrono::seconds(read_timeout));
		}	

		// Start an asynchronous operation to read a newline-delimited message.
		boost::asio::async_read_until(socket_,
			boost::asio::dynamic_buffer(input_buffer_), read_terminator,
			std::bind(&SPSocketClient::handle_read_until, this, _1, _2));
	}

	void SPSocketClient::handle_read(const boost::system::error_code& error, std::size_t n)
	{
		if (!error)
		{
			// Empty messages are heartbeats and so ignored.
			if (n > 0)
			{
				boost::asio::streambuf::const_buffers_type bufs = recv_buffer_.data();
				std::string str(boost::asio::buffers_begin(bufs),
					boost::asio::buffers_begin(bufs) + recv_buffer_.size());

				// Extract the delimited message from the buffer.
				std::string str_recv(str.substr(0, n - 1));

				if (use_recv_polling)
					push(str_recv);
				else
					OnReceive(str_recv);
			}
			start_read();
		}
		else
		{
			OnReceiveError(error.message());
			Disconnect();
		}
	}

	void SPSocketClient::handle_read_until(const boost::system::error_code& error, std::size_t n)
	{
		if (!error)
		{
			// Extract the delimited message from the buffer.
			std::string str_recv(input_buffer_.substr(0, n - 1));
			input_buffer_.erase(0, n);

			// Empty messages are heartbeats and so ignored.
			if (!str_recv.empty())
			{
				//std::thread([=] { OnReceive(str_recv); }).detach();		// use async callback instead, maybe dangerous
				if (use_recv_polling)
					push(str_recv);
				else
					OnReceive(str_recv);
			}
			start_read_until();
		}
		else
		{
			OnReceiveError(error.message());
			Disconnect();
		}
	}

	void SPSocketClient::send(const std::string& content)
	{
		if (!IsConnected())
			return;

		// Start an asynchronous operation to send a heartbeat message.
		boost::asio::async_write(socket_, boost::asio::buffer(content, content.length()),
			std::bind(&SPSocketClient::handle_send, this, _1));
	}

	void SPSocketClient::handle_send(const boost::system::error_code& error)
	{
		if (error)
		{
			OnSendError(error.message());
			Disconnect();
		}
	}

	void SPSocketClient::send_heartbeat()
	{
		if (!IsConnected())
			return;

		// Start an asynchronous operation to send a heartbeat message.
		boost::asio::async_write(socket_, boost::asio::buffer(heartbeat_str_, heartbeat_str_.length()),
			std::bind(&SPSocketClient::handle_send_heartbeat, this, _1));
	}

	void SPSocketClient::handle_send_heartbeat(const boost::system::error_code& error)
	{
		if (!error)
		{
			// Wait X seconds before sending the next heartbeat.
			heartbeat_timer_.expires_after(std::chrono::seconds(hb_interval));
			heartbeat_timer_.async_wait(std::bind(&SPSocketClient::send_heartbeat, this));
		}
		else
		{
			OnHeartBeatError(error.message());
			Disconnect();
		}
	}

	void SPSocketClient::check_deadline(const boost::system::error_code& error)
	{
		// Check whether the deadline has passed. We compare the deadline against
		// the current time since a new asynchronous operation may have moved the
		// deadline before this actor had a chance to run.
		if (deadline_.expiry() <= steady_timer::clock_type::now())
		{
			if (error)
			{
				OnReceiveTimeOut(error.message());

				// The deadline has passed. The socket is closed so that any outstanding
				// asynchronous operations are cancelled.
				socket_.close();
			}

			// There is no longer an active deadline. The expiry is set to the
			// maximum time point so that the actor takes no action until a new
			// deadline is set.
			deadline_.expires_at(steady_timer::time_point::max());
		}

		// Put the actor back to sleep.
		deadline_.async_wait(std::bind(&SPSocketClient::check_deadline, this, _1));
	}

	void SPSocketClient::push(const std::string& data)
	{
		std::lock_guard<std::mutex> lock(mtx_);
		recv_queue_.push(data);
	}

	void SPSocketClient::pop()
	{
		std::string data = "";
		{	// RAII
			std::lock_guard<std::mutex> lock(mtx_);
			if (recv_queue_.empty()) return;
			data = recv_queue_.front();
			recv_queue_.pop();
		}
		OnReceive(data);			
	}
}

