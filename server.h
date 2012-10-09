#ifndef SERVER_H_INCLUDED_
#define SERVER_H_INCLUDED_

#include <boost/asio.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <list>
#include <map>
#include <string>
#include <deque>
#include <functional>
#include <memory>

#include "frame.h"

namespace WS
{

using boost::asio::ip::tcp;

class Connection;
class Session;
class Server;

typedef std::shared_ptr<Connection> ConnectionPtr;
typedef std::weak_ptr<Connection> WeakConnectionPtr;
typedef std::shared_ptr<Session> SessionPtr;

struct Frame;

struct BaseFactory
{
	virtual SessionPtr make_session() = 0;
};

template <class T>
struct SessionFactory : public BaseFactory
{
	virtual SessionPtr make_session() {
		return std::make_shared<T>();
	}
};


class Server
	: public std::enable_shared_from_this<Server>
	, public boost::noncopyable
{
	private:

	tcp::acceptor acceptor_;

	boost::recursive_mutex m_connections_mutex;
	std::list<WeakConnectionPtr> m_connections;
	std::map<std::string, std::shared_ptr<BaseFactory> > m_factories;

	void start_listen();
	void stop_listen();

	void handle_accept(ConnectionPtr new_connection,
		const boost::system::error_code& error);

	void prune(ConnectionPtr);

	// Keep ctor private, use factory instead
	Server( boost::asio::io_service& io_service, boost::asio::ip::tcp::endpoint endpoint );

	public:

	~Server();

	std::shared_ptr<BaseFactory> get_factory(const std::string& resource);

	static std::shared_ptr<Server> create(boost::asio::io_service& io_service, boost::asio::ip::tcp::endpoint endpoint);

	std::vector< std::shared_ptr< Session > > get_peers( const std::string& resource );

	template <class T>
	void handle_resource(const std::string& res)
	{
		m_factories[res] = std::make_shared<SessionFactory<T> >();
	}
};

};

#endif
