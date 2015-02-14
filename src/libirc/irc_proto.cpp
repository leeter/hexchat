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
	namespace proto
	{
		void away(::irc::connection & con, const ::boost::string_ref& reason)
		{
			std::ostringstream out;
			out << "AWAY :";
			if (reason.empty())
				out << " ";
			else
				out << reason;
			out << "\r\n";
			con.send(out.str());
		}

		void back(::irc::connection & con)
		{
			con.send(boost::string_ref("AWAY\r\n", 6));
		}

		void invite(::irc::connection & con, const ::boost::string_ref& nick, const ::boost::string_ref& channel)
		{
			std::ostringstream out;
			out << boost::format{ "INVITE %s %s\r\n" } % nick % channel;
			con.send(out.str());
		}

		void join(::irc::connection& con, const::boost::string_ref& channel, const ::boost::string_ref& key)
		{
			std::ostringstream out;
			out << "JOIN " << channel;
			if (!key.empty())
				out << ' ' << key;
			out << "\r\n";
			con.send(out.str());
		}

		void mode(::irc::connection& con, const ::boost::string_ref& target, const ::boost::string_ref& mode)
		{
			std::ostringstream out;
			out << boost::format{ "MODE %s %s\r\n" } % target % mode;
			con.send(out.str());
		}

		void names(::irc::connection& con, const ::boost::string_ref& channel)
		{
			std::ostringstream out;
			out << boost::format{ "NAMES %s\r\n" } % channel;
			con.send(out.str());
		}

		void nick(::irc::connection& con, const ::boost::string_ref& nick)
		{
			std::ostringstream out;
			out << boost::format{ "NICK %s\r\n" } % nick;
			con.send(out.str());
		}

		void notice(::irc::connection & con, const ::boost::string_ref & channel, const ::boost::string_ref & text)
		{
			std::ostringstream out;
			out << boost::format{ "NOTICE %s :%s\r\n" } % channel % text;
			con.send(out.str());
		}

		void part(::irc::connection& con, const ::boost::string_ref& channel, const ::boost::string_ref& reason)
		{
			std::ostringstream out;
			out << "PART " << channel;
			if (!reason.empty())
				out << ':' << reason;
			out << "\r\n";
			con.send(out.str());
		}

		void privmsg(::irc::connection & con, const ::boost::string_ref & channel, const ::boost::string_ref& message)
		{
			std::ostringstream out;
			out << boost::format{ "PRIVMSG %s :%s\r\n" } % channel % message;
			con.send(out.str());
		}

		void quit(::irc::connection& con, const ::boost::string_ref& reason)
		{
			std::ostringstream out;
			out << "QUIT";
			if (!reason.empty())
			{
				out << " :" << reason;
			}
			out << "\r\n";
			con.send(out.str());
		}

		void whois(::irc::connection & con, const ::boost::string_ref& nick)
		{
			std::ostringstream out;
			out << boost::format{ "WHOIS %s\r\n" } % nick;
			con.send(out.str());
		}
	} // namespace proto

} // namespace irc