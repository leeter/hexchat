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

#ifndef HEXCHAT_FILESYSTEM_HPP
#define HEXCHAT_FILESYSTEM_HPP

#include <string>
#include <boost/filesystem.hpp>

namespace io
{
	namespace fs
	{
		boost::filesystem::path make_config_path(const boost::filesystem::path &);

		boost::filesystem::path make_path(const std::string & path);

		typedef int xof_flags;
		enum xof{
			XOF_DOMODE = 1,
			XOF_FULLPATH = 2
		};
		
		bool create_file_with_mode(const boost::filesystem::path&, int mode) noexcept;
	}
}

#endif