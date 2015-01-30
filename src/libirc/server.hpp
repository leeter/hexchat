#ifndef LIBIRC_SERVER_HPP
#define LIBIRC_SERVER_HPP

#include <memory>
#include <boost/utility/string_ref_fwd.hpp>
#include "connection.hpp"

namespace irc
{
	class server_impl;
	class server : public connection
	{
		std::unique_ptr<server_impl> p_impl;
		server();
	public:
		~server();

	public:
		static server connect();
		void send(const boost::string_ref&) override final;
	};
}

#endif //LIBIRC_SERVER_HPP