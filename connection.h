#ifndef CONNECTION_H_INCLUDED_
#define CONNECTION_H_INCLUDED_
#include "server.h"

class MyServer;
class Room;

class Connection : public WS::BasicConnection
{
	friend class MyServer;
	
	private:
	// Connection source.
	string m_source;
	
	MyServer& server()
	{
		return *(MyServer*)get_server<Connection>();
	}
	shared_ptr<Connection> shared_from_this()
	{
		return boost::static_pointer_cast<Connection>(WS::BasicConnection::shared_from_this());
	}
	
	public:
	
	Connection(boost::asio::io_service& io_service,
		WS::BasicServer* server);
	
	// Callbacks
	bool on_validate_headers();
	void on_connect();
	void on_disconnect();
	void on_message(const string& msg);

	// Commands
	void write(const Json::Value& val);
};

#endif

