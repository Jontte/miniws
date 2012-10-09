#include <iostream>
#include "server.h"
#include "sha1.h"
#include "base64.h"
#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>

namespace WS {

namespace // Static linkage
{
	std::ostream& log()
	{
		return std::cout;
	}
}

Server::Server(boost::asio::io_service& io_service, int port)
	: acceptor_(io_service, tcp::endpoint(tcp::v4(), port))
{
    start_listen();
}
Server::~Server()
{
    stop_listen();
}
void Server::stop_listen()
{
	acceptor_.close();
}
void Server::prune(ConnectionPtr con)
{
	// Remove given connection from connections array
	boost::lock_guard<boost::recursive_mutex> lock(m_connections_mutex);

	std::set<ConnectionPtr >::iterator iter = m_connections.find(con);

	if(iter == m_connections.end())
		return;

	m_connections.erase(iter);
	log() << "Client disconnected, clients left: " << m_connections.size() << std::endl;
}

void Server::get_peers(const std::string& resource, std::vector<std::shared_ptr<Session> >& out)
{
	boost::lock_guard<boost::recursive_mutex> lock(m_connections_mutex);
	out.clear();

	std::set<ConnectionPtr >::iterator iter;
	for(iter = m_connections.begin(); iter != m_connections.end(); iter++)
	{
		if((*iter)->get_resource() == resource && (*iter)->get_session())
			out.push_back((*iter)->get_session());
	}
}

void Server::start_listen()
{
	ConnectionPtr new_connection(new Connection(boost::ref(acceptor_.get_io_service()), this));

	acceptor_.async_accept(new_connection->socket(),
		boost::bind(&Server::handle_accept, this, new_connection,
			boost::asio::placeholders::error));
}

void Server::handle_accept(ConnectionPtr new_connection,
					const boost::system::error_code& error)
{
	if (!error)
	{
		new_connection->async_read();

		boost::lock_guard<boost::recursive_mutex> lock(m_connections_mutex);
		m_connections.insert(new_connection);
		log() << "Client connected, clients now: " << m_connections.size() << std::endl;
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
void Session::write(const std::string& m)
{
	try
	{
		ConnectionPtr con(m_connection);
		con->write_text(m);
	}
	catch(std::exception& e)
	{
		// session might be dead..
	}
}
void Session::get_peers(std::vector<SessionPtr>& out)
{
	try
	{
		ConnectionPtr con(m_connection);
		con->get_server().get_peers(con->get_resource(), out);
	}
	catch(std::exception& e)
	{
	}
}
void Session::close()
{
	try
	{
		ConnectionPtr con(m_connection);
    con->close();
	}
	catch(std::exception& e)
	{
	}
  m_connection.reset();
}
std::string Session::get_header(const std::string& key) const
{
	try
	{
		ConnectionPtr con(m_connection);
    return con->get_header(key);
	}
	catch(std::exception& e)
	{
		// session might be dead..
	}
  return "";
}


std::pair<buffer_iterator, bool> Connection::buffer_ready_condition(buffer_iterator begin, buffer_iterator end)
{
	// If we're still reading headers, cut from the first newline
	if(m_parsing_headers)
	{
		buffer_iterator i = begin;
		int counter = 0;
		while (i != end)
		{
		  if ('\n' == *i++)
		    return std::make_pair(i, true);
		  if(counter++ > (1<<16))
		  {
			// somethings not right!
			close();
			return std::make_pair(i, false);
  		  }
		}
		return std::make_pair(i, false);
	}
	else
	{
		// look for the next frame of websocket data
		Frame f;
		uint64_t bytes = f.parse(begin, end);
		if(bytes > 0)
		{
			// successfully parsed..
			for(uint64_t a = 0; a < bytes; a++)
				begin++;
			return std::make_pair(begin, true);
		}
		return std::make_pair(begin, false);
	}
}

void Connection::handle_write(const boost::system::error_code& error,
  size_t bytes_transferred,
  MessagePtr msg /*buffer keepalive handle*/)
{
	m_outbox.pop_front();
	if(error)
	{
		//log() << "handle_write: " << error.message() << std::endl;
		if(!m_active)
		{
			socket_.close();
		}
		else
			close();
		return;
	}
	m_bytes_sent += bytes_transferred;
	if(!m_outbox.empty())
		write_socket_impl();
	else if(!m_active)
	{
		// We were asked to shut down and we've just finished flushing the outbox.
		// Time for us to die.
		socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
		socket_.close();
	}
}
void Connection::handle_read(const boost::system::error_code& error, size_t bytes_transferred)
{
	if(!error)
	{
		m_bytes_received += bytes_transferred;
		if(m_parsing_headers)
		{
			std::istream is(&buffer_);
			std::string line;
			std::getline(is, line);

			if(!line.empty() && line[line.size()-1] == '\r')
				line.erase(line.end()-1);

			parse_header(line);

			// still parsing headers after all these bytes?
			// somethings not right
			if(m_bytes_received > 4*1024)
			{
				log() << "Client dropped: Huge handshake" << std::endl;
				close();
				return;
			}
		}
		else
		{
			Frame f;
			buffer_iterator begin = buffer_iterator::begin(buffer_.data());
			buffer_iterator end = buffer_iterator::begin(buffer_.data());
			end += bytes_transferred;

			int bytes = f.parse(begin, end);
			buffer_.consume(bytes);

			process(f);
		}

		async_read();
	}
	else
	{
		//log() << "handle_read: " << error.message() << std::endl;
		close();
	}
}

void Connection::process(Frame& f)
{
	// process recently received frame


	// These bits shouldn't be set'
	if(f.rsv1 || f.rsv2 || f.rsv3)
	{
		close();
		return;
	}

	if(f.is_control_frame())
	{
		if(f.opcode == 0x8)
		{
			// close-frame
			close();
			return;
		}
		else if(f.opcode == 0x9)
		{
			// ping-frame
			// send pong
			f.opcode = 0x10;
			write_raw(f.write());
			return;
		}
		else if(f.opcode == 0xA)
		{
			// pong-frame
			if(f.payload_data->size() == 1)
			{
				uint8_t recv = (*f.payload_data)[0];

				if(recv == m_ping.data)
				{
					boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();

					boost::posix_time::time_duration dur = boost::posix_time::time_period(m_ping.time, now).length();

					log() << "Ping/Pong roundtrip seconds: " << double(dur.total_microseconds())/1000000 << std::endl;

					m_ping.time = boost::posix_time::ptime();
				}
			}
			return;
		}
	}
	else
	{
		if(f.opcode == 0)
		{
			// continuation frame
			std::ostream in(&fragment_);

			std::copy(f.payload_data->begin(), f.payload_data->end(),
				std::ostream_iterator<uint8_t>(in));

			if(f.fin)
			{
				// consume fragmented message
				std::string s(
					buffer_iterator::begin(fragment_.data()),
					buffer_iterator::end(fragment_.data())
				);

				// clear the buffer
				fragment_.consume(fragment_.size());

				try
				{
					if(m_session)
						m_session->on_message(s);
				}
				catch(std::exception& e)
				{
					log() << "on_message: " << e.what() << std::endl;
				}
				return;
			}
			return;
		}
		else if(f.opcode == 0x1 || f.opcode == 0x2)
		{
			// text or binary data

			if(!f.fin)
			{
				// first piece of fragmented data ..
				std::ostream in(&fragment_);
				std::copy(f.payload_data->begin(), f.payload_data->end(),
					std::ostream_iterator<uint8_t>(in));
				return;
			}
			else
			{
				std::string s(f.payload_data->begin(),f.payload_data->end());
				try
				{
					if(m_session)
						m_session->on_message(s);
				}
				catch(std::exception& e)
				{
					log() << "on_message: " << e.what() << std::endl;
				}
				return;
			}
		}
	}

	log() << "UNHANDLED FRAME: " << std::endl;
	f.print();
}

void Connection::async_read()
{
	try
	{
		boost::asio::async_read_until(socket_, buffer_,
			boost::bind(&Connection::buffer_ready_condition, shared_from_this(), _1, _2),
			strand_.wrap(
				boost::bind(&Connection::handle_read,
				shared_from_this(),
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred)));
	}
	catch(std::exception& e)
	{
		log() << "async_read: " << e.what() << std::endl;
		close();
	}
}

bool Connection::validate_headers()
{
	// return whether the received headers are good from the Websocket apis point of view

	if(get_resource().empty() || get_http_version().empty() || get_method().empty())
		return false;
	if(boost::to_lower_copy(get_header("Connection")).find("upgrade") == std::string::npos)
		return false;
	if(boost::to_lower_copy(get_header("Upgrade")) != "websocket")
		return false;

	// mandate Sec-WebSocket-Origin or Origin
	if(	get_header("Sec-WebSocket-Origin").empty() &&
		get_header("Origin").empty())
		return false;

	// method must be GET
	if(get_method() != "GET")
		return false;

	std::string version = get_header("Sec-WebSocket-Version");
	if(version == "13")
		m_protocol_version = PROTOCOL_HYBI_13;
	else if(version == "8")
		m_protocol_version = PROTOCOL_HYBI_08;
	else
		return false;

	// Just a reminder.
	if(m_protocol_version == PROTOCOL_INDETERMINATE)
		return false;
	return true;
}
void Connection::send_handshake()
{
	// create and send handshake response

	std::string response = get_header("Sec-WebSocket-Key") + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

	unsigned char hash[20];
	sha1::calc(response.c_str(), response.length(), hash);
	std::string accept = base64_encode(hash, 20);

	std::string handshake;
	handshake.reserve(300);

	handshake =
"HTTP/1.1 101 Switching Protocols\r\n"
"Upgrade: websocket\r\n"
"Connection: Upgrade\r\n"
"Sec-WebSocket-Accept: ";
	handshake += accept;
	handshake += "\r\n\r\n";
	write_raw(handshake);
}
void Connection::parse_header(const std::string& line)
{
	if(!m_parsing_headers) return;
	// The first line contains method, resource and http version:
	if(m_method.empty())
	{
		size_t pos = line.find(' ');
		size_t prev = pos;
		if(pos == std::string::npos)
			return;
		m_method = line.substr(0,pos);
		pos = line.find(' ', pos+1);
		m_resource = line.substr(prev+1,pos-prev);
		m_http_version = line.substr(pos+1);
		boost::trim(m_method);
		boost::trim(m_resource);
		boost::trim(m_http_version);
		return;
	}

	if(line.empty())
	{
		// done with headers?
		m_parsing_headers = false;
		std::shared_ptr<BaseFactory> fact;
		// validate headers, check whether to handle resource
		if(!validate_headers() || !(fact = get_server().get_factory(m_resource)))
		{
			log() << "Client didn't get past validation phase" << std::endl;
			log() << "Client headers: " << std::endl;
			log() << "	" << get_method() << " " << get_resource() << " " << get_http_version() << std::endl;
			std::map<std::string, std::string>::iterator iter = m_headers.begin();
			for(;iter != m_headers.end(); iter++)
			{
				log() << "	" << iter->first << ": " << iter->second << std::endl;
			}
		    close();
		    return;
		}

		send_handshake();

		// Create session!
		m_session = fact->make_session();
		if(!m_session)
		{
			log() << "Unable to create session!" << std::endl;
			close();
			return;
		}

		m_session->m_connection = shared_from_this();

		m_owner->on_connect(m_session);
		m_session->on_connect();
		return;
	}

	// find ':'
	size_t pos = line.find(':');
	if(pos == std::string::npos)
		return;
	std::string name = line.substr(0,pos);
	std::string value = line.substr(pos+1);

	// trim
	boost::trim(name);
	boost::trim(value);
	if(name.empty())
		return;
	m_headers[name] = value;
};

Connection::Connection(boost::asio::io_service& io_service,
	Server* owner)
	: socket_(io_service)
	, strand_(io_service)
	, m_parsing_headers(true)
	, m_active(true)
	, m_owner(owner)
	, m_bytes_sent(0)
	, m_bytes_received(0)
	, m_protocol_version(PROTOCOL_INDETERMINATE)
{
}

std::shared_ptr<Session> Connection::get_session() const
{
	return m_session;
}

void Connection::ping()
{
	uint8_t val = rand() % 256;

	m_ping.time = boost::posix_time::microsec_clock::local_time();
	m_ping.data = val;

	Frame f;
	f.fin = 1;
	f.rsv1 = f.rsv2 = f.rsv3 = 0;
	f.opcode = 0x9;
	f.have_mask = 0;
	f.payload_data = std::make_shared<std::vector<char> >(1, val);

	std::vector<MessagePtr> total(2);
	total[0] = f.write();
	total[1] = f.payload_data;
	write_raw(total);
}
void Connection::write_text(const std::string& message)
{
	// construct a Frame and send it.
	Frame f;
	f.fin = 1;
	f.rsv1 = f.rsv2 = f.rsv3 = 0;
	f.opcode = 0x1;
	f.have_mask = 0;
	f.payload_data = std::make_shared<std::vector<char> >(message.begin(), message.end());

	std::vector<MessagePtr> total(2);
	total[0] = f.write();
	total[1] = f.payload_data;
	write_raw(total);
}
void Connection::write_raw(const std::string& message)
{
	MessagePtr msg = std::make_shared<std::vector<char> > ();
	msg->insert(msg->end(), message.begin(), message.end());
	write_raw(msg);
}
void Connection::write_raw(MessagePtr msg)
{
	try
	{
		strand_.post(
			boost::bind(
				&Connection::write_impl,
				shared_from_this(),
				msg
			)
		);
	}
	catch(std::exception& e)
	{
		log() << "write_raw: " << e.what() << std::endl;
		close();
	}
}
void Connection::write_raw(const std::vector<MessagePtr>& msg)
{
	try
	{
		strand_.post(
			boost::bind(
				&Connection::write_many_impl,
				shared_from_this(),
				msg
			)
		);
	}
	catch(std::exception& e)
	{
		log() << "write_raw: " << e.what() << std::endl;
		close();
	}
}
void Connection::write_impl(MessagePtr msg)
{
	// called from inside the strand!
	m_outbox.push_back( msg );
	if ( m_outbox.size() > 1 ) {
		// outstanding async_write
		return;
	}
	write_socket_impl();
}
void Connection::write_many_impl(std::vector<MessagePtr> msg)
{
	// called from inside the strand!
	m_outbox.insert(m_outbox.end(), msg.begin(), msg.end());
	if ( m_outbox.size() > msg.size() ) {
		// outstanding async_write
		return;
	}
	write_socket_impl();
}
void Connection::write_socket_impl()
{
	try
	{
		const MessagePtr& msg = m_outbox.front();

		boost::asio::async_write(
			socket_,
			boost::asio::buffer(*msg),
			strand_.wrap(
				boost::bind(&Connection::handle_write,
				shared_from_this(),
				boost::asio::placeholders::error,
				boost::asio::placeholders::bytes_transferred,
				msg)
			)
		);
	}
	catch(std::exception& e)
	{
		log() << "write_socket_impl: " << e.what() << std::endl;
		close();
	}
}

bool Connection::have_header(const std::string& q) const
{
	return m_headers.find(q) != m_headers.end();
}
std::string Connection::get_header(const std::string& q) const
{
	std::map<std::string,std::string>::const_iterator iter = m_headers.find(q);
	if(iter != m_headers.end())
		return iter->second;
	return "";
}
std::string Connection::get_method() const
{
	return m_method;
}
std::string Connection::get_resource() const
{
	return m_resource;
}
std::string Connection::get_http_version() const
{
	return m_http_version;
}

tcp::socket& Connection::socket()
{
	return socket_;
}

void Connection::close()
{
	if(m_active)
	{
		m_session->on_disconnect();
		m_owner->on_disconnect(m_session);

		strand_.post(
			boost::bind(&Connection::close_impl,
			shared_from_this())
		);
		m_owner -> prune(shared_from_this());
		m_session.reset();
	}
	m_active = false;
}

void Connection::close_impl()
{
	// Called from within a strand!
	boost::system::error_code e;
	socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_receive);
	if(m_outbox.empty())
	{
		// No pending operations
		socket_.shutdown(boost::asio::ip::tcp::socket::shutdown_both);
		socket_.close(e);
	}
}

}

