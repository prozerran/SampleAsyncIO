
#include "SampleServer.h"

#include <iostream>

namespace SPSocket
{
	void SampleServer::OnServerStarted()
	{
		std::cout << "Sample Server started. Press 'CTRL-C' to quit." << std::endl;
	}

	void SampleServer::OnServerStopped()
	{
		std::cout << "OnServerStopped" << std::endl;
	}

	void SampleServer::OnClientConnected(const std::string& host, unsigned short port)
	{
		std::cout << "OnClientConnected: " << "[" << host << ":" << port << "]" << std::endl;
	}

	void SampleServer::OnClientDisconnected(const std::string& host, unsigned short port)
	{
		std::cout << "OnClientDisconnected: " << "[" << host << ":" << port << "]" << std::endl;
	}

	void SampleServer::OnReceiveError(const std::string& msg)
	{
		std::cout << "OnReceiveError:" << msg.c_str() << std::endl;
	}

	void SampleServer::OnReceive(const std::string& msg)
	{
		std::cout << "OnReceive:" << msg.c_str() << std::endl;
	}
}