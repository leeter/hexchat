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

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#define STRICT_TYPED_ITEMIDS
#define NOMINMAX
#include <io.h>
#else
#include <unistd.h>
#define HEXCHAT_DIR "hexchat"
#endif

#include <fcntl.h>
#include <string>
#include <boost/filesystem.hpp>
#include "charset_helpers.hpp"
#include "filesystem.hpp"
#include "cfgfiles.hpp"

namespace bfs = boost::filesystem;

namespace io
{
	namespace fs
	{
		bfs::path make_config_path(const bfs::path &path)
		{
			static bfs::path config_dir(config::config_dir());
			return config_dir / path;
		}

		bfs::path make_path(const std::string & path)
		{
#ifdef WIN32
			return charset::widen(path);
#else
			return path;
#endif
		}

		bool create_file_with_mode(const bfs::path& path, int mode)
		{
			int tfd;
#ifdef WIN32
			tfd = _wopen(path.c_str(), _O_CREAT, mode);
#else
			tfd = open(path.c_str(), O_CREAT, mode);
#endif
			bool succeeded = tfd != -1;
			close(tfd);
			return succeeded;
		}
	}
}