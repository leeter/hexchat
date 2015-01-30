#ifndef LIBIRC_CONNECTION_HPP
#define LIBIRC_CONNECTION_HPP

#include <boost/utility/string_ref_fwd.hpp>

namespace irc
{
	class connection
	{
	public:
		virtual ~connection(){}
	public:
		virtual void send(const boost::string_ref&) = 0;
	};
}

#endif // LIBIRC_CONNECTION_HPP
