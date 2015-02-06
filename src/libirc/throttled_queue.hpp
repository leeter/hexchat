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

#ifndef HEXCHAT_THROTTLED_QUEUE_HPP
#define HEXCHAT_THROTTLED_QUEUE_HPP

#include <cstddef>
#include <memory>
#include <boost/optional/optional_fwd.hpp>
#include "tcpfwd.hpp"

namespace io
{
	namespace irc
	{
		class throttled_queue
		{
			class p_impl;
			std::unique_ptr<p_impl> impl;
		public:
			typedef std::size_t size_type;
			throttled_queue();
			~throttled_queue();

			void push(const std::string & inbound);
			boost::optional<std::string> front();
			void pop();
			void clear();

			size_type queue_length() const;
		};
	}
}

#endif
