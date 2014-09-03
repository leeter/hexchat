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

#include <atomic>
#include <algorithm>
#include <istream>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include "tcp_connection.hpp"

namespace{

    struct context{
        context(boost::asio::io_service& io_service, boost::asio::ssl::context::verify_mode mode)
            :ssl_ctx(io_service, boost::asio::ssl::context::sslv23)
        {
            ssl_ctx.set_verify_mode(mode);
        }

        boost::asio::ssl::context ssl_ctx;
    };

    template<class SocketType_>
    struct basic_connection : public io::tcp::connection
    {
        virtual ~basic_connection(){}
        template<class... Types_>
        basic_connection(boost::asio::io_service & service, Types_&& ... args)
            :message_(4096, '\0'), socket_(service, std::forward<Types_>(args)...), strand_(service), reconnect_({true})
        {
            input_buffer_.commit(4092);
        }

        basic_connection(){};

        bool reconnect() const
        {
            return this->reconnect_;
        }

        void reconnect(bool should_reconnect)
        {
            this->reconnect_ = should_reconnect;
        }
        void connect(boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
        {
            boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
            socket_.lowest_layer().async_connect(endpoint,
                boost::bind(&basic_connection::do_connect, this,
                boost::asio::placeholders::error, ++endpoint_iterator));
        }
        void enqueue_message(const std::string & message);
        /* Gets around the thorny issue of calling or referencing a
         * virtual function from the constructor
         */
        void do_connect(const boost::system::error_code& error,
            boost::asio::ip::tcp::resolver::iterator endpoint_iterator){
            if (error && endpoint_iterator != boost::asio::ip::tcp::resolver::iterator())
            {
                socket_.lowest_layer().close();
                boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
                socket_.lowest_layer().async_connect(endpoint,
                    boost::bind(&basic_connection::do_connect, this,
                    boost::asio::placeholders::error, ++endpoint_iterator));
            }
            else if (error)
            {
                this->handle_error(error);
            }
            else
            {
                if (endpoint_iterator != boost::asio::ip::tcp::resolver::iterator())
                    this->connected_endpoint_ = endpoint_iterator;

                this->on_valid_connection(this->connected_endpoint_->host_name());
                this->handle_connect(error, this->connected_endpoint_);
            }
        }
        virtual void handle_connect(const boost::system::error_code& error,
            boost::asio::ip::tcp::resolver::iterator endpoint_iterator) = 0;
        void handle_read(const boost::system::error_code& error,
            size_t bytes_transferred);
        void handle_write(const boost::system::error_code& error,
            size_t bytes_transferred);
        void handle_error(const boost::system::error_code& error);

        void write_impl(const std::string& message);
        void write();

        std::string message_;
        boost::asio::streambuf input_buffer_;
        std::queue<std::string> outbound_queue_;
        SocketType_ socket_;
        boost::asio::strand strand_;
        std::atomic_bool reconnect_;
        boost::asio::ip::tcp::resolver::iterator connected_endpoint_;
    };

    struct ssl_connection : public basic_connection < boost::asio::ssl::stream<boost::asio::ip::tcp::socket> >
    {
        ssl_connection(std::shared_ptr<context>& ctx, boost::asio::io_service & service, boost::asio::ssl::context & ssl_context)
            :basic_connection(service, ssl_context), ctx_(ctx)
        {}

        void handle_connect(const boost::system::error_code& error,
            boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
        {
            if (!error)
            {
                socket_.async_handshake(boost::asio::ssl::stream_base::client,
                    boost::bind(&ssl_connection::handle_handshake, this,
                    boost::asio::placeholders::error));
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
                    boost::bind(&basic_connection::handle_read, this,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));

                // callback to allow for printing of cipher info
                this->on_ssl_handshakecomplete(socket_.impl()->ssl);
                this->on_connect(error);
            }
            else
            {
                this->handle_error(error);
            }
        }
        std::shared_ptr<context> ctx_;
    };

    struct tcp_connection : public basic_connection < boost::asio::ip::tcp::socket >
    {
        tcp_connection(boost::asio::io_service & service)
            :basic_connection(service)
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
                boost::bind(&basic_connection::handle_read, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred));
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
            boost::bind(&basic_connection::handle_write, this,
            boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred));
    }

    template<class SocketType_>
    void
    basic_connection<SocketType_>::enqueue_message(const std::string & message)
    {
        this->strand_.post(std::bind(std::mem_fn(&basic_connection::write_impl), this, message));
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
                boost::bind(&basic_connection::handle_read, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred));
        }
        else
        {
            this->handle_error(error);
        }
    }

    template<class SocketType_>
    void basic_connection<SocketType_>::handle_error(const boost::system::error_code& error)
    {
        switch (error.value())
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
        }
        this->on_error(error);
    }
}

namespace io{
    namespace tcp{

        boost::asio::ip::tcp::resolver::iterator resolve_endpoints(boost::asio::io_service& io_service, const std::string & host, unsigned short port)
        {
            boost::asio::ip::tcp::resolver::query query(host, std::to_string(port));
            boost::asio::ip::tcp::resolver res(io_service);
            return res.resolve(query);
        }

        std::shared_ptr<connection>
            connection::create_connection(connection_security security, boost::asio::io_service& io_service)
        {
            if (security == connection_security::enforced || security == connection_security::no_verify)
            {
                auto ctx = std::make_shared<context>(io_service, 0);
                return std::make_shared<ssl_connection>(ctx, io_service, ctx->ssl_ctx);
            }
            return std::make_shared<tcp_connection>(io_service);
        }
    }
}

