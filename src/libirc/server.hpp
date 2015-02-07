/* libirc
* Copyright (C) 2014 - 2015 Leetsoftwerx.
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

#ifndef LIBIRC_SERVER_HPP
#define LIBIRC_SERVER_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <boost/utility/string_ref_fwd.hpp>
#include "config.hpp"
#include "connection.hpp"
#include "tcpfwd.hpp"

namespace irc
{
	class server_impl;
	class server : public connection
	{
		server(const server&) = delete;
		server& operator=(const server&) = delete;
		server(std::unique_ptr<server_impl>);

		std::unique_ptr<server_impl> p_impl;
	public:
		server(server&&) NOEXCEPT;
		~server();

		server& operator=(server&&) NOEXCEPT;
	public:
		static server connect(::io::tcp::connection_security sec, const ::boost::string_ref& hostname, ::std::uint16_t port);
	public:
		void swap(server&) NOEXCEPT;
		void send(const ::boost::string_ref&) override final;
		void throttle(bool);

	public:
		::std::string hostname() const;
		bool throttle() const NOEXCEPT;
		std::size_t queue_length() const NOEXCEPT;
	};
} // namespace irc

#endif //LIBIRC_SERVER_HPP