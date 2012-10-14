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

	protected:
	Server( boost::asio::io_service& io_service, boost::asio::ip::tcp::endpoint endpoint );

	public:

	virtual void on_connect(const std::string& resource, std::shared_ptr<Session> session) {}
	virtual void on_disconnect(const std::string& resource, std::shared_ptr<Session> session) {}

	virtual ~Server();

	std::shared_ptr<BaseFactory> get_factory(const std::string& resource);

	template<class T = Server>
	static std::shared_ptr<T> create(boost::asio::io_service& io_service, boost::asio::ip::tcp::endpoint endpoint)
	{
		std::shared_ptr<T> ptr(new T(io_service, endpoint));
		ptr->start_listen(); // cannot be called in ctor since shared_from_this won't work there
		return ptr;
	}

	std::vector< std::shared_ptr< Session > > get_peers( const std::string& resource );

	template <class T>
	void handle_resource(const std::string& res)
	{
		m_factories[res] = std::make_shared<SessionFactory<T> >();
	}

	template <class T>
	std::vector<std::shared_ptr<T> > get_peers(const std::string& res)
	{
		auto peers = get_peers(res);
		std::vector<std::shared_ptr<T> > ret;
		ret.reserve(peers.size());
		for(auto& peer : peers)
			ret.push_back(std::static_pointer_cast<T>(peer));
		return ret;
	}
};

};

#endif
