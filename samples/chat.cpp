#include <iostream>
#include "../server.h"
#include "../session.h"
#include <boost/thread.hpp>

using namespace std;

class Session : public WS::SessionWrap<Session>
{
	void on_message(const string& msg)
	{
		// Send incoming msg to all peers..
		for( auto p : get_peers())
			p->write(msg);
	}
};

void ping_thread(std::shared_ptr<WS::Server> server)
{
	// Send pointless messages to all clients every ten seconds

	while(true)
	{
		sleep(10);
		for( auto p : server->get_peers("/chat"))
			p->write("kissa");
	}
}

int main()
{
	boost::asio::io_service service;

	auto server = WS::Server::create(service, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 8080));
	server->handle_resource<Session>("/chat");

//	boost::thread thread(ping_thread, server);

	while(true)
	{
		try
		{
			service.run();
		}
		catch(std::exception& e)
		{
			cout << e.what() << endl;
		}
	}
//	thread.join();
}

