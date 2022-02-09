
#ifndef SAMPLE_CLIENT_H
#define SAMPLE_CLIENT_H

#include "../SRC/SPSocketClient.h"

namespace SPSocket
{
	class SampleClient : public SPSocketClient {
	public:

		explicit SampleClient(boost::asio::io_context& io_context)
			: SPSocketClient(io_context)
		{ };

		virtual ~SampleClient() noexcept 
		{ };

		void OnConnecting(const endpoint_type& ep) override;
		void OnConnected(const endpoint_type& ep) override;
		void OnConnectTimedOut(const endpoint_type& ep) override;
		void OnConnectionError(const std::string& msg) override;
		void OnHeartBeatError(const std::string& msg) override;
		void OnReceiveTimeOut(const std::string& msg) override;
		void OnReceiveError(const std::string& msg) override;
		void OnReceive(const std::string& msg) override;
		void OnSendError(const std::string& msg) override;
		void OnDisconnected() override;
	};
}

#endif
