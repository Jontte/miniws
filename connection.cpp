#include "stdafx.h"
#include "connection.h"
#include "myserver.h"

Connection::Connection(boost::asio::io_service& io_service, WS::BasicServer* server) 
	: WS::BasicConnection(io_service, server)
{

}
bool Connection::on_validate_headers()
{
	// check headers
	if(get_resource() != "/socket" || get_method() != "GET")
		return false;
	return true;
}
void Connection::on_connect()
{
	m_source = get_header("Sec-WebSocket-Origin");
	if(m_source.empty())
		m_source = get_header("Origin");
	size_t p = m_source.find("://");
	if(p != string::npos)
		m_source = m_source.substr(p+3);
	
	boost::trim(m_source);
	if(m_source.empty())
		m_source = "unknown";
	
	Json::Value root;
	root["message"] = "hello world!";
	write(root);
	return ;
}
void Connection::on_disconnect()
{
}
void Connection::write(const Json::Value& val)
{
	Json::FastWriter writer;
	write_text(writer.write(val));
}
void Connection::on_message(const string& msg)
{	
	if(msg.length() > 1024) // length exceeded :P
	{
		close();
		return;
	}
	
	Json::Value root;   // will contains the root value after parsing.
	Json::Reader reader;
	Json::FastWriter writer;
	bool parsingSuccessful = reader.parse( msg, root );
	if ( !parsingSuccessful )
		return;

	if(!root.isObject())
		return;

    // Do stuff with packet?
    LOG("Message: " << msg)
}


