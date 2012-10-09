#include <iostream>
#include "server.h"
#include "connection.h"
#include "session.h"

namespace WS {

Server::Server(boost::asio::io_service& io_service, tcp::endpoint endpoint)
	: acceptor_(io_service, endpoint)
{
	// Server::create() calls start_listen()
}
Server::~Server()
{
    stop_listen();
}
void Server::stop_listen()
{
	acceptor_.close();
}

std::vector<std::shared_ptr<Session> > Server::get_peers(const std::string& resource)
{
	boost::lock_guard<boost::recursive_mutex> lock(m_connections_mutex);
	std::vector<std::shared_ptr<Session> > out;

	for(auto iter = m_connections.begin(); iter != m_connections.end(); )
	{
		WeakConnectionPtr weak= *iter;
		try
		{
			auto con = weak.lock();
			if(con->get_resource() == resource && con->get_session())
				out.push_back(con->get_session());
		}
		catch(const std::bad_weak_ptr&)
		{
			iter = m_connections.erase(iter);
			continue;
		}
		++iter;
	}
	return out;
}

void Server::start_listen()
{
	ConnectionPtr new_connection(new Connection(std::ref(acceptor_.get_io_service()), shared_from_this()));

	acceptor_.async_accept(new_connection->socket(),
		std::bind(&Server::handle_accept, shared_from_this(), new_connection, std::placeholders::_1));
}

void Server::handle_accept(ConnectionPtr new_connection,
					const boost::system::error_code& error)
{
	if (!error)
	{
		new_connection->async_read(); // refcount +1

		boost::lock_guard<boost::recursive_mutex> lock(m_connections_mutex);
		m_connections.push_back(new_connection);
	}
	start_listen();
}
std::shared_ptr<BaseFactory> Server::get_factory(const std::string& resource)
{
	std::map<std::string, std::shared_ptr<BaseFactory> >::iterator iter = m_factories.find(resource);
	if(iter == m_factories.end())
		return std::shared_ptr<BaseFactory>();
	return iter->second;
}

std::shared_ptr< Server > Server::create ( boost::asio::io_service& io_service, tcp::endpoint endpoint )
{
	std::shared_ptr<Server> ptr(new Server(io_service, endpoint));
	ptr->start_listen(); // cannot be called in ctor since shared_from_this won't work there
	return ptr;
}

}
