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
#include <locale>
#include <type_traits>
#include <vector>
#include "tcp_connection.hpp"
#include "irc_client.hpp"
#include "sutter.hpp"

namespace
{
	bool is_valid_nick(const std::string& candidate)
	{
		return false;
	}
}

namespace irc
{
	class client::client_impl
	{
		std::unique_ptr<io::tcp::connection> connection;
		std::string nick;
		std::locale loc;
		typedef std::pair<std::hash<std::string>::result_type, std::shared_ptr<detail::filter> > hash_pair;
		std::vector <hash_pair > filters;

	public:
		explicit client_impl(std::unique_ptr<io::tcp::connection> connection, const std::locale & loc)
			:connection(std::move(connection)),
			loc(loc)
		{
		}

		void change_nick(const std::string& new_nick)
		{
			// TODO: do we change the nick here? or listen later
		}

		void add_filter(const std::string & name, std::shared_ptr<detail::filter> filter)
		{
			filters.emplace_back(std::make_pair(std::hash<std::string>()(name), filter));
		}

		bool remove_filter(const std::string & name)
		{
			auto hash = std::hash<std::string>()(name);
			// remove all filters with that name hash
			filters.erase(
				std::remove_if(filters.begin(), filters.end(), [hash](const hash_pair & ele){
				return hash == ele.first;
			}), filters.end());
			return false;
		}

		// broken we need a way to poll the filters
		void send(const std::string & to_send)
		{
			auto value = boost::make_optional<std::string>(to_send);
			for (auto & filter : filters)
			{
				if (!value)
					return;
				filter.second->input(*value);
				value = filter.second->next();
			}
			this->connection->enqueue_message(*value);
		}

	};

	client::client(std::unique_ptr<io::tcp::connection> connection, std::locale loc)
		:p_impl(sutter::make_unique<client_impl>(std::move(connection), loc))
	{}

	// require to be explicit for the p_impl unique_ptr
	client::~client()
	{}

	void client::add_filter(const std::string & name, std::shared_ptr<detail::filter> filter)
	{
		p_impl->add_filter(name, filter);
	}

	bool client::remove_filter(const std::string & name)
	{
		return p_impl->remove_filter(name);
	}

	void get_capablities(client& c)
	{
		//c. "CAP LS\r\n");		/* start with CAP LS as Charybdis sasl.txt suggests */
		//this->sent_capend = FALSE;	/* track if we have finished */
	}

	void login()
	{
		/*
		if (this->password[0] && this->loginmethod == LOGIN_PASS)
		{
		tcp_sendf (this, "PASS %s\r\n", this->password);
		}

		tcp_sendf (this,
		"NICK %s\r\n"
		"USER %s %s %s :%s\r\n",
		this->nick, user.c_str(), user.c_str(), this->servername, realname.c_str());
		*/
	}
}
