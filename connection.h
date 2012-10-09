#ifndef MINIWS_CONNECTION_H_INCLUDED_
#define MINIWS_CONNECTION_H_INCLUDED_

#include "server.h"

namespace WS {

class Connection
: public std::enable_shared_from_this<Connection>
, public boost::noncopyable
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

	std::weak_ptr<Server> m_server;

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

	std::shared_ptr<Session> m_session;

	Connection(
		boost::asio::io_service& io_service,
		std::shared_ptr<Server>
	);

public:

	template <class T>
	void post(const T& t)
	{
		strand_.post(t);
	}

	std::shared_ptr<Server> get_server() const
	{
		return m_server.lock();
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

}

#endif