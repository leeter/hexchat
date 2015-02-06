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
#include <boost/utility/string_ref.hpp>
#include <boost/optional.hpp>
#include "server.hpp"
#include "sutter.hpp"
#include "tcp_connection.hpp"
#include "throttled_queue.hpp"

namespace irc
{
	class server_impl : public connection
	{
		server_impl(const server_impl&) = delete;
		std::string _hostname;
		std::unique_ptr<io::tcp::connection> p_connection;
		::io::irc::throttled_queue outbound_queue;
		bool _throttle;
	public:
		server_impl(std::unique_ptr<io::tcp::connection> connection)
			:p_connection(std::move(connection)), _throttle(false)
		{
		}

	public:
		void send(const boost::string_ref& raw) override final
		{
			if (!_throttle)
				p_connection->enqueue_message(raw.to_string());
			outbound_queue.push(raw);
		}

		void poll()
		{
			auto to_send = outbound_queue.front();
			if (to_send)
			{
				p_connection->enqueue_message(*to_send);
				outbound_queue.pop();
			}
			p_connection->poll();
		}

		void throttle(bool do_throttle)
		{
			_throttle = do_throttle;
		}

	public:
		std::string hostname() const
		{
			return _hostname;
		}

		bool throttle() const NOEXCEPT
		{
			return _throttle;
		}

		io::irc::throttled_queue::size_type queue_length() const NOEXCEPT
		{
			return outbound_queue.queue_length();
		}
	};

	server server::connect(::io::tcp::connection_security sec, const boost::string_ref& hostname, std::uint16_t port)
	{
		boost::asio::io_service io_service;
		auto resolved = io::tcp::resolve_endpoints(io_service, hostname.to_string(), port);
		if (resolved.first){
			boost::asio::detail::throw_error(resolved.first, "resolve");
		}
		auto connection = io::tcp::connection::create_connection(sec, io_service);
		auto impl = sutter::make_unique<server_impl>(std::move(connection));
		return{ std::move(impl) };
	}

	server::server(std::unique_ptr<server_impl> impl)
		:p_impl(std::move(impl))
	{
	}

	server::server(server&& other) NOEXCEPT
	{
		*this = std::move(other);
	}

	/// Destructor is explicit because of using unique_ptr for p_impl
	/// the type must be complete at the point of destruction (here)
	server::~server()
	{
	}

	server& server::operator=(server&& other) NOEXCEPT
	{
		if (this != &other)
		{
			std::swap(this->p_impl, other.p_impl);
		}
		return *this;
	}

	void server::swap(server& other) NOEXCEPT
	{
		*this = std::move(other);
	}

	void server::send(const boost::string_ref& raw)
	{
		p_impl->send(raw);
	}

	void server::throttle(bool do_throttle)
	{
		p_impl->throttle(do_throttle);
	}

	// Accessors
	std::string server::hostname() const
	{
		return p_impl->hostname();
	}

	std::size_t server::queue_length() const NOEXCEPT
	{
		return p_impl->queue_length();
	}

	bool server::throttle() const NOEXCEPT
	{
		return p_impl->throttle();
	}

}// namespace irc
