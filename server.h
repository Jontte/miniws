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

#include "frame.h"

namespace WS
{

using boost::asio::ip::tcp;

class Connection;
class Session;
class Server;

typedef boost::shared_ptr<Connection> ConnectionPtr;
typedef boost::shared_ptr<Session> SessionPtr;

struct Frame;

struct BaseFactory
{
	virtual SessionPtr make_session() = 0;
};

template <class T>
struct SessionFactory : public BaseFactory
{
	virtual SessionPtr make_session() {
		return boost::make_shared<T>();
	}
};


class Server : public boost::noncopyable
{
	private:
	
	tcp::acceptor acceptor_;

	boost::recursive_mutex m_connections_mutex;
	std::set<ConnectionPtr> m_connections;
	std::map<std::string, boost::shared_ptr<BaseFactory> > m_factories;
	
	void start_listen();

	void handle_accept(ConnectionPtr new_connection,
		const boost::system::error_code& error);


	public:

	Server(boost::asio::io_service& io_service, int port);
	void prune(ConnectionPtr);
	void stop_listen();

	void get_peers(const std::string& resource, std::vector<SessionPtr>& out);
	boost::shared_ptr<BaseFactory> get_factory(const std::string& resource);

	template <class T>
	void handle_resource(const std::string& res)
	{
		m_factories[res] = boost::make_shared<SessionFactory<T> >();
	}

  virtual void on_connect(SessionPtr){};
  virtual void on_disconnect(SessionPtr){};
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
	void process(Frame& header);
	
	// strand'ed, thread safe call:
	void close_impl();
	
	// outbox-related function calls
	void write_impl(MessagePtr);
	void write_many_impl(std::vector<MessagePtr>);
	void write_socket_impl();
	
	struct
	{
		uint8_t data;
		boost::posix_time::ptime time;
	} m_ping;

	boost::shared_ptr<Session> m_session;
	
	Connection(
		boost::asio::io_service& io_service,
		Server* );

public:

  template <class T>
  void post(const T& t)
  {
    strand_.post(t);
  }
	
	
	Server& get_server() const
	{
		return *m_owner;
	}
	
	void ping();
	
	std::pair<buffer_iterator, bool>
	buffer_ready_condition(
		buffer_iterator begin,
		buffer_iterator end
	);

	bool have_header(const std::string& q) const;
	std::string get_header(const std::string& q) const;
	std::string get_method() const;
	std::string get_resource() const;
	std::string get_http_version() const;
	tcp::socket& socket();	
	void close();

	SessionPtr get_session() const;

	// encapsulated data
	void write_text(const std::string& message);

	// raw data	
	void write_raw(const std::string& message);
	void write_raw(MessagePtr msg);
	void write_raw(const std::vector<MessagePtr>& msg);
};

class Session
{
	friend class Connection;
private:
	boost::weak_ptr<Connection> m_connection;
protected:
  template <class T>
  void post(T t)
  {
   try
   {
    boost::shared_ptr<Connection> con(m_connection);
    con->post(t);
   }
   catch(std::exception& e){}
  }
public:
	
	void write(const std::string& m);
  void close();
	void get_peers(std::vector<SessionPtr>& out);
  std::string get_header(const std::string&) const;

  virtual void on_connect(){};
	virtual void on_message(const std::string& m) = 0;
  virtual void on_disconnect(){};
};

template <class T>
class SessionWrap : public Session
{
public:
	void get_peers(std::vector<boost::shared_ptr<T> >& out)
	{
		std::vector<SessionPtr> temp;
		Session::get_peers(temp);

		out.clear();
		for(std::vector<SessionPtr>::iterator iter = temp.begin(); iter != temp.end(); iter++)
		{
			out.push_back(boost::static_pointer_cast<T>(*iter));
		}
	}
};


};


#endif
