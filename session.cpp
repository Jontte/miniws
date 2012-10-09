#include "session.h"

namespace WS {

void Session::write(const std::string& m)
{
	try
	{
		m_connection.lock()->write_text(m);
	}
	catch(std::bad_weak_ptr& e)
	{
		// session might be dead..
	}
}
std::vector<SessionPtr> Session::get_peers()
{
	std::vector<SessionPtr> out;
	try
	{
		auto con = m_connection.lock();
		out = con->get_server()->get_peers(con->get_resource());
	}
	catch(std::bad_weak_ptr& e)
	{
	}
	return out;
}
void Session::close()
{
	try
	{
		m_connection.lock()->close();
	}
	catch(std::bad_weak_ptr& e)
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
	catch(std::bad_weak_ptr& e)
	{
		// session might be dead..
	}
	return "";
}

}