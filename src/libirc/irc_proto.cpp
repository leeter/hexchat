/* libirc
* Copyright (C) 2015 Leetsoftwerx.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
*/

#include <string>
#include <sstream>
#include <boost/utility/string_ref.hpp>
#include <boost/format.hpp>
#include "irc_proto.hpp"
#include "connection.hpp"

namespace irc
{
	void send_privmsg(::irc::connection & con, const ::boost::string_ref & channel, const ::boost::string_ref& message)
	{
		std::ostringstream out;
		out << boost::format("PRIVMSG %s :%s\r\n") % channel % message;
		con.send(out.str());
	}

} // namespace irc