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


#ifndef HEXCHAT_IRC_CLIENT_HPP
#define HEXCHAT_IRC_CLIENT_HPP

#include <memory>
#include "tcpfwd.hpp"

namespace irc
{
	class client
	{
		class client_impl;
		std::unique_ptr<client_impl> p_impl;
	public:
		explicit client(std::shared_ptr<io::tcp::connection> && connection);
		~client();
	};
}

#endif
