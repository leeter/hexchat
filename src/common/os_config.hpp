/* HexChat
* Copyright (C) 2017 Leetsoftwerx.
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

#ifndef HEX_COMMON_OS_CONFIG
#define HEX_COMMON_OS_CONFIG

#ifdef _MSC_VER
#pragma once
#endif

namespace os {
	enum class os {
		mac,
		posix,
		windows
	};

#ifdef WIN32
	constexpr os current = os::windows;
#elif __APPLE__
	constexpr os current = os::mac;
#else
	constexpr os current = os::posix;
#endif
}




#endif // !HEX_COMMON_OS_CONFIG



