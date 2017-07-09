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
#define OPENSSL_NO_SSL2
#define OPENSSL_NO_SSL3
#include <atomic>
#include <algorithm>
#include <istream>
#include <memory>
#include <queue>
#include <random>
#include <string>
#include <utility>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include "tcp_connection.hpp"

#ifdef WIN32
#include "win_tls_stream.hpp"
#include "w32crypt_seed.hpp"
#else

namespace{

	struct context{
		virtual ~context(){}

		boost::asio::io_service io_service;
	};

	struct ssl_context : public context{
		ssl_context(boost::asio::ssl::context::verify_mode mode)
			:ssl_ctx(io_service, boost::asio::ssl::context::tlsv12)
		{
			ssl_ctx.set_options(
				boost::asio::ssl::context::no_sslv2 |
				boost::asio::ssl::context::no_sslv3 |
				boost::asio::ssl::context::no_compression | 
				boost::asio::ssl::context::single_dh_use |
				SSL_OP_CIPHER_SERVER_PREFERENCE);
			ssl_ctx.set_verify_mode(mode);
		}
		boost::asio::ssl::context ssl_ctx;
	};

	template<class SocketType_>
	struct basic_connection : public io::tcp::connection
	{
		virtual ~basic_connection(){}
		template<class... Types_>
		basic_connection(context * ctx, Types_&& ... args)
			:ctx_(ctx), message_(4096, '\0'), socket_(ctx_->io_service, std::forward<Types_>(args)...), strand_(ctx_->io_service)
		{
			input_buffer_.commit(4092);
		}

		basic_connection(){};

		bool connected() const
		{
			return socket_.lowest_layer().is_open();
		}

		void connect(boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
		{
			boost::asio::ip::tcp::resolver::iterator current_iterator = endpoint_iterator;
			boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
			socket_.lowest_layer().async_connect(endpoint,
				[this, endpoint_iterator, current_iterator](const auto & error) mutable {
				this->do_connect(error, ++endpoint_iterator, current_iterator);
			});
		}
		void enqueue_message(const std::string & message);
		/* Gets around the thorny issue of calling or referencing a
		 * virtual function from the constructor
		 */
		void do_connect(const boost::system::error_code& error,
			boost::asio::ip::tcp::resolver::iterator endpoint_iterator,
			boost::asio::ip::tcp::resolver::iterator current_endpoint){
			if (error && endpoint_iterator != boost::asio::ip::tcp::resolver::iterator())
			{
				socket_.lowest_layer().close();
				boost::asio::ip::tcp::resolver::iterator current_iterator = endpoint_iterator;
				boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
				socket_.lowest_layer().async_connect(endpoint,
					[this, endpoint_iterator, current_iterator](const auto & error) mutable {
					this->do_connect(error, ++endpoint_iterator, current_iterator);
				});
			}
			else if (error)
			{
				this->handle_error(error);
			}
			else
			{
				boost::asio::ip::tcp::no_delay no_delay(true);
				this->socket_.lowest_layer().set_option(no_delay);
				boost::asio::socket_base::non_blocking_io non_blocking(true);
				this->socket_.lowest_layer().io_control(non_blocking);
				boost::asio::socket_base::keep_alive option(true);
				this->socket_.lowest_layer().set_option(option);
				this->on_valid_connection(current_endpoint->host_name());
				this->handle_connect(error, current_endpoint);
			}
		}
		virtual void handle_connect(const boost::system::error_code& error,
			boost::asio::ip::tcp::resolver::iterator endpoint_iterator) = 0;
		void handle_read(const boost::system::error_code& error,
			size_t bytes_transferred);
		void handle_write(const boost::system::error_code& error,
			size_t bytes_transferred);
		void handle_error(const boost::system::error_code& error);
		void poll()
		{
			this->ctx_->io_service.poll();
		}
		void write_impl(const std::string& message);
		void write();

		std::unique_ptr<context> ctx_;
		std::string message_;
		boost::asio::streambuf input_buffer_;
		std::queue<std::string> outbound_queue_;
		SocketType_ socket_;
		boost::asio::strand strand_;
		boost::asio::ip::tcp::endpoint connected_endpoint_;
	};

	struct ssl_connection : public basic_connection < boost::asio::ssl::stream<boost::asio::ip::tcp::socket> >
	{
		ssl_connection(ssl_context * ctx)
			:basic_connection(ctx, ctx->ssl_ctx)
		{
		}

		void handle_connect(const boost::system::error_code& error,
			boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
		{
			if (!error)
			{
				socket_.async_handshake(boost::asio::ssl::stream_base::client, [this](const auto& error) {
					this->handle_handshake(error);
				});
			}
			else
			{
				this->handle_error(error);
				// TODO: print error to session
			}
		}

		void handle_handshake(const boost::system::error_code& error)
		{
			if (!error)
			{
				// start the read loop
				boost::asio::async_read_until(socket_, this->input_buffer_, "\r\n",
					[this](const auto & error, auto transferred ) {
					this->handle_read(error, transferred);
				});

				// callback to allow for printing of cipher info
				this->on_ssl_handshakecomplete(socket_.impl()->ssl);
				this->on_connect(error);
			}
			else
			{
				this->handle_error(error);
			}
		}
	};

	struct tcp_connection : public basic_connection < boost::asio::ip::tcp::socket >
	{
		tcp_connection(context * ctx)
			:basic_connection(ctx)
		{
		}

		void handle_connect(const boost::system::error_code& error,
			boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
		{
			if (error)
			{
				this->handle_error(error);
				return;
			}
			// start the read loop
			boost::asio::async_read_until(socket_, this->input_buffer_, "\r\n",
				[this](const auto & error, auto transferred) {
				this->handle_read(error, transferred);
			});
			this->on_connect(error);
		}
	};

	template<class SocketType_>
	void
	basic_connection<SocketType_>::write_impl(const std::string & message)
	{
		this->outbound_queue_.push(message);
		// return if we have a pending write
		if (this->outbound_queue_.size() > 1)
			return;
		this->write();
	}

	template<class SocketType_>
	void
	basic_connection<SocketType_>::write()
	{
		const std::string& message = this->outbound_queue_.front();
		boost::asio::async_write(socket_,
			boost::asio::buffer(message),
			[this](const auto & error, auto transferred) {
			this->handle_write(error, transferred);
		});
	}

	template<class SocketType_>
	void
	basic_connection<SocketType_>::enqueue_message(const std::string & message)
	{
		this->strand_.post([this, message] {
			this->write_impl(message);
		});
	}

	template<class SocketType_>
	void
	basic_connection<SocketType_>::handle_write(const boost::system::error_code& error,
		size_t bytes_transferred)
	{
		if (error)
		{
			this->handle_error(error);
			// TODO: print error to session
			return;
		}
		this->outbound_queue_.pop();
		if (!this->outbound_queue_.empty()){
			this->write();
		}
	   
	}

	template<class SocketType_>
	void
	basic_connection<SocketType_>::handle_read(const boost::system::error_code& error,
		size_t bytes_transferred)
	{
		if (!error)
		{
			std::istream stream(&input_buffer_);
			size_t to_read = std::min(message_.size(), bytes_transferred);
			stream.read(&message_[0], to_read);
			this->on_message(message_, to_read);
			
			boost::asio::async_read_until(socket_, this->input_buffer_, "\r\n",
				[this](const auto & error, auto transferred) {
				this->handle_read(error, transferred);
			});
		}
		else
		{
			this->handle_error(error);
		}
	}

	template<class SocketType_>
	void basic_connection<SocketType_>::handle_error(const boost::system::error_code& error)
	{
		// do reconnect here?
		/*switch (error.value())
		{
		case boost::system::errc::no_link:
		case boost::system::errc::connection_reset:
			if (this->reconnect_)
			{
				if (socket_.lowest_layer().is_open())
					socket_.lowest_layer().close();

				socket_.lowest_layer().async_connect(*(this->connected_endpoint_),
					boost::bind(&basic_connection::do_connect, this,
					boost::asio::placeholders::error, this->connected_endpoint_));
			}
			break;
		default:
			break;
		}*/
		this->on_error(error);
	}
	
}
#endif

namespace io{
	namespace tcp{

		std::pair<boost::system::error_code, boost::asio::ip::tcp::resolver::iterator> resolve_endpoints(boost::asio::io_service& io_service, const std::string & host, unsigned short port)
		{
			boost::asio::ip::tcp::resolver::query query{ host, std::to_string(port) };
			boost::asio::ip::tcp::resolver res{ io_service };
			boost::system::error_code ec;
			auto result = res.resolve(query, ec);
			return std::make_pair(ec, result);
		}
#ifndef WIN32

		std::unique_ptr<connection>
			connection::create_connection(connection_security security, boost::asio::io_service& io_service)
		{
			if (security == connection_security::enforced || security == connection_security::no_verify)
			{
#ifdef WIN32
				w32::crypto::seed_openssl_random();
#endif
				return std::make_unique<ssl_connection>(new ssl_context(security == connection_security::enforced ? boost::asio::ssl::verify_peer : boost::asio::ssl::verify_none));
			}
			return std::make_unique<tcp_connection>(new context());
		}
#endif
	}
}

