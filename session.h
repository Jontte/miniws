#ifndef SESSION_H_INCLUDED
#define SESSION_H_INCLUDED
#include "connection.h"

namespace WS {

class Session
{
	friend class Connection;
private:
	std::weak_ptr<Connection> m_connection;
protected:
	template <class T>
	void post(T t)
	{
		try
		{
			m_connection.lock()->post(t);
		}
		catch(std::bad_weak_ptr& e){}
	}
public:

	void write(const std::string& m);
	void close();
	std::vector< SessionPtr > get_peers();
	std::string get_header(const std::string&) const;

	virtual void on_connect(){};
	virtual void on_message(const std::string& m) = 0;
	virtual void on_disconnect(){};
};

template <class T>
class SessionWrap : public Session
{
public:
	std::vector<std::shared_ptr<T> > get_peers()
	{
		std::vector<std::shared_ptr<T> > out;
		auto peers = Session::get_peers();
		for(auto iter = peers.begin(); iter != peers.end(); ++iter)
		{
			out.push_back(std::static_pointer_cast<T>(*iter));
		}
		return out;
	}
};

}

#endif