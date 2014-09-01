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

#include <memory>
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

    class ssl_connection : public connection{
    public:
        ssl_connection(std::shared_ptr<context> & ctx, boost::asio::io_service& io_service, boost::asio::ssl::context& context,
            boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
        void handle_connect(const boost::system::error_code& error,
            boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
        void handle_handshake(const boost::system::error_code& error);
        void handle_read(const boost::system::error_code& error,
            size_t bytes_transferred);
        void handle_write(const boost::system::error_code& error,
            size_t bytes_transferred);
    private:
        boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket_;
    };

    ssl_connection::ssl_connection(std::shared_ptr<context> & ctx, boost::asio::io_service& io_service, boost::asio::ssl::context& context,
        boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
        :connection(ctx), socket_(io_service, context)
    {
        boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
        socket_.lowest_layer().async_connect(endpoint,
            boost::bind(&ssl_connection::handle_connect, this,
            boost::asio::placeholders::error, ++endpoint_iterator));
    }

    void
    ssl_connection::handle_connect(const boost::system::error_code& error,
            boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
    {
        if (!error)
        {
            socket_.async_handshake(boost::asio::ssl::stream_base::client,
                boost::bind(&ssl_connection::handle_handshake, this,
                boost::asio::placeholders::error));
        }
        else if (endpoint_iterator != boost::asio::ip::tcp::resolver::iterator())
        {
            socket_.lowest_layer().close();
            boost::asio::ip::tcp::endpoint endpoint = *endpoint_iterator;
            socket_.lowest_layer().async_connect(endpoint,
                boost::bind(&ssl_connection::handle_connect, this,
                boost::asio::placeholders::error, ++endpoint_iterator));
        }
        else
        {
            //std::cout << "Connect failed: " << error << "\n";
        }
    }

    void 
    ssl_connection::handle_handshake(const boost::system::error_code& error)
    {
        if (!error)
        {
           /* std::cout << "Enter message: ";
            std::cin.getline(request_, max_length);*/
            //size_t request_length = strlen(request_);

           /* boost::asio::async_write(socket_,
                boost::asio::buffer(request_, request_length),
                boost::bind(&ssl_connection::handle_write, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred));*/
        }
        else
        {
            //std::cout << "Handshake failed: " << error << "\n";
        }
    }

    void 
    ssl_connection::handle_write(const boost::system::error_code& error,
        size_t bytes_transferred)
    {
        if (!error)
        {
            /*boost::asio::async_read(socket_,
                boost::asio::buffer(reply_, bytes_transferred),
                boost::bind(&ssl_connection::handle_read, this,
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred));*/
        }
        else
        {
            //std::cout << "Write failed: " << error << "\n";
        }
    }

    void handle_read(const boost::system::error_code& error,
        size_t bytes_transferred)
    {
        if (!error)
        {
           /* std::cout << "Reply: ";
            std::cout.write(reply_, bytes_transferred);
            std::cout << "\n";*/
        }
        else
        {
            //std::cout << "Read failed: " << error << "\n";
        }
    }

}

std::unique_ptr<connection>
connection::create_connection(connection_security security, boost::asio::io_service& io_service, boost::asio::ip::tcp::resolver::iterator endpoint_iterator)
{
    if (security == connection_security::enforced || security == connection_security::no_verify)
    {
        auto ctx = std::make_shared<context>(io_service, security == connection_security::enforced ? boost::asio::ssl::context::verify_peer : boost::asio::ssl::context::verify_none);
        return std::unique_ptr<ssl_connection>(new ssl_connection(ctx, io_service, ctx->ssl_ctx, endpoint_iterator));
    }
    return nullptr;
}
