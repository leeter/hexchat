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

#ifndef LIBIRC_CONNECTION_DETAIL_HPP
#define LIBIRC_CONNECTION_DETAIL_HPP

#ifdef _MSC_VER
#pragma once
#endif

#include <functional>
#include "../config.hpp"
#include "../message_fwd.hpp"
#include "../connection.hpp"

namespace irc
{
	namespace detail
	{
		struct connection_detail : public connection
		{
			virtual const std::function<bool(::irc::connection&, const message&)> & message_handler() const NOEXCEPT = 0;
		};
	}// detail
}// irc

#endif //LIBIRC_CONNECTION_DETAIL_HPP