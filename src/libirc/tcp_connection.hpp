/* HexChat
* Copyright (C) 2014 Berke Viktor.
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

#ifndef HEXCHAT_TCP_CONNECTION_HPP
#define HEXCHAT_TCP_CONNECTION_HPP

#include <functional>
#include <memory>
#include <string>
#include <boost/asio.hpp>
#include <boost/signals2.hpp>

namespace io
{
	namespace tcp{
		enum class connection_security{
			none,
			enforced,
			no_verify
		};

		class connection{
		public:
			static std::shared_ptr<connection> create_connection(connection_security security, boost::asio::io_service& io_service);
			virtual void enqueue_message(const std::string & message) = 0;
			virtual void connect(boost::asio::ip::tcp::resolver::iterator endpoint_iterator) = 0;
			virtual bool connected() const = 0;
			virtual void poll() = 0;
			virtual ~connection(){}
			boost::signals2::signal<void(const boost::system::error_code&)> on_connect;
			boost::signals2::signal<void(const std::string& hostname)> on_valid_connection;
			boost::signals2::signal<void(const boost::system::error_code&)> on_error;
			boost::signals2::signal<void(const std::string & message, size_t length)> on_message;
			boost::signals2::signal<void(const SSL*)> on_ssl_handshakecomplete;
		};

		boost::asio::ip::tcp::resolver::iterator resolve_endpoints(boost::asio::io_service& io_service, const std::string & host, unsigned short port);
	}
}
#endif