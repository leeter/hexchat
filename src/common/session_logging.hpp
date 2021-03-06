/* HexChat
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

#ifndef HEXCHAT_SESSION_LOGGING_HPP
#define HEXCHAT_SESSION_LOGGING_HPP

#include <ctime>
#include <string>
#include <memory>
#include "sessfwd.hpp"

std::string log_create_filename(const std::string & channame);
//void log_write(session &sess, const std::string & text, time_t ts);
//void log_close(session &sess);
//void log_open_or_close(session *sess);

class session_logger_impl;

class session_logger
{
	session & _parent;
	std::unique_ptr<session_logger_impl> _impl;
public:
	session_logger(session & parent);
	~session_logger();
	bool write(const std::string & text, time_t ts);
};

#endif
