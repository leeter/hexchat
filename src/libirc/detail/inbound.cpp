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

#include <functional>
#include <string>
#include "../message.hpp"
#include "connection_detail.hpp"

namespace irc
{
	namespace detail
	{
		namespace inbound
		{
			void handle_inbound_message(irc::detail::connection_detail & con, const std::string & message, std::size_t)
			{
				const auto & inbound_handler = con.message_handler();
				auto parsed_message = irc::parse(message);
				if (!parsed_message || inbound_handler && inbound_handler(con, parsed_message.get()))
				{
					return;
				}

				// TODO any handling of our own
			}
		} // inbound
	}// detail
}// irc