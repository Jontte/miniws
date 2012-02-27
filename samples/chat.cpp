#include <iostream>
#include "../server.h"
#include <boost/thread.hpp>

using namespace std;

class Session : public WS::SessionWrap<Session>
{
	void on_message(const string& msg)
	{
		// Send incoming msg to all peers..

		std::vector<boost::shared_ptr<Session> > peers;
		get_peers(peers);

		for( std::vector<boost::shared_ptr<Session> >::iterator iter = peers.begin();
			 iter != peers.end();
			 iter++)
		{
			(*iter)->send(msg);
		}
	}
};

void ping_thread(WS::Server& server)
{
	// Send pointless messages to all clients every ten seconds

	while(true)
	{
		sleep(10);

		std::vector<WS::SessionPtr> peers;
		server.get_peers("/chat", peers);

		for(std::vector<WS::SessionPtr>::iterator iter = peers.begin();iter != peers.end(); iter++)
		{
			(*iter)->send("kissa");
		}
	}
}

int main()
{
	boost::asio::io_service service;
	
	WS::Server server(service, 8080);
	server.handle_resource<Session>("/chat");

	boost::thread thread(ping_thread, boost::ref(server));

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
}

