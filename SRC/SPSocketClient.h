
#ifndef _SP_SOCKET_CLIENT_H_
#define _SP_SOCKET_CLIENT_H_

#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/read_until.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/write.hpp>
//#include <boost/bind.hpp>

#include <functional>
#include <queue>
#include <string>

// https://www.boost.org/doc/libs/1_78_0/doc/html/boost_asio/example/cpp11/timeouts/async_tcp_client.cpp

namespace SPSocket
{
	typedef boost::asio::ip::basic_endpoint<boost::asio::ip::tcp> endpoint_type;

	using boost::asio::steady_timer;
	using boost::asio::ip::tcp;
	using std::placeholders::_1;
	using std::placeholders::_2;

	enum class ConnectionStatus
	{
		S_NOT_CONNECTED,
		S_CONNECTING,
		S_CONNECTED,
		S_CLOSING,
		S_CLOSED,
		S_CONNECT_ERROR,
		S_CONNECT_TIMED_OUT
	};

	//
	// This class manages socket timeouts by applying the concept of a deadline.
	// Some asynchronous operations are given deadlines by which they must complete.
	// Deadlines are enforced by an "actor" that persists for the lifetime of the
	// client object:
	//
	//  +----------------+
	//  |                |
	//  | check_deadline |<---+
	//  |                |    |
	//  +----------------+    | async_wait()
	//              |         |
	//              +---------+
	//
	// If the deadline actor determines that the deadline has expired, the socket
	// is closed and any outstanding operations are consequently cancelled.
	//
	// Connection establishment involves trying each endpoint in turn until a
	// connection is successful, or the available endpoints are exhausted. If the
	// deadline actor closes the socket, the connect actor is woken up and moves to
	// the next endpoint.
	//
	//  +---------------+
	//  |               |
	//  | start_connect |<---+
	//  |               |    |
	//  +---------------+    |
	//           |           |
	//  async_-  |    +----------------+
	// connect() |    |                |
	//           +--->| handle_connect |
	//                |                |
	//                +----------------+
	//                          :
	// Once a connection is     :
	// made, the connect        :
	// actor forks in two -     :
	//                          :
	// an actor for reading     :       and an actor for
	// inbound messages:        :       sending heartbeats:
	//                          :
	//  +------------+          :          +-------------+
	//  |            |<- - - - -+- - - - ->|             |
	//  | start_read |                     | start_write |<---+
	//  |            |<---+                |             |    |
	//  +------------+    |                +-------------+    | async_wait()
	//          |         |                        |          |
	//  async_- |    +-------------+       async_- |    +--------------+
	//   read_- |    |             |       write() |    |              |
	//  until() +--->| handle_read |               +--->| handle_write |
	//               |             |                    |              |
	//               +-------------+                    +--------------+
	//
	// The input actor reads messages from the socket, where messages are delimited
	// by the newline character. The deadline for a complete message is 30 seconds.
	//
	// The heartbeat actor sends a heartbeat (a message that consists of a single
	// newline character) every 10 seconds. In this example, no deadline is applied
	// to message sending.
	//

	class SPSocketClient {
	public:

		explicit SPSocketClient(boost::asio::io_context& io_context) : 
			resolver_(io_context),
			socket_(io_context), 
			deadline_(io_context),
			heartbeat_timer_(io_context),
			recv_buffer_(), recv_queue_(), mtx_(), 
			status(ConnectionStatus::S_NOT_CONNECTED)
		{};

		virtual ~SPSocketClient() noexcept {};

		// Called by the user of the client class to initiate the connection process.
		void Connect(const std::string& host, int port);

		// Async read until terminator detected, return string via OnReceive
		void UseReadUntil(char terminator = '\n');

		// Read timeout value in seconds, 0 = infinite (default)
		void UseReadTimeOut(int recv_timeout_sec) { read_timeout = recv_timeout_sec; }

		// Sends heartbeat periodically to server
		void UseSendHeartBeat(int sec_interval, const std::string& heartbeat = "\n");

		// If true, receiving data will no longer push data to OnReceive(), instead, user needs to manually
		// Poll() for data, which then can be read in OnReceive()
		void UsePollingToReceive(bool flag) { use_recv_polling = flag; }

		// Determines is there is a connected socket
		bool IsConnected() const { return status == ConnectionStatus::S_CONNECTED; }

		// Sends data over network to server
		void Send(const std::vector<char>& buf) { std::string str(buf.data(), buf.size()); Send(str); }
		void Send(const char* buf, size_t size) { std::string str(buf, size); Send(str); }
		void Send(const std::string& content) { send(content); }

		// Polls for data received through socket through OnReceive()
		void Poll() { pop(); }

		// This function terminates all the actors to shut down the connection. It
		// may be called by the user of the client class, or by the class itself in
		// response to graceful termination or an unrecoverable error.
		void Disconnect();

		// Gets current connection status
		ConnectionStatus GetConnectionStatus() { return status; }

	public:

		// Client shall extend from this class and will need to override following methods
		virtual void OnConnecting(const endpoint_type& ep) = 0;
		virtual void OnConnected(const endpoint_type& ep) = 0;
		virtual void OnConnectTimedOut(const endpoint_type& ep) = 0;		
		virtual void OnConnectionError(const std::string& msg) = 0;
		virtual void OnHeartBeatError(const std::string& msg) = 0;
		virtual void OnReceiveTimeOut(const std::string& msg) = 0;
		virtual void OnReceiveError(const std::string& msg) = 0;
		virtual void OnReceive(const std::string& msg) = 0;	
		virtual void OnSendError(const std::string& msg) = 0;
		virtual void OnDisconnected() = 0;

	private:

		// The endpoints will have been obtained using a tcp::resolver.
		void start(tcp::resolver::results_type endpoints);
		void start_connect(tcp::resolver::results_type::iterator endpoint_iter);
		void handle_connect(const boost::system::error_code& error,
			tcp::resolver::results_type::iterator endpoint_iter);

		void start_read();
		void start_read_until();
		void start_async_reading();
		void handle_read(const boost::system::error_code& error, std::size_t n);
		void handle_read_until(const boost::system::error_code& error, std::size_t n);

		void send(const std::string& content);
		void send_heartbeat();

		void handle_send(const boost::system::error_code& error);
		void handle_send_heartbeat(const boost::system::error_code& error);

		void push(const std::string& data);		// enqueues
		void pop();								// dequeues

		void check_deadline(const boost::system::error_code& error);

	private:

		ConnectionStatus status;

		bool running_ = false;
		bool use_read_until = false;
		bool use_recv_polling = false;
		
		char read_terminator = '\n';
		int read_timeout = 0;
		int hb_interval = 30;

		std::string heartbeat_str_ = "";
		std::string input_buffer_ = "";
		std::queue<std::string> recv_queue_;

		tcp::resolver resolver_;
		tcp::resolver::results_type endpoints_;
		tcp::socket socket_;

		steady_timer deadline_;
		steady_timer heartbeat_timer_;

		std::mutex mtx_;

		boost::asio::streambuf recv_buffer_;
	};
}

#endif // ! _SP_SOCKET_CLIENT_H_