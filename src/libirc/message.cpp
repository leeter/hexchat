/* HexChat
* Copyright (C) 2014 Leetsoftwerx.
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

#include <cstdlib>
#include <string>
#include <vector>
#include <boost/algorithm/string.hpp>
#include "message.hpp"

namespace
{
	namespace rfc2812
	{
		const std::string::size_type max_prefix_len = 64;
		const std::string::size_type max_message_len = 512;
	}
}

namespace irc
{
	boost::optional<message> parse(const std::string & inbound)
	{
		message m;
		// if we're getting garbage, just ignore it
		if (inbound.size() > rfc2812::max_message_len)
		{
			return boost::none;
		}
		// a message must end with a crlf
		if (!boost::ends_with(inbound, "\r\n"))
			return boost::none;
		auto prefix_start_loc = inbound.find_first_of(':');
		auto prefix_end_loc = inbound.find_first_of(' ');
		// if there  is a prefix the first character will be :
		if (prefix_start_loc == 0 &&
			prefix_end_loc != 0 &&
			prefix_end_loc != std::string::npos)
		{
			//malformed message
			if (prefix_end_loc > rfc2812::max_prefix_len)
				return boost::none;
			m.prefix = inbound.substr(0, prefix_end_loc);
			// increment by one so we can get the command
			++prefix_end_loc;
		}
		else
		{
			prefix_end_loc = 0;
		}

		// get the command
		auto command_end = inbound.find_first_of(' ', prefix_end_loc);
		// a command is required if we can't parse one bug out
		if (command_end == std::string::npos || command_end == prefix_end_loc)
			return boost::none;
		m.command_s = inbound.substr(prefix_end_loc, command_end - prefix_end_loc);
		char*end;
		auto numeric_rep = std::strtol(m.command_s.c_str(), &end, 10);
		if (numeric_rep)
			m.command_n = static_cast<message::numeric_reply>(numeric_rep);

		// there are parameters
		if (command_end < inbound.size() - 2)
			m.params = inbound.substr(command_end + 1, inbound.size() - 2);

		return m;
	}
}