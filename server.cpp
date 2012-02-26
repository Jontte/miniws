#include "server.h"
#include "sha1.h"
#include "base64.h"
#include <boost/algorithm/string.hpp>
#include <boost/bind.hpp>
#include <ctime>
#include <endian.h>

namespace WS {
	
namespace
{
	std::ostream& log()
	{
		return std::cout;
	}
	
	// Static linkage
	union u64
	{
		uint64_t integer;
		unsigned char data[8];
	};
	union u32
	{
		uint32_t integer;
		unsigned char data[4];
	};
	union u16
	{
		uint16_t integer;
		unsigned char data[2];
	};

	uint64_t ntohll(uint64_t in)
	{
		return be64toh(in);
	}
	uint64_t htonll(uint64_t in)
	{
		return htobe64(in);
	}
}

struct Frame
{
	bool fin;
	bool rsv1;
	bool rsv2;
	bool rsv3;
	uint8_t opcode; // 4-bits
	bool have_mask;
	uint64_t payload_length;
	uint8_t masking_key[4];
	std::vector<uint8_t> payload_data;
	
	bool is_control_frame() { return opcode & 0x8; }
	
	void print();
	
	// Return amount of bytes consumed or 0 on failure
	uint64_t parse(buffer_iterator begin, buffer_iterator end);
	
	// Write frame and return it
	MessagePtr write();
};


uint64_t Frame::parse(buffer_iterator begin, buffer_iterator end)
{
	buffer_iterator i = begin;
	uint64_t counter = 0;

	if(i == end)return 0;
	
	uint8_t firstbyte = *i;
	
	fin  = firstbyte & (1<<7);
	rsv1 = firstbyte & (1<<6);
	rsv2 = firstbyte & (1<<5);
	rsv3 = firstbyte & (1<<4);
	
	opcode = firstbyte & 0xF;
		
	if(++i == end)return 0;
	
	uint64_t length = *i;
	
	have_mask = length & 128;
	
	length &= 127;
		
	if(length == 126)
	{
		// 16-bit length follows
		
		u16 len;			
		if(++i == end)return 0;
		len.data[0] = *i;
		if(++i == end)return 0;
		len.data[1] = *i;
		
		length = ntohs(len.integer);
		counter = 4 + length;
	}
	else if(length == 127)
	{
		// 64-bit length follows
		// make sure there's at least 8 bytes left
		u64 len;
		for(int a = 0; a < 8; a++)
		{
			if(++i == end)return 0;
			len.data[a] = *i;
		}
		length = ntohll(len.integer);
		
		counter = 10 + length;
	}
	else
	{
		counter = 2 + length;
	}
	
	payload_length = length;
	

	if(have_mask)
	{
		for(int a = 0; a < 4; a++)
		{
			if(++i == end)return 0;
			masking_key[a] = *i;
		}
		counter += 4;
	}
	payload_data.resize(payload_length);
	for(uint64_t a = 0; a < length; a++)
	{
		if(++i == end)return 0;
		
		payload_data[a] = *i;
		
		
		if(have_mask)
			payload_data[a] ^= masking_key[a % 4];
	}
	
	return counter;
}
MessagePtr Frame::write()
{
	MessagePtr msg = boost::make_shared<std::vector <char> >();
	msg->reserve(200);
	
	unsigned char firstbyte = 0;
	firstbyte |= fin << 7;
	firstbyte |= rsv1 << 6;
	firstbyte |= rsv2 << 5;
	firstbyte |= rsv3 << 4;
	firstbyte |= opcode & 0xF;
	
	msg->push_back(firstbyte);
	
	// host never masks..
	payload_length = payload_data.size();
	if(payload_length < 126)
	{
		msg->push_back(uint8_t(payload_length));
	}
	else if(payload_length <= 0xFFFF)
	{
		msg->push_back(uint8_t(126));
		u16 conv;
		conv.integer = htons(uint16_t(payload_length));
		msg->push_back(conv.data[0]);
		msg->push_back(conv.data[1]);
	}
	else
	{
		msg->push_back(uint8_t(127));
		u64 conv;
		conv.integer = htonll(payload_length);
		for(int i = 0; i < 8; i++)
			msg->push_back(conv.data[i]);
	}
	msg->insert(msg->end(), payload_data.begin(), payload_data.end());
	return msg;
}
void Frame::print()
{
	using std::cout;
	using std::endl;
	
	cout << "== BEGIN FRAME ==" << endl;
	cout << "fin 	| " << fin << endl;
	cout << "rsv1	| " << rsv1 << endl;
	cout << "rsv2	| " << rsv2 << endl;
	cout << "rsv3	| " << rsv3 << endl;
	cout << "opcode	| " << int(opcode) << endl;
	cout << "masked	| " << have_mask << endl;
	cout << "length	| " << payload_length << endl;
	if(have_mask) {
	cout << "mask	| " << int(masking_key[0]) << ", "
						<< int(masking_key[1]) << ", "
						<< int(masking_key[2]) << ", "
						<< int(masking_key[3]) << endl;
	}
	
	cout << "{ ";
	for(uint64_t i = 0; i < payload_data.size(); i++)
		cout << int(payload_data[i]) << " ";
	cout << "}" << endl;

	cout << "{ ";
	for(uint64_t i = 0; i < payload_data.size(); i++)
		cout << char(payload_data[i]);
	cout << " }" << endl;
	cout << "== END FRAME ==" << endl;
}

Server::Server(boost::asio::io_service& io_service, int port)
	: acceptor_(io_service, tcp::endpoint(tcp::v4(), port))
{
	start_listen();
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

void Server::get_peers(const std::string& resource, std::set<boost::shared_ptr<Session> >& out)
{
	boost::lock_guard<boost::recursive_mutex> lock(m_connections_mutex);
	out.clear();

	std::set<ConnectionPtr >::iterator iter;
	for(iter = m_connections.begin(); iter != m_connections.end(); iter++)
	{
		if((*iter)->get_resource() == resource && (*iter)->get_session())
			out.insert((*iter)->get_session());
	}
}

void Server::start_listen()
{
	ConnectionPtr new_connection =
		boost::make_shared<Connection>(
			boost::ref(acceptor_.get_io_service()), this);

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
boost::shared_ptr<BaseFactory> Server::get_factory(const std::string& resource)
{
	std::map<std::string, boost::shared_ptr<BaseFactory> >::iterator iter = m_factories.find(resource);
	if(iter == m_factories.end())
		return boost::shared_ptr<BaseFactory>();
	return iter->second;
}
void Session::send(const std::string& m)
{
	try
	{
		ConnectionPtr con(m_connection);
		con->write_text(m);
	}
	catch(std::exception& e)
	{
		// session might be dead..
		m_connection.reset();
	}
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
			if(f.payload_data.size() == 1)
			{
				uint8_t recv = f.payload_data[0];
				
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
			
			std::copy(f.payload_data.begin(), f.payload_data.end(), 
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
				std::copy(f.payload_data.begin(), f.payload_data.end(), 
					std::ostream_iterator<uint8_t>(in));
				return;
			}
			else
			{
				std::string s(f.payload_data.begin(),f.payload_data.end());
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
		boost::shared_ptr<BaseFactory> fact;
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
		m_session.reset(fact->make_session());
		if(!m_session)
		{
			log() << "Unable to create session!" << std::endl;
			close();
			return;
		}
		
		m_session->m_connection = shared_from_this();
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

boost::shared_ptr<Session> Connection::get_session() const
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
	f.payload_data.assign(1, val);
	write_raw(f.write());
}
void Connection::write_text(const std::string& message)
{
	// construct a Frame and send it.
	Frame f;
	f.fin = 1;
	f.rsv1 = f.rsv2 = f.rsv3 = 0;
	f.opcode = 0x1;
	f.have_mask = 0;
	f.payload_data.insert(f.payload_data.end(), message.begin(), message.end());
	write_raw(f.write());
}
void Connection::write_raw(const std::string& message)
{
	MessagePtr msg = boost::make_shared<std::vector<char> > ();
	msg->insert(msg->end(), message.begin(), message.end());
	write_raw(msg);
}
void Connection::write_raw(MessagePtr msg)
{
	//if(!socket_.is_open())return;
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
void Connection::write_impl(MessagePtr msg)
{
	//if(!socket_.is_open())return;
	//	return;
	// called from inside the strand!
	m_outbox.push_back( msg );
	if ( m_outbox.size() > 1 ) {
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

