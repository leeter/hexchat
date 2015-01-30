#include <boost/utility/string_ref.hpp>
#include "server.hpp"
#include "tcp_connection.hpp"

namespace irc
{
	class server_impl
	{
		std::unique_ptr<io::tcp::connection> p_connection;
	public:
		server_impl(std::unique_ptr<io::tcp::connection> connection)
			:p_connection(std::move(connection))
		{

		}

	};

	server server::connect()
	{
		return{};
	}

	server::server()
	{
	}


	server::~server()
	{
	}

	void server::send(const boost::string_ref& raw)
	{

	}

}// namespace irc
