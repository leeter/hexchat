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

#include <string>
#include <memory>
#include <utility>
#include <boost/bind.hpp>
#include "tcp_connection.hpp"

struct context{
    context(boost::asio::io_service& io_service, boost::asio::ssl::context::verify_mode mode)
        :ssl_ctx(io_service, boost::asio::ssl::context::sslv23)
    {
        ssl_ctx.set_verify_mode(mode);
    }

    boost::asio::ssl::context ssl_ctx;
};

namespace{

    template<class SocketType_>
    class basic_connection : public connection
    {
    public:
        ~basic_connection(){}
        template<class... Types_>
        basic_connection(std::shared_ptr<context>& ctx, boost::asio::ip::tcp::resolver::iterator endpoint_iterator, Types_&& ... args)
            :ctx_(ctx), socket_(std::forward<Types_>(args)...)
        {
            boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
            socket_.lowest_layer().async_connect(endpoint,
                boost::bind(&basic_connection::handle_connect, this,
                boost::asio::placeholders::error, ++endpoint_iterator));
        }
        basic_connection(){};
        void enqueue_message(const std::string & message);
    private:
        void handle_connect(const boost::system::error_code& error,
            boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
        void handle_handshake(const boost::system::error_code& error);
        void handle_read(const boost::system::error_code& error,
            size_t bytes_transferred);
        void handle_write(const boost::system::error_code& error,
            size_t bytes_transferred);

        boost::optional<std::string> get_next_message() const;
        void pop_output_queue();
        boost::asio::streambuf input_buffer_;
    private:
        std::shared_ptr<context> ctx_;
        std::queue<std::string> outbound_queue_;
        SocketType_ socket_;

        friend std::unique_ptr<connection>
        connection::create_connection(connection_security security, boost::asio::io_service& io_service, boost::asio::ip::tcp::resolver::iterator endpoint_iterator, server& owner);
    };        

    template<class SocketType_>
    void
    basic_connection<SocketType_>::enqueue_message(const std::string & message)
    {
        this->outbound_queue_.push(message);
    }

    template<class SocketType_>
    void
    basic_connection<SocketType_>::handle_connect(const boost::system::error_code& error,
            boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
    {
        if (!error)
        {
            socket_.async_handshake(boost::asio::ssl::stream_base::client,
                boost::bind(&basic_connection<typename SocketType_>::handle_handshake, this,
                boost::asio::placeholders::error));
        }
        else if (endpoint_iterator != boost::asio::ip::tcp::resolver::iterator())
        {
            socket_.lowest_layer().close();
            boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
            socket_.lowest_layer().async_connect(endpoint,
                boost::bind(&basic_connection<typename SocketType_>::handle_connect, this,
                boost::asio::placeholders::error, ++endpoint_iterator));
        }
        else
        {
            // TODO: print error to session
        }
    }

    template<class SocketType_>
    void
    basic_connection<SocketType_>::handle_handshake(const boost::system::error_code& error)
    {
        if (!error)
        {           
            /*boost::asio::async_write(socket_,
                boost::asio::buffer(request_, request_length),
                boost::bind(&client::handle_write, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred));*/
        }
        else
        {
            // TODO: print error to session
        }
    }

    template<class SocketType_>
    void
    basic_connection<SocketType_>::handle_write(const boost::system::error_code& error,
        size_t bytes_transferred)
    {
        if (!error)
        {
            boost::asio::async_read_until(socket_, this->input_buffer_, "\r\n",
                boost::bind(&basic_connection<typename SocketType_>::handle_read, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred));
        }
        else
        {
            // TODO: print error to session
        }
    }

    template<class SocketType_>
    void
    basic_connection<SocketType_>::handle_read(const boost::system::error_code& error,
        size_t bytes_transferred)
    {
        if (!error)
        {
            auto next_message = this->get_next_message();
            if (!next_message)
                return;

            boost::asio::async_write(socket_,
                boost::asio::buffer(next_message.get()),
                boost::bind(&basic_connection<typename SocketType_>::handle_write, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred));
            // TODO: push read to server
        }
        else
        {
            // TODO: print error to session
        }
    }

}

typedef basic_connection < boost::asio::ssl::stream<boost::asio::ip::tcp::socket> > ssl_connection;

std::unique_ptr<connection>
connection::create_connection(connection_security security, boost::asio::io_service& io_service, boost::asio::ip::tcp::resolver::iterator endpoint_iterator, server& owner)
{
    if (security == connection_security::enforced || security == connection_security::no_verify)
    {
        auto ctx = std::make_shared<context>(io_service, security == connection_security::enforced ? boost::asio::ssl::context::verify_peer : boost::asio::ssl::context::verify_none);
        return std::unique_ptr<ssl_connection>(new ssl_connection(ctx, endpoint_iterator, io_service, ctx->ssl_ctx));
    }
    return nullptr;
}

