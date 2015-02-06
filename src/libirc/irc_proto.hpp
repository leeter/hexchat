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

#ifndef LIBIRC_IRC_PROTO_HPP
#define LIBIRC_IRC_PROTO_HPP

#include <boost/utility/string_ref_fwd.hpp>
#include "connection_fwd.hpp"

namespace irc
{
	namespace proto
	{
		void names(::irc::connection& con, const ::boost::string_ref& channel);
		void notice(::irc::connection & con, const ::boost::string_ref & channel, const ::boost::string_ref & text);
		void ping_user(::irc::connection & con, const ::boost::string_ref& user, const ::boost::string_ref & timestring);
		void privmsg(::irc::connection & con, const ::boost::string_ref & channel, const ::boost::string_ref& message);
		void whois(::irc::connection & con, const ::boost::string_ref& nick);
	} // namespace proto
} // namespace irc

#endif // LIBIRC_IRC_PROTO_HPP