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

#include <iosfwd>
#include <string>
#include <vector>
#include <boost/filesystem.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>

namespace io
{
    namespace fs
    {

        boost::filesystem::path make_path(const std::string & path);
        boost::filesystem::path make_path(const std::vector<std::string>& segments);

        typedef int xof_flags;
        enum xof{
            XOF_DOMODE = 1,
            XOF_FULLPATH = 2
        };
        boost::iostreams::file_descriptor
            open_stream(const std::string& file, std::ios::openmode flags, int mode, xof_flags xof_flags);
        boost::iostreams::file_descriptor
            open_stream(const boost::filesystem::path &file_path, std::ios::openmode flags);
    }
}

#endif