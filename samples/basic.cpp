#include <iostream>
#include "../server.h"
#include "../session.h"

using namespace std;

class Session : public WS::Session
{
	void on_message(const string& msg)
	{
		cout << "Incoming message: " << msg << endl;
		write("Foobar");
	}
};

int main()
{
	boost::asio::io_service service;

	auto server = WS::Server::create<>(service, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 8080));
	server->handle_resource<Session>("/basic");

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

