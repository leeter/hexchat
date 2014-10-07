/* HexChat
* Copyright (C) 2014 Leetsoftwerx.
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


#ifndef IRCLIB_SUTTER_HPP
#define IRCLIB_SUTTER_HPP


// temporary until gcc gets make_unique
namespace sutter
{
	template<class T, class... Types>
	inline typename std::enable_if<!std::is_array<T>::value,
		std::unique_ptr<T> >::type make_unique(Types&&... Args)
	{
		return (std::unique_ptr<T>(new T(std::forward<Types>(Args)...)));
	}
}

#endif