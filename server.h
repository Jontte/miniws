#ifndef SERVER_H_INCLUDED_
#define SERVER_H_INCLUDED_

namespace WS
{

using boost::asio::ip::tcp;

typedef boost::asio::buffers_iterator<boost::asio::streambuf::const_buffers_type> buffer_iterator;
class BasicConnection;

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
	vector<uint8_t> payload_data;
	
	bool is_control_frame() { return opcode & 0x8; }
	
	void print();
	
	// Return amount of bytes consumed or 0 on failure
	uint64_t parse(buffer_iterator begin, buffer_iterator end);
	
	// Write frame and return it
	shared_ptr<vector<char> > write();
};

class BasicServer : public boost::noncopyable
{
	public:
	// just provides an interface
	virtual void prune(shared_ptr<BasicConnection>) = 0;
};

template <class Con>
class Server : public BasicServer
{
	private:
	
	tcp::acceptor acceptor_;
	
	protected:
	std::set<shared_ptr<Con> > m_connections;
	
	// A mutex to restrict access to the m_connections set
	boost::recursive_mutex m_connections_mutex;
	
	void stop_listen()
	{
		acceptor_.close();
	}

	public:

	Server(boost::asio::io_service& io_service, int port)
		: acceptor_(io_service, tcp::endpoint(tcp::v4(), port))
	{
		start_accept();
	}
	virtual ~Server(){}
	
	void prune(shared_ptr<BasicConnection> con)
	{
		// Remove given connection from connections array
		boost::lock_guard<boost::recursive_mutex> lock(m_connections_mutex);
		
		shared_ptr<Con> foo = boost::static_pointer_cast<Con>(con);
		
    typename std::set<shared_ptr<Con> >::iterator iter = m_connections.find(foo);
		if(iter == m_connections.end())
			return;
		
		m_connections.erase(iter);
		log() << "Client disconnected, clients left: " << m_connections.size() << endl;
	}
	
	void get_peers(std::set<shared_ptr<Con> > & in)
	{
		boost::lock_guard<boost::recursive_mutex> lock(m_connections_mutex);
		in = m_connections;
	}

	private:
	void start_accept()
	{
		shared_ptr<Con> new_connection = 
			make_shared<Con>(boost::ref(acceptor_.get_io_service()), this);
		
		acceptor_.async_accept(new_connection->socket(),
		boost::bind(&Server::handle_accept, this, new_connection,
			boost::asio::placeholders::error));
	}

	void handle_accept(shared_ptr<Con> new_connection,
		const boost::system::error_code& error)
	{
		if (!error)
		{
			new_connection->initialize();		
			
			boost::lock_guard<boost::recursive_mutex> lock(m_connections_mutex);	
			m_connections.insert(new_connection);
			log() << "Client connected, clients now: " << m_connections.size() << endl;
		}
		start_accept();
	}
};
	
class BasicConnection
	: public boost::enable_shared_from_this<BasicConnection>
{
private:
	tcp::socket socket_;
	boost::asio::io_service::strand strand_;
	boost::asio::streambuf buffer_;
	boost::asio::streambuf fragment_; // fragmented pieces written here
	
	
	map<string, string> m_headers;
	string m_method;
	string m_resource;
	string m_http_version;
	bool m_parsing_headers;
	bool m_active;
	
	BasicServer * m_owner;

	uint64_t m_bytes_sent;
	uint64_t m_bytes_received;
	
	enum {
		PROTOCOL_HYBI_08, 
		PROTOCOL_HYBI_13,
		PROTOCOL_INDETERMINATE
	} m_protocol_version;
	
	deque<shared_ptr<vector<char> > > m_outbox;
		
	void handle_write(const boost::system::error_code& /*error*/,
	  size_t /*bytes_transferred*/,
	  shared_ptr<vector<char> > /*buffer keepalive handle*/);
	void handle_read(const boost::system::error_code& error, size_t bytes_transferred);
	
	void async_read();
	
	bool validate_headers();
	void send_handshake();
	void parse_header(const string& line);
	void process(Frame& f); 
	
	// strand'ed, thread safe call:
	void close_impl();
	
	// outbox-related function calls
	void write_impl(shared_ptr<vector<char> > );
	void write_socket_impl();
	
	struct
	{
		uint8_t data;
		boost::posix_time::ptime time;
	} m_ping;
protected:
	BasicConnection(
		boost::asio::io_service& io_service,
		BasicServer* );

	virtual void on_connect() = 0;
	virtual void on_message(const string& message) = 0;
	virtual bool on_validate_headers() = 0;
	virtual void on_disconnect() = 0;
	
	void ping();
	
public:

	pair<buffer_iterator, bool> buffer_ready_condition(buffer_iterator begin, buffer_iterator end);

	void initialize();
	
	bool have_header(const string& q) const;
	string get_header(const string& q) const;
	string get_method() const;
	string get_resource() const;
	string get_http_version() const;
	tcp::socket& socket();	
	void close();

	// encapsulated data
	void write_text(const string& message);

	// raw data	
	void write_raw(const string& message);
	void write_raw(shared_ptr<vector<char> > msg);
		
	virtual ~BasicConnection();
	
	template <class T> 
	Server<T>* get_server() const
	{
		return static_cast<Server<T>*>(m_owner); 
	}
};

};

/* typedef decltype(
			   boost::bind(&WS::BasicConnection::buffer_ready_condition,
				         shared_ptr<WS::BasicConnection>(), _1, _2)
				    ) koira;

static_assert(sizeof(koira) == 1);

namespace boost {
	namespace asio {
		template <> struct is_match_condition<
    koira >
		: public boost::true_type {};
	}
}*/


#endif
