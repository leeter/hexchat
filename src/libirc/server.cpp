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

#include <memory>
#include <string>
#include <boost/utility/string_ref.hpp>
#include <boost/optional.hpp>
#include "server.hpp"
#include "tcp_connection.hpp"
#include "throttled_queue.hpp"
#include "message.hpp"
#include "detail/connection_detail.hpp"
#include "detail/inbound.hpp"

namespace irc
{
	class server_impl : public detail::connection_detail
	{
		server_impl(const server_impl&) = delete;
		std::string _hostname;
		std::unique_ptr<io::tcp::connection> p_connection;
		::io::irc::throttled_queue outbound_queue;
		std::function<bool(connection&, const message&)> _message_handler;
		bool _throttle;
	public:
		server_impl(std::unique_ptr<io::tcp::connection> connection)
			:p_connection(std::move(connection)), _throttle(false)
		{
			p_connection->on_message.connect([this](const std::string& message, std::size_t length){
				irc::detail::inbound::handle_inbound_message(*this, message, length);
			});
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

		void message_handler(const std::function<bool(connection&, const message&)>& new_handler)
		{
			_message_handler = new_handler;
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

		explicit operator bool() const NOEXCEPT
		{
			return p_connection->connected();
		}

		const std::function<bool(::irc::connection&, const message&)> & message_handler() const NOEXCEPT override final
		{
			return _message_handler;
		}
	};

	server server::connect(::io::tcp::connection_security sec, const boost::string_ref& hostname, std::uint16_t port)
	{
		auto connection = io::tcp::connection::create_connection(sec);
		auto impl = std::make_unique<server_impl>(std::move(connection));
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

	void server::message_handler(const std::function<bool(connection&, const message&)>& new_handler)
	{
		p_impl->message_handler(new_handler);
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

	server::operator bool() const NOEXCEPT
	{
		return static_cast<bool>(*p_impl);
	}

}// namespace irc
