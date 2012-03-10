#include <iostream>
#include "../server.h"

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
	
	WS::Server server(service, 8080);
	server.handle_resource<Session>("/basic");

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

