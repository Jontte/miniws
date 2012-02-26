#ifndef SERVER_H_INCLUDED_
#define SERVER_H_INCLUDED_

#include <boost/asio.hpp>
#include <boost/make_shared.hpp>
#include <boost/thread/recursive_mutex.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <set>
#include <map>
#include <string>
#include <deque>

namespace WS
{
using boost::asio::ip::tcp;
	
typedef boost::asio::buffers_iterator<boost::asio::streambuf::const_buffers_type> buffer_iterator;
typedef boost::shared_ptr< std::vector<char> > MessagePtr;

struct Frame;
class Connection;
class Session;
class Server;

typedef boost::shared_ptr<Connection> ConnectionPtr;

struct BaseFactory
{
	virtual Session* make_session() = 0;
};
template <class T>
struct SessionFactory : public BaseFactory
{
	virtual T* make_session() {
		return new T;
	}
};

class Session
{
	friend class Connection;
private:
	boost::weak_ptr<Connection> m_connection;
protected:
public:
	void send(const std::string& m);

	virtual void on_message(const std::string& m) = 0;
};

class Server : public boost::noncopyable
{
	private:
	
	tcp::acceptor acceptor_;

	boost::recursive_mutex m_connections_mutex;
	std::set<ConnectionPtr> m_connections;
	std::map<std::string, boost::shared_ptr<BaseFactory> > m_factories;
	
	void stop_listen();
	void start_listen();

	void handle_accept(ConnectionPtr new_connection,
		const boost::system::error_code& error);
	
	protected:

	public:

	void prune(ConnectionPtr);
	Server(boost::asio::io_service& io_service, int port);

	void get_peers(const std::string& resource, std::set<boost::shared_ptr<Session> >& out);
	boost::shared_ptr<BaseFactory> get_factory(const std::string& resource);

	template <class T>
	void handle_resource(const std::string res)
	{
		m_factories[res] = boost::make_shared<SessionFactory<T> >();
	}
};


class Connection
	: public boost::enable_shared_from_this<Connection>
{
	friend class Server;
private:
	tcp::socket socket_;
	boost::asio::io_service::strand strand_;
	boost::asio::streambuf buffer_;
	boost::asio::streambuf fragment_; // fragmented pieces written here

	std::map<std::string, std::string> m_headers;
	std::string m_method;
	std::string m_resource;
	std::string m_http_version;
	bool m_parsing_headers;
	bool m_active;

	Server * m_owner; // NO OWNERSHIP! The server always outlives 

	uint64_t m_bytes_sent;
	uint64_t m_bytes_received;
	
	enum {
		PROTOCOL_HYBI_08, 
		PROTOCOL_HYBI_13,
		PROTOCOL_INDETERMINATE
	} m_protocol_version;
	
	std::deque< MessagePtr > m_outbox;
		
	void handle_write(const boost::system::error_code& /*error*/,
		size_t /*bytes_transferred*/,
		MessagePtr /*buffer keepalive handle*/);
	void handle_read(const boost::system::error_code& error, size_t bytes_transferred);
	
	void async_read();
	
	bool validate_headers();
	void send_handshake();
	void parse_header(const std::string& line);
	void process(Frame& f); 
	
	// strand'ed, thread safe call:
	void close_impl();
	
	// outbox-related function calls
	void write_impl(MessagePtr);
	void write_socket_impl();
	
	struct
	{
		uint8_t data;
		boost::posix_time::ptime time;
	} m_ping;

	boost::shared_ptr<Session> m_session;
	
protected:
	void ping();
	
public:
	
	Connection(
		boost::asio::io_service& io_service,
		Server* );
	
	std::pair<buffer_iterator, bool> buffer_ready_condition(buffer_iterator begin, buffer_iterator end);

	bool have_header(const std::string& q) const;
	std::string get_header(const std::string& q) const;
	std::string get_method() const;
	std::string get_resource() const;
	std::string get_http_version() const;
	tcp::socket& socket();	
	void close();

	boost::shared_ptr<Session> get_session() const;

	// encapsulated data
	void write_text(const std::string& message);

	// raw data	
	void write_raw(const std::string& message);
	void write_raw(MessagePtr msg);
	
	Server& get_server() const
	{
		assert(m_owner);
		return *m_owner;
	}
};

};


#endif
