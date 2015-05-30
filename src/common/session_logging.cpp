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

#include <cstdio>
#include <sstream>
#include <string>

#include <fcntl.h>
#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif


#include <boost/algorithm/string/regex.hpp>
#include <boost/format.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/regex.hpp>
#include <boost/utility/string_ref.hpp>

#include <glib.h>

#include "fe.hpp"
#include "cfgfiles.hpp"
#include "chanopt.hpp"
#include "hexchat.hpp"
#include "hexchatc.hpp"
#include "session.hpp"
#include "server.hpp"
#include "session_logging.hpp"
#include "text.hpp"
#include "util.hpp"

std::string log_create_filename(const std::string & channame)
{
	static const std::string replace_format{ "_" };
#ifdef WIN32
	/* win32 can't handle filenames with \|/><:"*? characters */
	static boost::regex replace_regex("(/|\\\\|>|<|\\:|\"|\\*|\\?|\\|)");
#else
	static boost::regex replace_regex("/");
#endif
	return boost::replace_all_regex_copy(channame, replace_regex, replace_format);
}

void log_close(session &sess)
{
	if (sess.logfd != -1)
	{
		std::time_t currenttime = std::time(nullptr);
		std::ostringstream stream;
		stream << boost::format(_("**** ENDING LOGGING AT %s\n")) % std::ctime(&currenttime);
		auto to_output = stream.str();
		write(sess.logfd, to_output.c_str(), to_output.length());
		close(sess.logfd);
		sess.logfd = -1;
	}
}

/* like strcpy, but % turns into %% */

static char * log_escape_strcpy(char *dest, const char *src, const char *end)
{
	while (*src)
	{
		*dest = *src;
		if (dest + 1 == end)
			break;
		dest++;
		src++;

		if (*src == '%')
		{
			if (dest + 1 == end)
				break;
			dest[0] = '%';
			dest++;
		}
	}

	dest[0] = 0;
	return dest - 1;
}

/* substitutes %c %n %s into buffer */

static void log_insert_vars(char *buf, size_t bufsize, const char *fmt, const char *c, const char *n, const char *s)
{
	char *end = buf + bufsize;

	for (;;)
	{
		switch (fmt[0])
		{
		case 0:
			buf[0] = 0;
			return;

		case '%':
			fmt++;
			switch (fmt[0])
			{
			case 'c':
				buf = log_escape_strcpy(buf, c, end);
				break;
			case 'n':
				buf = log_escape_strcpy(buf, n, end);
				break;
			case 's':
				buf = log_escape_strcpy(buf, s, end);
				break;
			default:
				buf[0] = '%';
				buf++;
				buf[0] = fmt[0];
				break;
			}
			break;

		default:
			buf[0] = fmt[0];
		}
		fmt++;
		buf++;
		/* doesn't fit? */
		if (buf == end)
		{
			buf[-1] = 0;
			return;
		}
	}
}

static bool logmask_is_fullpath()
{
	/* Check if final path/filename is absolute or relative.
	* If one uses log mask variables, such as "%c/...", %c will be empty upon
	* connecting since there's no channel name yet, so we have to make sure
	* we won't try to write to the FS root. On Windows we can be sure it's
	* full path if the 2nd character is a colon since Windows doesn't allow
	* colons in filenames.
	*/
#ifdef WIN32
	/* Treat it as full path if it
	* - starts with '\' which denotes the root directory of the current drive letter
	* - starts with a drive letter and followed by ':'
	*/
	if (prefs.hex_irc_logmask[0] == '\\' || (((prefs.hex_irc_logmask[0] >= 'A' && prefs.hex_irc_logmask[0] <= 'Z') || (prefs.hex_irc_logmask[0] >= 'a' && prefs.hex_irc_logmask[0] <= 'z')) && prefs.hex_irc_logmask[1] == ':'))
#else
	if (prefs.hex_irc_logmask[0] == '/')
#endif
	{
		return true;
	}
	else
	{
		return false;
	}
}

static boost::filesystem::path log_create_pathname(const char *servname, const char *channame, const char *netname)
{
	namespace bfs = boost::filesystem;

	std::string net_name = !netname ? std::string("NETWORK") : log_create_filename(netname);

	/* first, everything is in UTF-8 */
	std::string chan_name = !rfc_casecmp(channame, servname) ? std::string("server") : log_create_filename(channame);

	char fname[384];
	log_insert_vars(fname, sizeof(fname), prefs.hex_irc_logmask, chan_name.c_str(), net_name.c_str(), servname);

	/* insert time/date */
	auto now = time(nullptr);
	char fnametime[384];
	strftime_utf8(fnametime, sizeof(fnametime), fname, now);
	bfs::path ret;
	/* create final path/filename */
	if (logmask_is_fullpath())
	{
		ret = fnametime;
	}
	else	/* relative path */
	{
		ret = bfs::path(config::config_dir()) / "logs" / fnametime;
	}

	/* create all the subdirectories */
	boost::system::error_code ec;
	bfs::create_directories(ret.parent_path(), ec);

	return ret;
}

static int log_open_file(const char *servname, const char *channame, const char *netname)
{
	auto file = log_create_pathname(servname, channame, netname);
	int fd;
#ifdef WIN32
	fd = _wopen(file.c_str(), O_CREAT | O_APPEND | O_WRONLY | O_BINARY, S_IREAD | S_IWRITE);
#else
	fd = open(file.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0644);
#endif

	if (fd == -1)
		return -1;
	auto currenttime = time(NULL);
	char buf[512];
	write(fd, buf,
		snprintf(buf, sizeof(buf), _("**** BEGIN LOGGING AT %s\n"),
		std::ctime(&currenttime)));

	return fd;
}

static void log_open(session &sess)
{
	static bool log_error = false;

	log_close(sess);
	sess.logfd = log_open_file(sess.server->servername, sess.channel,
		sess.server->get_network(false).data());

	if (!log_error && sess.logfd == -1)
	{
		auto path = log_create_pathname(sess.server->servername, sess.channel, sess.server->get_network(false).data());
		std::ostringstream message;
		message << boost::format(_("* Can't open log file(s) for writing. Check the\npermissions on %s")) % path;

		fe_message(message.str(), FE_MSG_WAIT | FE_MSG_ERROR);
		log_error = true;
	}
}

void log_open_or_close(session *sess)
{
	if (sess->text_logging == SET_DEFAULT)
	{
		if (prefs.hex_irc_logging)
			log_open(*sess);
		else
			log_close(*sess);
	}
	else
	{
		if (sess->text_logging)
			log_open(*sess);
		else
			log_close(*sess);
	}
}

void log_write(session &sess, const std::string & text, time_t ts)
{
	if (sess.text_logging == SET_DEFAULT)
	{
		if (!prefs.hex_irc_logging)
			return;
	}
	else
	{
		if (sess.text_logging != SET_ON)
			return;
	}

	if (sess.logfd == -1)
		log_open(sess);

	/* change to a different log file? */
	auto file = log_create_pathname(sess.server->servername, sess.channel,
		sess.server->get_network(false).data());
	boost::system::error_code ec;
	if (!boost::filesystem::exists(file, ec))
	{
		close(sess.logfd);
		sess.logfd = log_open_file(sess.server->servername, sess.channel,
			sess.server->get_network(false).data());
	}

	if (prefs.hex_stamp_log)
	{
		if (!ts) ts = time(0);
		auto stamp = get_stamp_str(prefs.hex_stamp_log_format, ts);
		if (!stamp.empty())
		{
			write(sess.logfd, stamp.c_str(), stamp.size());
		}
	}
	auto temp = strip_color(text, STRIP_ALL);
	write(sess.logfd, temp.c_str(), temp.size());
	/* lots of scripts/plugins print without a \n at the end */
	if (!temp.empty() && temp.back() != '\n')
		write(sess.logfd, "\n", 1);	/* emulate what xtext would display */
}