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

#include <string>
#include <vector>
#include <boost/utility/string_ref.hpp>
#include "split.hpp"

namespace irc
{
	std::vector<std::string> split_for_send(
		std::size_t cmd_length,
		const boost::string_ref & channel,
		const boost::string_ref & userhost,
		const boost::string_ref & message,
		std::codecvt<char, char, std::mbstate_t> & cvt)
	{
		return{};
	}
}