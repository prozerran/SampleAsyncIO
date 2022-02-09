
#ifndef SAMPLE_SERVER_H
#define SAMPLE_SERVER_H

#include "../SRC/SPSocketServer.h"

namespace SPSocket
{
	class SampleServer : public SPSocketServer {
	public:

		explicit SampleServer(boost::asio::io_context& io_context, const tcp::endpoint& listen_endpoint, const udp::endpoint& broadcast_endpoint)
			: SPSocketServer(io_context, listen_endpoint, broadcast_endpoint)
		{ };

		virtual ~SampleServer() noexcept
		{ };

		void OnServerStarted() override;
		void OnServerStopped() override;
		void OnClientConnected(const std::string& host, unsigned short port) override;
		void OnClientDisconnected(const std::string& host, unsigned short port) override;
		void OnReceiveError(const std::string& msg) override;
		void OnReceive(const std::string& msg) override;
	};
}

#endif
