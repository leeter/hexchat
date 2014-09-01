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

#ifndef HEXCHAT_TCP_CONNECTION_HPP
#define HEXCHAT_TCP_CONNECTION_HPP

#include <functional>
#include <memory>
#include <string>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "hexchat.hpp"

struct context;

enum class connection_security{
    none,
    enforced,
    no_verify
};

class connection{
public:
    static std::unique_ptr<connection> create_connection(connection_security security, boost::asio::io_service& io_service, boost::asio::ip::tcp::resolver::iterator endpoint_iterator, server& owner );
    virtual void enqueue_message(const std::string & message) = 0;
};

#endif