/* HexChat
 * Copyright (C) 1998-2010 Peter Zelezny.
 * Copyright (C) 2009-2013 Berke Viktor.
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

/* simple identd server for HexChat under Win32 */

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <boost/asio.hpp>
#include <boost/utility/string_ref.hpp>

#include "identd.hpp"
#include "inet.hpp"
#include "hexchat.hpp"
#include "hexchatc.hpp"
#include "text.hpp"

using boost::asio::ip::tcp;

namespace{

	class session
		: public std::enable_shared_from_this<session>
	{
	public:
		session(tcp::socket socket)
			: socket_(std::move(socket))
		{
		}

		void start()
		{
			do_read();
		}

	private:
		void do_read()
		{
			auto self(shared_from_this());
			boost::asio::async_read_until(this->socket_, this->buffer_, "\r\n",
				[this, self](boost::system::error_code ec, std::size_t length)
			{
				if (!ec)
				{
					std::istream stream(&this->buffer_);
					size_t to_read = std::min(static_cast<size_t>(max_length), length);
					stream.read(data_, to_read);
					do_write(length);
				}
			});
		}

		void do_write(std::size_t length)
		{
			auto self(shared_from_this());
			boost::asio::async_write(socket_, boost::asio::buffer(data_, length),
				[this, self](boost::system::error_code ec, std::size_t /*length*/)
			{
				if (!ec)
				{
					do_read();
				}
			});
		}

		tcp::socket socket_;
		enum: std::size_t { max_length = 1024 };
		boost::asio::streambuf buffer_;
		char data_[max_length];
	};

	class server
	{
	public:
		server(boost::asio::io_service& io_service, short port)
			: acceptor_(io_service, tcp::endpoint(tcp::v4(), port)),
			socket_(io_service)
		{
			do_accept();
		}

	private:
		void do_accept()
		{
			acceptor_.async_accept(socket_,
				[this](boost::system::error_code ec)
			{
				if (!ec)
				{
					std::ostringstream accept_announce("*\tServicing ident request from ", std::ios::ate);
					accept_announce << socket_.remote_endpoint().address().to_string() << "\n";
					PrintText(current_sess, accept_announce.str());
					std::make_shared<session>(std::move(socket_))->start();
				}

				do_accept();
			});
		}

		tcp::acceptor acceptor_;
		tcp::socket socket_;
	};

	static ::std::atomic_bool identd_is_running = { false };
#ifdef USE_IPV6
	static ::std::atomic_bool identd_ipv6_is_running = { false };
#endif

	struct sock{
		SOCKET s;
		sock(SOCKET sock)
			:s(sock){}
		~sock(){
			release();
		}
		void release(){
			if (s != INVALID_SOCKET)
				closesocket(s);
			s = INVALID_SOCKET;
		}
		operator SOCKET(){
			return s;
		}
	};

static int
identd(std::string username)
{
	int len;
	char *p;
	char buf[256];
	char outbuf[256];
	char ipbuf[INET_ADDRSTRLEN];
	sockaddr_in addr = { 0 };
	addr.sin_family = AF_INET;
	addr.sin_port = htons(113);

	sock sok = socket(AF_INET, SOCK_STREAM, 0);
	if (sok == INVALID_SOCKET)
	{
		return 0;
	}

	len = 1;
	setsockopt (sok, SOL_SOCKET, SO_REUSEADDR, (char *) &len, sizeof (len));

	if (bind (sok, (struct sockaddr *) &addr, sizeof (addr)) == SOCKET_ERROR)
	{
		return 0;
	}

	if (listen (sok, 1) == SOCKET_ERROR)
	{
		return 0;
	}

	len = sizeof (addr);
	sock read_sok = accept(sok, (struct sockaddr *) &addr, &len);
	sok.release();
	if (read_sok == INVALID_SOCKET)
	{
		return 0;
	}

	identd_is_running = false;

#if 0	/* causes random crashes, probably due to CreateThread */
	EMIT_SIGNAL (XP_TE_IDENTD, current_sess, inet_ntoa (addr.sin_addr), username, NULL, NULL, 0);
#endif
	inet_ntop (AF_INET, &addr.sin_addr, ipbuf, sizeof (ipbuf));
	snprintf(outbuf, sizeof(outbuf), "*\tServicing ident request from %s as %s\n", ipbuf, username.c_str());
	PrintText (current_sess, outbuf);

	recv (read_sok, buf, sizeof (buf) - 1, 0);
	buf[sizeof (buf) - 1] = 0;	  /* ensure null termination */

	p = strchr (buf, ',');
	if (p)
	{
		snprintf (outbuf, sizeof (outbuf) - 1, "%d, %d : USERID : UNIX : %s\r\n",
		atoi(buf), atoi(p + 1), username.c_str());
		outbuf[sizeof (outbuf) - 1] = 0;	/* ensure null termination */
		send (read_sok, outbuf, strlen (outbuf), 0);
	}

	std::this_thread::sleep_for(std::chrono::seconds(1));

	return 0;
}

#ifdef USE_IPV6
static int
identd_ipv6(::std::string username)
{
		int len;
	char *p;
	char buf[256];
	char outbuf[256];
	char ipbuf[INET6_ADDRSTRLEN];
	struct sockaddr_in6 addr = { 0 };
	addr.sin6_family = AF_INET6;
	addr.sin6_port = htons(113);

	sock sok = socket(AF_INET6, SOCK_STREAM, 0);
	if (sok == INVALID_SOCKET)
	{
		return 0;
	}

	len = 1;
	setsockopt (sok, SOL_SOCKET, SO_REUSEADDR, (char *) &len, sizeof (len));

	if (bind (sok, (struct sockaddr *) &addr, sizeof (addr)) == SOCKET_ERROR)
	{
		return 0;
	}

	if (listen (sok, 1) == SOCKET_ERROR)
	{
		return 0;
	}

	len = sizeof (addr);
	sock read_sok = accept(sok, (struct sockaddr *) &addr, &len);
	sok.release();
	if (read_sok == INVALID_SOCKET)
	{
		return 0;
	}

	identd_ipv6_is_running = false;

	inet_ntop (AF_INET6, &addr.sin6_addr, ipbuf, sizeof (ipbuf));
	snprintf(outbuf, sizeof(outbuf), "*\tServicing ident request from %s as %s\n", ipbuf, username.c_str());
	PrintText (current_sess, outbuf);

	recv (read_sok, buf, sizeof (buf) - 1, 0);
	buf[sizeof (buf) - 1] = 0;	  /* ensure null termination */

	p = strchr (buf, ',');
	if (p)
	{
		snprintf(outbuf, sizeof(outbuf) - 1, "%d, %d : USERID : UNIX : %s\r\n", atoi(buf), atoi(p + 1), username.c_str());
		outbuf[sizeof (outbuf) - 1] = 0;	/* ensure null termination */
		send (read_sok, outbuf, strlen (outbuf), 0);
	}

	std::this_thread::sleep_for(std::chrono::seconds(1));

	return 0;
}
#endif
}

void
identd_start(const ::std::string& username)
{
#ifdef USE_IPV6
	if (identd_ipv6_is_running == false)
	{
		identd_ipv6_is_running = true;
		::std::thread ipv6(identd_ipv6, username);
		ipv6.detach();
	}
#endif

	if (identd_is_running == false)
	{
		identd_is_running = true;
		::std::thread ipv4(identd, username);
		ipv4.detach();
	}
}

namespace io
{
	namespace services
	{
		class identd_server_impl
		{
			std::unordered_map<std::string, std::string> serv_to_nick;
			boost::asio::io_service io_service;
		public:
			void register_username(short server_port, short client_port, const std::string & username)
			{
				std::ostringstream buff;
				buff << server_port << ", " << client_port;
				this->serv_to_nick.emplace(buff.str(), username);
			}

			void poll()
			{
				io_service.poll();
			}
		};

		identd_server::identd_server()
			:p_impl(std::make_shared<io::services::identd_server_impl>()){}

		void identd_server::register_username(short server_port, short client_port, const std::string & username)
		{
			this->p_impl->register_username(server_port, client_port, username);
		}

		void identd_server::poll()
		{
			this->p_impl->poll();
		}
	}
}
