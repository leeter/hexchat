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

#ifndef LIBIRC_MESSAGE_HPP
#define LIBIRC_MESSAGE_HPP

#ifdef _MSC_VER
#pragma once
#endif

#include <string>
#include <boost/optional.hpp>
#include "message_fwd.hpp"

namespace irc
{
	struct message
	{
		enum numeric_reply
		{
			NON_NUMERIC = 0,
			RPL_WELCOME = 1,
			RPL_YOURHOST,
			RPL_CREATED,
			RPL_MYINFO,
			RPL_BOUNCE,

			RPL_TRACELINK = 200,
			RPL_TRACECONNECTING,

			RPL_USERHOST = 302,
			RPL_ISON
		};
		std::string prefix;
		numeric_reply reply;
		std::string command;
		std::string params;
		std::string nick;
		std::string host;
	};

	boost::optional<message> parse(const std::string & inbound);
}

#endif