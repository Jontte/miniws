#include <iostream>
#include "../server.h"
#include <boost/thread.hpp>

using namespace std;

class Session : public WS::Session
{
	void on_message(const string& msg)
	{
		// Send incoming msg to all peers..

//		std::set<boost::shared_ptr<Session> > peers = get_peers<Session>();
//		for(auto iter = peers.begin());iter != peers.end(); iter++)
//		{
//			iter->send(msg);
//		}
	}
};

void ping_thread(WS::Server& server)
{
	// Send pointless messages to all clients every second

	while(true)
	{
		sleep(1);

		std::set<boost::shared_ptr<WS::Session> > peers;
		server.get_peers("/chat", peers);

		for(std::set<boost::shared_ptr<WS::Session> >::iterator iter = peers.begin();iter != peers.end(); iter++)
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

