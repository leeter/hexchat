/* HexChat
* Copyright (C) 2014 Berke Viktor.
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

#include <fcntl.h>
#include <iosfwd>
#include <boost/filesystem.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include "charset_helpers.hpp"
#include "filesystem.hpp"

char* get_xdir();

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#define STRICT_TYPED_ITEMIDS
#define NOMINMAX
#include <io.h>
#else
#include <unistd.h>
#define HEXCHAT_DIR "hexchat"
#endif

namespace bio = boost::iostreams;
namespace bfs = boost::filesystem;

namespace io
{
    namespace fs
    {
        bfs::path
            make_path(const std::string & path)
        {
#ifdef WIN32
            return charset::widen(path);
#else
            return path;
#endif
        }

        bio::file_descriptor
            open_stream(const std::string& file, std::ios_base::openmode flags, int mode, int xof_flags)
        {

            bfs::path file_path = make_path(file);
            if (!(xof_flags & XOF_FULLPATH))
                file_path = make_path(get_xdir()) / file_path;
            if (xof_flags & XOF_DOMODE)
            {
                int tfd;
#ifdef WIN32
                tfd = _wopen(file_path.c_str(), _O_CREAT, mode);
#else
                tfd = open(file_path.c_str(), O_CREAT, mode);
#endif
                close(tfd);
            }

            return open_stream(file_path, flags);
        }

        boost::iostreams::file_descriptor
            open_stream(const boost::filesystem::path &file_path, std::ios::openmode flags)
        {
#ifdef WIN32
            return bio::file_descriptor(file_path, flags | std::ios::binary);
#else
            return bio::file_descriptor(file_path.string(), flags | std::ios::binary);
#endif
        }
    }
}