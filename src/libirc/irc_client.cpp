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

#include <memory>
#include <string>
#include <utility>
#include "tcp_connection.hpp"
#include "irc_client.hpp"

namespace irc
{
	class client::client_impl
	{
		std::shared_ptr<io::tcp::connection> connection;
		std::string nick;
	public:
		explicit client_impl(std::shared_ptr<io::tcp::connection>&& connection)
			:connection(std::forward<std::shared_ptr<io::tcp::connection> >(connection))
		{
		}

		void change_nick(const std::string& new_nick)
		{

		}

	};

	client::client(std::shared_ptr<io::tcp::connection>&& connection)
		:p_impl(new client_impl(std::forward<std::shared_ptr<io::tcp::connection> >(connection)))
	{}

	// require to be explicit for the p_impl unique_ptr
	client::~client()
	{}

}