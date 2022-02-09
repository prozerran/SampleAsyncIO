
#include <stdio.h>
#include "SampleServer.h"

#include <boost/thread.hpp>

#define SERVER_HOST "127.0.0.1"
#define SERVER_PORT 9091

using namespace SPSocket;

boost::asio::io_context io_context;

int main(int argc, char* argv[])
{
    tcp::endpoint listen_endpoint(tcp::v4(), SERVER_PORT);
    udp::endpoint broadcast_endpoint(boost::asio::ip::make_address(SERVER_HOST), 0);

    SampleServer ss(io_context, listen_endpoint, broadcast_endpoint);
    ss.StartServer();

    boost::thread sampleThread{ []() { io_context.run(); } };

    while (true)
    {
        std::string line;
        std::getline(std::cin, line);
        line.append("\n");	// tag back along the newline char
        ss.BroadCast(line);

        Sleep(5);	// need to delay some time between getline and send otherwise program will blow up!
    }
    ss.StopServer();
    io_context.stop();

    return 0;
}