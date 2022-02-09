
#include "SampleClient.h"

#include <iostream>
#include <stdio.h>

#include <boost/thread.hpp>

#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT 9091

using namespace SPSocket;

boost::asio::io_context io_context;

int main(int argc, char* argv[])
{
	SampleClient sc(io_context);
	//sc.UseReadUntil();		// config any setting here
	sc.Connect(SERVER_HOST, SERVER_PORT);

	boost::thread sampleThread{ []() { io_context.run(); } };

	while (true)
	{
		std::string line;
		std::getline(std::cin, line);
		line.append("\n");	// tag back along the newline char
		sc.Send(line);

		Sleep(5);	// need to delay some time between getline and send otherwise program will blow up!
	}
	sc.Disconnect();
	io_context.stop();

	return 0;
}