
#include "SampleClient.h"

#include <iostream>

namespace SPSocket
{
	void SampleClient::OnConnecting(const endpoint_type& ep)
	{
		//std::cout << "OnConnecting" << std::endl;
	}

	void SampleClient::OnConnected(const endpoint_type& ep)
	{
		std::cout << "Sample Client started. Press 'CTRL-C' to quit." << std::endl;
	}

	void SampleClient::OnConnectTimedOut(const endpoint_type& ep)
	{
		std::cout << "OnConnectTimedOut" << std::endl;
	}

	void SampleClient::OnConnectionError(const std::string& msg)
	{
		std::cout << "OnConnectionError:" << msg.c_str() << std::endl;
	}

	void SampleClient::OnHeartBeatError(const std::string& msg)
	{ }

	void SampleClient::OnReceiveTimeOut(const std::string& msg)
	{
		std::cout << "OnReceiveTimeOut:" << msg.c_str() << std::endl;
	}

	void SampleClient::OnReceiveError(const std::string& msg)
	{
		std::cout << "OnReceiveError:" << msg.c_str() << std::endl;
	}

	void SampleClient::OnSendError(const std::string& msg)
	{ }

	void SampleClient::OnDisconnected()
	{
		std::cout << "OnDisconnected" << std::endl;
	}

	void SampleClient::OnReceive(const std::string& msg)
	{
		std::cout << "OnReceive:" << msg.c_str() << std::endl;
	}
}