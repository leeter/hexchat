/* X-Chat
 * Copyright (C) 1998 Peter Zelezny.
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
#define NOMINMAX
#endif

#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <ctime>
#include <cwchar>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <boost/algorithm/string_regex.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/format.hpp>
#include <boost/optional.hpp>
#include <boost/utility/string_ref.hpp>


#ifdef WIN32
#include <Windows.h>
#include <mmsystem.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/mman.h>
#endif

#include "hexchat.hpp"
#include "cfgfiles.hpp"
#include "chanopt.hpp"
#include "plugin.hpp"
#include "fe.hpp"
#include "filesystem.hpp"
#include "server.hpp"
#include "util.hpp"
#include "outbound.hpp"
#include "hexchatc.hpp"
#include "text.hpp"
#include "typedef.h"
#include "session.hpp"

#ifdef USE_LIBCANBERRA
#include <canberra.h>
#endif

#ifdef USE_LIBCANBERRA
static ca_context *ca_con;
#endif

static std::string log_create_filename (const std::string& channame);

static boost::optional<boost::filesystem::path> scrollback_get_filename (const session &sess)
{
	namespace bfs = boost::filesystem;
	const char * net = sess.server->get_network(false);
	if (!net)
		return boost::none;
	bfs::path path = bfs::path( config::config_dir() ) / "scrollback" / net / "";
	boost::system::error_code ec;
	bfs::create_directories(path, ec);

	auto chan = log_create_filename (sess.channel);
	if (chan.empty())
		return boost::none;

	return path / (chan + ".txt");
}

#if 0

static void
scrollback_unlock (session *sess)
{
	char buf[1024];

	if (!scrollback_get_filename (sess, buf, sizeof (buf) - 6))
		return;

	strcat (buf, ".lock");
	unlink (buf);
}

static gboolean
scrollback_lock (session *sess)
{
	char buf[1024];
	int fh;

	if (!scrollback_get_filename (sess, buf, sizeof (buf) - 6))
		return FALSE;

	strcat (buf, ".lock");

	if (access (buf, F_OK) == 0)
		return FALSE;	/* can't get lock */

	fh = open (buf, O_CREAT | O_TRUNC | O_APPEND | O_WRONLY, 0644);
	if (fh == -1)
		return FALSE;

	return TRUE;
}

#endif

void scrollback_close (session &sess)
{
	if (sess.scrollfd != -1)
	{
		close (sess.scrollfd);
		sess.scrollfd = -1;
	}
}

/* shrink the file to roughly prefs.hex_text_max_lines */

static void scrollback_shrink (session &sess)
{
	scrollback_close (sess);
	sess.scrollwritten = 0;
	int lines = 0;
	auto file = scrollback_get_filename(sess);
	if (!file)
	{
		return;
	}

	char *buf;
	gsize len;
	if (!g_file_get_contents (file->string().c_str(), &buf, &len, NULL))
	{
		return;
	}
	glib_string buf_ptr(buf);
	/* count all lines */
	auto p = buf;
	while (p != buf + len)
	{
		if (*p == '\n')
			lines++;
		p++;
	}

	int fh = g_open(file->string().c_str(), O_CREAT | O_TRUNC | O_APPEND | O_WRONLY, 0644);
	if (fh == -1)
	{
		return;
	}

	int line = 0;
	p = buf;
	while (p != buf + len)
	{
		if (*p == '\n')
		{
			line++;
			if (line >= lines - prefs.hex_text_max_lines &&
				 p + 1 != buf + len)
			{
				p++;
				write (fh, p, len - (p - buf));
				break;
			}
		}
		p++;
	}

	close (fh);
}

static void scrollback_save (session &sess, const std::string & text)
{
	if (sess.type == session::SESS_SERVER && prefs.hex_gui_tab_server == 1)
		return;

	if (sess.text_scrollback == SET_DEFAULT)
	{
		if (!prefs.hex_text_replay)
			return;
	}
	else
	{
		if (sess.text_scrollback != SET_ON)
			return;
	}

	if (sess.scrollfd == -1)
	{
		auto path = scrollback_get_filename(sess);
		if (!path)
			return;

		sess.scrollfd = g_open (path->string().c_str(), O_CREAT | O_APPEND | O_WRONLY, 0644);
		if (sess.scrollfd == -1)
			return;
	}

	auto stamp = time (0);
	glib_string buf(g_strdup_printf ("T %" G_GINT64_FORMAT " ", (gint64)stamp));
	write (sess.scrollfd, buf.get(), strlen (buf.get()));

	auto len = text.size();
	write (sess.scrollfd, text.c_str(), text.size());
	if (len && text[len - 1] != '\n')
		write (sess.scrollfd, "\n", 1);

	sess.scrollwritten++;

	if ((sess.scrollwritten * 2 > prefs.hex_text_max_lines && prefs.hex_text_max_lines > 0) ||
	   sess.scrollwritten > 32000)
		scrollback_shrink (sess);
}

void scrollback_load (session &sess)
{
	if (sess.text_scrollback == SET_DEFAULT)
	{
		if (!prefs.hex_text_replay)
			return;
	}
	else if (sess.text_scrollback != SET_ON)
	{
		return;
	}
	
	auto path = scrollback_get_filename(sess);
	if (!path)
		return;
	GError *file_error = nullptr;
	std::unique_ptr<GIOChannel, decltype(&g_io_channel_unref)> io(g_io_channel_new_file (path->string().c_str(), "r", &file_error), g_io_channel_unref);
	if (!io)
		return;

	int lines = 0;
	char *text;
	time_t stamp;

	while (1)
	{
		gsize n_bytes;
		gchar* buf_ptr;
		GError *io_err = nullptr;
		auto io_status = g_io_channel_read_line (io.get(), &buf_ptr, &n_bytes, nullptr, &io_err);
		glib_string buf(buf_ptr);
		if (io_status != G_IO_STATUS_NORMAL)
		{
			break;
		}

		/* If nothing but funny trailing matter e.g. 0x0d or 0x0d0a, toss it */
		if (n_bytes >= 1 && buf[0] == 0x0d)
		{
			continue;
		}

		n_bytes--;
		auto buf_tmp = buf.get();
		buf.reset(g_strndup(buf_tmp, n_bytes));

		/*
		* Some scrollback lines have three blanks after the timestamp and a newline
		* Some have only one blank and a newline
		* Some don't even have a timestamp
		* Some don't have any text at all
		*/
		if (buf[0] == 'T')
		{
			stamp = strtoull(buf.get() + 2, NULL, 10); /* in case time_t is 64 bits */
			text = strchr(buf.get() + 3, ' ');
			if (text && text[1])
			{
				std::string temp;
				if (prefs.hex_text_stripcolor_replay)
				{
					temp = strip_color(text + 1, STRIP_COLOR);
					text = &temp[0];
				}

				fe_print_text(sess, text, stamp, TRUE);
			}
			else
			{
				fe_print_text(sess, "  ", stamp, TRUE);
			}
		}
		else
		{
			if (strlen(buf.get()))
				fe_print_text(sess, buf.get(), 0, TRUE);
			else
				fe_print_text(sess, "  ", 0, TRUE);
		}
		lines++;
	}

	sess.scrollwritten = lines;

	if (lines)
	{
		text = ctime (&stamp);
		text[24] = 0;	/* get rid of the \n */
		glib_string buf(g_strdup_printf ("\n*\t%s %s\n\n", _("Loaded log from"), text));
		fe_print_text (sess, buf.get(), 0, TRUE);
		/*EMIT_SIGNAL (XP_TE_GENMSG, sess, "*", buf, NULL, NULL, NULL, 0);*/
	}
}

void log_close (session &sess)
{
	if (sess.logfd != -1)
	{
		std::time_t currenttime = std::time (nullptr);
		std::ostringstream stream;
		stream << boost::format(_("**** ENDING LOGGING AT %s\n")) % std::ctime(&currenttime);
		auto to_output = stream.str();
		write (sess.logfd, to_output.c_str(), to_output.length());
		close (sess.logfd);
		sess.logfd = -1;
	}
}

static std::string log_create_filename (const std::string & channame)
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

/* like strcpy, but % turns into %% */

static char * log_escape_strcpy (char *dest, const char *src, const char *end)
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

static void log_insert_vars (char *buf, size_t bufsize, const char *fmt, const char *c, const char *n, const char *s)
{
	char *end = buf + bufsize;

	while (1)
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
				buf = log_escape_strcpy (buf, c, end);
				break;
			case 'n':
				buf = log_escape_strcpy (buf, n, end);
				break;
			case 's':
				buf = log_escape_strcpy (buf, s, end);
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

static bool logmask_is_fullpath ()
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

static boost::filesystem::path log_create_pathname (const char *servname, const char *channame, const char *netname)
{
	namespace bfs = boost::filesystem;

	std::string net_name = !netname ? std::string("NETWORK") : log_create_filename(netname);

	/* first, everything is in UTF-8 */
	std::string chan_name = !rfc_casecmp(channame, servname) ? std::string("server") : log_create_filename(channame);
	
	char fname[384];
	log_insert_vars (fname, sizeof (fname), prefs.hex_irc_logmask, chan_name.c_str(), net_name.c_str(), servname);

	/* insert time/date */
	auto now = time (nullptr);
	char fnametime[384];
	strftime_utf8 (fnametime, sizeof (fnametime), fname, now);
	bfs::path ret;
	/* create final path/filename */
	if (logmask_is_fullpath ())
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

static int log_open_file (const char *servname, const char *channame, const char *netname)
{
	auto file = log_create_pathname (servname, channame, netname);
	int fd;
#ifdef WIN32
	fd = _wopen (file.c_str(), O_CREAT | O_APPEND | O_WRONLY, S_IREAD|S_IWRITE);
#else
	fd = open (file.c_str(), O_CREAT | O_APPEND | O_WRONLY, 0644);
#endif

	if (fd == -1)
		return -1;
	auto currenttime = time (NULL);
	char buf[512];
	write (fd, buf,
			 snprintf (buf, sizeof (buf), _("**** BEGIN LOGGING AT %s\n"),
						  std::ctime (&currenttime)));

	return fd;
}

static void log_open (session &sess)
{
	static bool log_error = false;

	log_close (sess);
	sess.logfd = log_open_file (sess.server->servername, sess.channel,
		sess.server->get_network(false));

	if (!log_error && sess.logfd == -1)
	{
		auto path = log_create_pathname(sess.server->servername, sess.channel, sess.server->get_network(false));
		std::ostringstream message;
		message << boost::format(_("* Can't open log file(s) for writing. Check the\npermissions on %s")) % path;

		fe_message (message.str(), FE_MSG_WAIT | FE_MSG_ERROR);
		log_error = true;
	}
}

void log_open_or_close (session *sess)
{
	if (sess->text_logging == SET_DEFAULT)
	{
		if (prefs.hex_irc_logging)
			log_open (*sess);
		else
			log_close (*sess);
	}
	else
	{
		if (sess->text_logging)
			log_open (*sess);
		else
			log_close (*sess);
	}
}

gsize get_stamp_str (const char fmt[], time_t tim, char **ret)
{
	glib_string loc;

	/* strftime wants the format string in LOCALE! */
	if (!prefs.utf8_locale)
	{
		const gchar *charset;

		g_get_charset (&charset);
		loc.reset(g_convert_with_fallback (fmt, -1, charset, "UTF-8", "?", 0, 0, 0));
		if (loc)
			fmt = loc.get();
	}

	char dest[128];
	auto len = strftime_validated (dest, sizeof (dest), fmt, localtime (&tim));
	if (len)
	{
		if (prefs.utf8_locale)
			*ret = g_strdup (dest);
		else
			*ret = g_locale_to_utf8 (dest, len, 0, &len, 0);
	}

	return len;
}

static void log_write (session &sess, const std::string & text, time_t ts)
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
		log_open (sess);

	/* change to a different log file? */
	auto file = log_create_pathname (sess.server->servername, sess.channel,
		sess.server->get_network(false));
	boost::system::error_code ec;
	if (!boost::filesystem::exists(file, ec))
	{
		close(sess.logfd);
		sess.logfd = log_open_file(sess.server->servername, sess.channel,
			sess.server->get_network(false));
	}

	if (prefs.hex_stamp_log)
	{
		if (!ts) ts = time(0);
		char* stamp;
		auto len = get_stamp_str (prefs.hex_stamp_log_format, ts, &stamp);
		if (len)
		{
			glib_string stamp_ptr(stamp);
			write (sess.logfd, stamp, len);
		}
	}
	auto temp = strip_color (text, STRIP_ALL);
	write (sess.logfd, temp.c_str(), temp.size());
	/* lots of scripts/plugins print without a \n at the end */
	if (!temp.empty() && temp.back() != '\n')
		write (sess.logfd, "\n", 1);	/* emulate what xtext would display */
}

/* converts a CP1252/ISO-8859-1(5) hybrid to UTF-8                           */
/* Features: 1. It never fails, all 00-FF chars are converted to valid UTF-8 */
/*           2. Uses CP1252 in the range 80-9f because ISO doesn't have any- */
/*              thing useful in this range and it helps us receive from mIRC */
/*           3. The five undefined chars in CP1252 80-9f are replaced with   */
/*              ISO-8859-15 control codes.                                   */
/*           4. Handles 0xa4 as a Euro symbol ala ISO-8859-15.               */
/*           5. Uses ISO-8859-1 (which matches CP1252) for everything else.  */
/*           6. This routine measured 3x faster than g_convert :)            */

static unsigned char *
iso_8859_1_to_utf8 (const unsigned char *text, int len, gsize *bytes_written)
{
	typedef std::basic_ostringstream<unsigned char> utf8ostringstream;
	//unsigned char *res, *output;
	static const unsigned short lowtable[] = /* 74 byte table for 80-a4 */
	{
	/* compressed utf-8 table: if the first byte's 0x20 bit is set, it
	   indicates a 2-byte utf-8 sequence, otherwise prepend a 0xe2. */
		0x82ac, /* 80 Euro. CP1252 from here on... */
		0xe281, /* 81 NA */
		0x809a, /* 82 */
		0xe692, /* 83 */
		0x809e, /* 84 */
		0x80a6, /* 85 */
		0x80a0, /* 86 */
		0x80a1, /* 87 */
		0xeb86, /* 88 */
		0x80b0, /* 89 */
		0xe5a0, /* 8a */
		0x80b9, /* 8b */
		0xe592, /* 8c */
		0xe28d, /* 8d NA */
		0xe5bd, /* 8e */
		0xe28f, /* 8f NA */
		0xe290, /* 90 NA */
		0x8098, /* 91 */
		0x8099, /* 92 */
		0x809c, /* 93 */
		0x809d, /* 94 */
		0x80a2, /* 95 */
		0x8093, /* 96 */
		0x8094, /* 97 */
		0xeb9c, /* 98 */
		0x84a2, /* 99 */
		0xe5a1, /* 9a */
		0x80ba, /* 9b */
		0xe593, /* 9c */
		0xe29d, /* 9d NA */
		0xe5be, /* 9e */
		0xe5b8, /* 9f */
		0xe2a0, /* a0 */
		0xe2a1, /* a1 */
		0xe2a2, /* a2 */
		0xe2a3, /* a3 */
		0x82ac  /* a4 ISO-8859-15 Euro. */
	};

	if (len == -1)
		len = std::char_traits<unsigned char>::length(text);

	/* worst case scenario: every byte turns into 3 bytes */
	utf8ostringstream output_stream;
	std::ostream_iterator<unsigned char, unsigned char> output(output_stream);
	while (len)
	{
		if (G_LIKELY (*text < 0x80))
		{
			*output = *text;	/* ascii maps directly */
		}
		else if (*text <= 0xa4)	/* 80-a4 use a lookup table */
		{
			auto idx = *text - 0x80;
			if (lowtable[idx] & 0x2000)
			{
				*output++ = (lowtable[idx] >> 8) & 0xdf; /* 2 byte utf-8 */
				*output = lowtable[idx] & 0xff;
			}
			else
			{
				*output++ = 0xe2;	/* 3 byte utf-8 */
				*output++ = (lowtable[idx] >> 8) & 0xff;
				*output = lowtable[idx] & 0xff;
			}
		}
		else if (*text < 0xc0)
		{
			*output++ = 0xc2;
			*output = *text;
		}
		else
		{
			*output++ = 0xc3;
			*output = *text - 0x40;
		}
		output++;
		text++;
		len--;
	}
	//*output = 0;	/* terminate */
	*bytes_written = output_stream.tellp();

	return (unsigned char*)g_strdup((const char*)output_stream.str().c_str());
}

// deprecated should be removed as soon as possible... we should be using UTF-8 everywhere
char * text_validate (char **text, size_t *len)
{
	char *utf;
	gsize utf_len;

	/* valid utf8? */
	if (g_utf8_validate (*text, *len, 0))
		return NULL;

#ifdef WIN32
	if (GetACP () == 1252) /* our routine is better than iconv's 1252 */
#else
	if (prefs.utf8_locale)
#endif
		/* fallback to iso-8859-1 */
		utf = (char*)iso_8859_1_to_utf8 ((unsigned char*) *text, *len, &utf_len);
	else
	{
		/* fallback to locale */
		utf = g_locale_to_utf8 (*text, *len, 0, &utf_len, NULL);
		if (!utf)
			utf = (char*)iso_8859_1_to_utf8((unsigned char*)*text, *len, &utf_len);
	}

	if (!utf) 
	{
		*text = g_strdup ("%INVALID%");
		*len = 9;
	} else
	{
		*text = utf;
		*len = utf_len;
	}

	return utf;
}

void PrintTextTimeStamp(session *sess, const boost::string_ref & text, time_t timestamp)
{
	// putting this in here to help track down places that are sending in invalid UTF-8
	/*if (!g_utf8_validate(text.c_str(), text.size(), nullptr))
	{
		throw std::invalid_argument("text must be valid utf8");
	}*/
	if (!sess)
	{
		if (!sess_list)
			return;
		sess = static_cast<session *>(sess_list->data);
	}
	
	std::string buf = text.to_string();
	glib_string conv;
	/* make sure it's valid utf8 */
	if (buf.empty())
	{
		buf = "\n";
		//buf.push_back(0);
	}
	if (!g_utf8_validate(buf.c_str(), buf.size(), nullptr))
	{
		size_t len = 0;
		//buf.push_back(0);
		char* buf_ptr = &buf[0];
		conv.reset(text_validate (&buf_ptr, &len));
	}

	log_write(*sess, buf, timestamp);
	scrollback_save(*sess, buf);
	fe_print_text(*sess, &buf[0], timestamp, FALSE);
}

void PrintText (session *sess, const std::string & text)
{
	PrintTextTimeStamp (sess, text, 0);
}

void PrintTextf(session * sess, const boost::format & fmt)
{
	std::ostringstream outbuf;
	outbuf << fmt;
	PrintText(sess, outbuf.str());
}

void PrintTextf (session *sess, const char format[], ...)
{
	va_list args;

	va_start (args, format);
	glib_string buf(g_strdup_vprintf (format, args));
	va_end (args);

	PrintText (sess, buf.get());
}

void PrintTextTimeStampf (session *sess, time_t timestamp, const char format[], ...)
{
	va_list args;

	va_start (args, format);
	glib_string buf{ g_strdup_vprintf(format, args) };
	va_end (args);

	PrintTextTimeStamp (sess, buf.get(), timestamp);
}

/* Print Events stuff here --AGL */

/* Consider the following a NOTES file:

   The main upshot of this is:
   * Plugins and Perl scripts (when I get round to signaling perl.c) can intercept text events and do what they like
   * The default text engine can be config'ed

   By default it should appear *exactly* the same (I'm working hard not to change the default style) but if you go into Settings->Edit Event Texts you can change the text's. The format is thus:

   The normal %Cx (color) and %B (bold) etc work

   $x is replaced with the data in var x (e.g. $1 is often the nick)

   $axxx is replace with a single byte of value xxx (in base 10)

   AGL (990507)
 */

/* These lists are thus:
   pntevts_text[] are the strings the user sees (WITH %x etc)
   pntevts[] are the data strings with \000 etc
 */

/* To add a new event:

   Think up a name (like "Join")
   Make up a pevt_name_help struct
	Add an entry to textevents.in
	Type: make textevents
 */

/* Internals:

   On startup ~/.xchat/printevents.conf is loaded if it doesn't exist the
   defaults are loaded. Any missing events are filled from defaults.
   Each event is parsed by pevt_build_string and a binary output is produced
   which looks like:

   (byte) value: 0 = {
   (int) numbers of bytes
   (char []) that number of byte to be memcpy'ed into the buffer
   }
   1 =
   (byte) number of variable to insert
   2 = end of buffer

   Each XP_TE_* signal is hard coded to call text_emit which calls
   display_event which decodes the data

   This means that this system *should be faster* than snprintf because
   it always 'knows' that format of the string (basically is preparses much
   of the work)

   --AGL
 */
std::array<std::string, NUM_XP> pntevts_text;
std::array<std::string, NUM_XP> pntevts;

#define pevt_generic_none_help nullptr

static const char * const pevt_genmsg_help[] = {
	N_("Left message"),
	N_("Right message"),
};

#if 0
static char * const pevt_identd_help[] = {
	N_("IP address"),
	N_("Username")
};
#endif

static const char * const pevt_join_help[] = {
	N_("The nick of the joining person"),
	N_("The channel being joined"),
	N_("The host of the person"),
	N_("The account of the person"),
};

static const char * const pevt_chanaction_help[] = {
	N_("Nickname"),
	N_("The action"),
	N_("Mode char"),
	N_("Identified text"),
};

static const char * const pevt_chanmsg_help[] = {
	N_("Nickname"),
	N_("The text"),
	N_("Mode char"),
	N_("Identified text"),
};

static const char * const pevt_privmsg_help[] = {
	N_("Nickname"),
	N_("The message"),
	N_("Identified text")
};

static const char * const pevt_capack_help[] = {
	N_("Server Name"),
	N_("Acknowledged Capabilities")
};

static const char * const pevt_caplist_help[] = {
	N_("Server Name"),
	N_("Server Capabilities")
};

static const char * const pevt_capreq_help[] = {
	N_("Requested Capabilities")
};

static const char * const pevt_changenick_help[] = {
	N_("Old nickname"),
	N_("New nickname"),
};

static const char * const pevt_newtopic_help[] = {
	N_("Nick of person who changed the topic"),
	N_("Topic"),
	N_("Channel"),
};

static const char * const pevt_topic_help[] = {
	N_("Channel"),
	N_("Topic"),
};

static const char * const pevt_kick_help[] = {
	N_("The nickname of the kicker"),
	N_("The person being kicked"),
	N_("The channel"),
	N_("The reason"),
};

static const char * const pevt_part_help[] = {
	N_("The nick of the person leaving"),
	N_("The host of the person"),
	N_("The channel"),
};

static const char * const pevt_chandate_help[] = {
	N_("The channel"),
	N_("The time"),
};

static const char * const pevt_topicdate_help[] = {
	N_("The channel"),
	N_("The creator"),
	N_("The time"),
};

static const char * const pevt_quit_help[] = {
	N_("Nick"),
	N_("Reason"),
	N_("Host"),
};

static const char * const pevt_pingrep_help[] = {
	N_("Who it's from"),
	N_("The time in x.x format (see below)"),
};

static const char * const pevt_notice_help[] = {
	N_("Who it's from"),
	N_("The message"),
};

static const char * const pevt_channotice_help[] = {
	N_("Who it's from"),
	N_("The Channel it's going to"),
	N_("The message"),
};

static const char * const pevt_uchangenick_help[] = {
	N_("Old nickname"),
	N_("New nickname"),
};

static const char * const pevt_ukick_help[] = {
	N_("The person being kicked"),
	N_("The channel"),
	N_("The nickname of the kicker"),
	N_("The reason"),
};

static const char * const pevt_partreason_help[] = {
	N_("The nick of the person leaving"),
	N_("The host of the person"),
	N_("The channel"),
	N_("The reason"),
};

static const char * const pevt_ctcpsnd_help[] = {
	N_("The sound"),
	N_("The nick of the person"),
	N_("The channel"),
};

static const char * const pevt_ctcpgen_help[] = {
	N_("The CTCP event"),
	N_("The nick of the person"),
};

static const char * const pevt_ctcpgenc_help[] = {
	N_("The CTCP event"),
	N_("The nick of the person"),
	N_("The Channel it's going to"),
};

static const char * const pevt_chansetkey_help[] = {
	N_("The nick of the person who set the key"),
	N_("The key"),
};

static const char * const pevt_chansetlimit_help[] = {
	N_("The nick of the person who set the limit"),
	N_("The limit"),
};

static const char * const pevt_chanop_help[] = {
	N_("The nick of the person who did the op'ing"),
	N_("The nick of the person who has been op'ed"),
};

static const char * const pevt_chanhop_help[] = {
	N_("The nick of the person who has been halfop'ed"),
	N_("The nick of the person who did the halfop'ing"),
};

static const char * const pevt_chanvoice_help[] = {
	N_("The nick of the person who did the voice'ing"),
	N_("The nick of the person who has been voice'ed"),
};

static const char * const pevt_chanban_help[] = {
	N_("The nick of the person who did the banning"),
	N_("The ban mask"),
};

static const char * const pevt_chanquiet_help[] = {
	N_("The nick of the person who did the quieting"),
	N_("The quiet mask"),
};

static const char * const pevt_chanrmkey_help[] = {
	N_("The nick who removed the key"),
};

static const char * const pevt_chanrmlimit_help[] = {
	N_("The nick who removed the limit"),
};

static const char * const pevt_chandeop_help[] = {
	N_("The nick of the person of did the deop'ing"),
	N_("The nick of the person who has been deop'ed"),
};
static const char * const pevt_chandehop_help[] = {
	N_("The nick of the person of did the dehalfop'ing"),
	N_("The nick of the person who has been dehalfop'ed"),
};

static const char * const pevt_chandevoice_help[] = {
	N_("The nick of the person of did the devoice'ing"),
	N_("The nick of the person who has been devoice'ed"),
};

static const char * const pevt_chanunban_help[] = {
	N_("The nick of the person of did the unban'ing"),
	N_("The ban mask"),
};

static const char * const pevt_chanunquiet_help[] = {
	N_("The nick of the person of did the unquiet'ing"),
	N_("The quiet mask"),
};

static const char * const pevt_chanexempt_help[] = {
	N_("The nick of the person who did the exempt"),
	N_("The exempt mask"),
};

static const char * const pevt_chanrmexempt_help[] = {
	N_("The nick of the person removed the exempt"),
	N_("The exempt mask"),
};

static const char * const pevt_chaninvite_help[] = {
	N_("The nick of the person who did the invite"),
	N_("The invite mask"),
};

static const char * const pevt_chanrminvite_help[] = {
	N_("The nick of the person removed the invite"),
	N_("The invite mask"),
};

static const char * const pevt_chanmodegen_help[] = {
	N_("The nick of the person setting the mode"),
	N_("The mode's sign (+/-)"),
	N_("The mode letter"),
	N_("The channel it's being set on"),
};

static const char * const pevt_whois1_help[] = {
	N_("Nickname"),
	N_("Username"),
	N_("Host"),
	N_("Full name"),
};

static const char * const pevt_whois2_help[] = {
	N_("Nickname"),
	N_("Channel Membership/\"is an IRC operator\""),
};

static const char * const pevt_whois3_help[] = {
	N_("Nickname"),
	N_("Server Information"),
};

static const char * const pevt_whois4_help[] = {
	N_("Nickname"),
	N_("Idle time"),
};

static const char * const pevt_whois4t_help[] = {
	N_("Nickname"),
	N_("Idle time"),
	N_("Signon time"),
};

static const char * const pevt_whois5_help[] = {
	N_("Nickname"),
	N_("Away reason"),
};

static const char * const pevt_whois6_help[] = {
	N_("Nickname"),
};

static const char * const pevt_whoisid_help[] = {
	N_("Nickname"),
	N_("Message"),
	"Numeric"
};

static const char * const pevt_whoisauth_help[] = {
	N_("Nickname"),
	N_("Message"),
	N_("Account"),
};

static const char * const pevt_whoisrealhost_help[] = {
	N_("Nickname"),
	N_("Real user@host"),
	N_("Real IP"),
	N_("Message"),
};

static const char * const pevt_generic_channel_help[] = {
	N_("Channel Name"),
};

static const char * const pevt_saslauth_help[] = {
	N_("Username"),
	N_("Mechanism")
};

static const char * const pevt_saslresponse_help[] = {
	N_("Server Name"),
	N_("Raw Numeric or Identifier"),
	N_("Username"),
	N_("Message")
};

static const char * const pevt_servertext_help[] = {
	N_("Text"),
	N_("Server Name"),
	N_("Raw Numeric or Identifier")
};

static const char * const pevt_sslmessage_help[] = {
	N_("Text"),
	N_("Server Name")
};

static const char * const pevt_invited_help[] = {
	N_("Channel Name"),
	N_("Nick of person who invited you"),
	N_("Server Name"),
};

static const char * const pevt_usersonchan_help[] = {
	N_("Channel Name"),
	N_("Users"),
};

static const char * const pevt_nickclash_help[] = {
	N_("Nickname in use"),
	N_("Nick being tried"),
};

static const char * const pevt_connfail_help[] = {
	N_("Error"),
};

static const char * const pevt_connect_help[] = {
	N_("Host"),
	N_("IP"),
	N_("Port"),
};

static const char * const pevt_sconnect_help[] = {
	"PID"
};

static const char * const pevt_generic_nick_help[] = {
	N_("Nickname"),
	N_("Server Name"),
	N_("Network")
};

static const char * const pevt_chanmodes_help[] = {
	N_("Channel Name"),
	N_("Modes string"),
};

static const char * const pevt_chanurl_help[] = {
	N_("Channel Name"),
	N_("URL"),
};

static const char * const pevt_rawmodes_help[] = {
	N_("Nickname"),
	N_("Modes string"),
};

static const char * const pevt_kill_help[] = {
	N_("Nickname"),
	N_("Reason"),
};

static const char * const pevt_dccchaterr_help[] = {
	N_("Nickname"),
	N_("IP address"),
	N_("Port"),
	N_("Error"),
};

static const char * const pevt_dccstall_help[] = {
	N_("DCC Type"),
	N_("Filename"),
	N_("Nickname"),
};

static const char * const pevt_generic_file_help[] = {
	N_("Filename"),
	N_("Error"),
};

static const char * const pevt_dccrecverr_help[] = {
	N_("Filename"),
	N_("Destination filename"),
	N_("Nickname"),
	N_("Error"),
};

static const char * const pevt_dccrecvcomp_help[] = {
	N_("Filename"),
	N_("Destination filename"),
	N_("Nickname"),
	N_("CPS"),
};

static const char * const pevt_dccconfail_help[] = {
	N_("DCC Type"),
	N_("Nickname"),
	N_("Error"),
};

static const char * const pevt_dccchatcon_help[] = {
	N_("Nickname"),
	N_("IP address"),
};

static const char * const pevt_dcccon_help[] = {
	N_("Nickname"),
	N_("IP address"),
	N_("Filename"),
};

static const char * const pevt_dccsendfail_help[] = {
	N_("Filename"),
	N_("Nickname"),
	N_("Error"),
};

static const char * const pevt_dccsendcomp_help[] = {
	N_("Filename"),
	N_("Nickname"),
	N_("CPS"),
};

static const char * const pevt_dccoffer_help[] = {
	N_("Filename"),
	N_("Nickname"),
	N_("Pathname"),
};

static const char * const pevt_dccfileabort_help[] = {
	N_("Nickname"),
	N_("Filename")
};

static const char * const pevt_dccchatabort_help[] = {
	N_("Nickname"),
};

static const char * const pevt_dccresumeoffer_help[] = {
	N_("Nickname"),
	N_("Filename"),
	N_("Position"),
};

static const char * const pevt_dccsendoffer_help[] = {
	N_("Nickname"),
	N_("Filename"),
	N_("Size"),
	N_("IP address"),
};

static const char * const pevt_dccgenericoffer_help[] = {
	N_("DCC String"),
	N_("Nickname"),
};

static const char * const pevt_notifyaway_help[] = {
	N_("Nickname"),
	N_("Away Reason"),
};

static const char * const pevt_notifynumber_help[] = {
	N_("Number of notify items"),
};

static const char * const pevt_serverlookup_help[] = {
	N_("Server Name"),
};

static const char * const pevt_servererror_help[] = {
	N_("Text"),
};

static const char * const pevt_foundip_help[] = {
	N_("IP"),
};

static const char * const pevt_dccrename_help[] = {
	N_("Old Filename"),
	N_("New Filename"),
};

static const char * const pevt_ctcpsend_help[] = {
	N_("Receiver"),
	N_("Message"),
};

static const char * const pevt_ignoreaddremove_help[] = {
	N_("Hostmask"),
};

static const char * const pevt_resolvinguser_help[] = {
	N_("Nickname"),
	N_("Hostname"),
};

static const char * const pevt_malformed_help[] = {
	N_("Nickname"),
	N_("The Packet"),
};

static const char * const pevt_pingtimeout_help[] = {
	N_("Seconds"),
};

static const char * const pevt_uinvite_help[] = {
	N_("Nick of person who have been invited"),
	N_("Channel Name"),
	N_("Server Name"),
};

static const char * const pevt_banlist_help[] = {
	N_("Channel"),
	N_("Banmask"),
	N_("Who set the ban"),
	N_("Ban time"),
};

static const char * const pevt_discon_help[] = {
	N_("Error"),
};

#include "textevents.h"

static void pevent_load_defaults ()
{
	for (int i = 0; i < NUM_XP; i++)
	{
		/* make-te.c sets this 128 flag (DON'T call gettext() flag) */
		if (te[i].num_args & 128)
			pntevts_text[i] = te[i].def;
		else
			pntevts_text[i] = _(te[i].def);
	}
}

void pevent_make_pntevts ()
{
	char out[1024];

	for (int i = 0; i < NUM_XP; i++)
	{
		int m;
		if (pevt_build_string (pntevts_text[i], pntevts[i], m) != 0)
		{
			snprintf (out, sizeof (out),
						 _("Error parsing event %s.\nLoading default."), te[i].name);
			fe_message (out, FE_MSG_WARN);
			/* make-te.c sets this 128 flag (DON'T call gettext() flag) */
			if (te[i].num_args & 128)
				pntevts_text[i] = te[i].def;
			else
				pntevts_text[i] = _(te[i].def);
			if (pevt_build_string (pntevts_text[i], pntevts[i], m) != 0)
			{
				fprintf (stderr,
							"HexChat CRITICAL *** default event text failed to build!\n");
				abort ();
			}
		}
	}
}

/* Loading happens at 2 levels:
   1) File is read into blocks
   2) Pe block is parsed and loaded

   --AGL */

/* Better hope you pass good args.. --AGL */

static void pevent_trigger_load (int *i_penum, char **i_text, char **i_snd)
{
	int penum = *i_penum;
	char *text = *i_text, *snd = *i_snd;

	if (penum != -1 && text != NULL)
	{
		pntevts_text[penum] = text;
	}

	delete[] text;
	delete[] snd;
	*i_text = NULL;
	*i_snd = NULL;
	*i_penum = 0;
}

static int pevent_find (const char name[], int &i_i)
{
	int i = i_i, j;

	j = i + 1;
	while (1)
	{
		if (j == NUM_XP)
			j = 0;
		if (strcmp (te[j].name, name) == 0)
		{
			i_i = j;
			return j;
		}
		if (j == i)
			return -1;
		j++;
	}
}

int pevent_load (const char *filename)
{
	/* AGL, I've changed this file and pevent_save, could you please take a look at
	 *      the changes and possibly modify them to suit you
	 *      //David H
	 */
	int fd, i = 0;
	struct stat st;
	char *text = NULL, *snd = NULL;
	int penum = 0;

	if (filename == NULL)
		fd = hexchat_open_file ("pevents.conf", O_RDONLY, 0, 0);
	else
		fd = hexchat_open_file (filename, O_RDONLY, 0, io::fs::XOF_FULLPATH);

	if (fd == -1)
		return 1;
	if (fstat (fd, &st) != 0)
	{
		close (fd);
		return 1;
	}
	std::string ibufr(st.st_size, '\0');
	read(fd, &ibufr[0], st.st_size);
	close (fd);
	std::istringstream buffer(ibufr);
	for (std::string line; std::getline(buffer, line, '\n');)
	{
		if (line.empty())
			continue;
		if (line[0] == '#')
			continue;

		auto ofs = line.find_first_of('=');
		if (ofs == std::string::npos)
			continue;
		auto first_part = line.substr(0, ofs);
		auto second_part = line.substr(ofs + 1);

		if (first_part == "event_name")
		{
			if (penum >= 0)
				pevent_trigger_load (&penum, &text, &snd);
			penum = pevent_find (&second_part[0], i);
			continue;
		}
		else if (first_part == "event_text")
		{
			delete[] text;

#if 0
			/* This allows updating of old strings. We don't use new defaults
				if the user has customized the strings (.e.g a text theme).
				Hash of the old default is enough to identify and replace it.
				This only works in English. */

			switch (g_str_hash (ofs))
			{
			case 0x526743a4:
		/* %C08,02 Hostmask                  PRIV NOTI CHAN CTCP INVI UNIG %O */
				text = strdup (te[XP_TE_IGNOREHEADER].def);
				break;

			case 0xe91bc9c2:
		/* %C08,02                                                         %O */
				text = strdup (te[XP_TE_IGNOREFOOTER].def);
				break;

			case 0x1fbfdf22:
		/* -%C10-%C11-%O$tDCC RECV: Cannot open $1 for writing - aborting. */
				text = strdup (te[XP_TE_DCCFILEERR].def);
				break;

			default:
				text = strdup (ofs);
			}
#else
			text = new_strdup (second_part.c_str());
#endif

			continue;
		}/* else if (strcmp (buf, "event_sound") == 0)
		{
			if (snd)
				free (snd);
			snd = strdup (ofs);
			continue;
		}*/

		continue;
	}

	pevent_trigger_load (&penum, &text, &snd);
	return 0;
}

static void pevent_check_all_loaded ()
{
	for (int i = 0; i < NUM_XP; i++)
	{
		if (pntevts_text[i].empty())
		{
			/*printf ("%s\n", te[i].name);
			snprintf(out, sizeof(out), "The data for event %s failed to load. Reverting to defaults.\nThis may be because a new version of HexChat is loading an old config file.\n\nCheck all print event texts are correct", evtnames[i]);
			   gtkutil_simpledialog(out); */
			/* make-te.c sets this 128 flag (DON'T call gettext() flag) */
			if (te[i].num_args & 128)
				pntevts_text[i] = te[i].def;
			else
				pntevts_text[i] = _(te[i].def);
		}
	}
}

void load_text_events ()
{
	if (pevent_load (nullptr))
		pevent_load_defaults ();
	pevent_check_all_loaded ();
	pevent_make_pntevts ();
}

/*
	CL: format_event now handles filtering of arguments:
	1) if prefs.hex_text_stripcolor_msg is set, filter all style control codes from arguments
	2) always strip \010 (ATTR_HIDDEN) from arguments: it is only for use in the format string itself
*/
#define ARG_FLAG(argn) (1 << (argn))

void format_event (session *sess, int index, char **args, char *dst, size_t dstsize, unsigned int stripcolor_args)
{
	if (index < 0 || index > sizeof(pntevts))
		throw std::invalid_argument("Invalid index");
	int len, output_index, input_index, numargs;
	bool done_all = false;

	const std::string& display_evt = pntevts[index];
	numargs = te[index].num_args & 0x7f;

	output_index = input_index = len = 0;
	dst[0] = 0;

	if (display_evt.empty())
		return;

	while (!done_all)
	{
		char d = display_evt[input_index++];
		switch (d)
		{
		case 0:
			memcpy (&len, &(display_evt[input_index]), sizeof (int));
			input_index += sizeof (int);
			if (output_index + len > dstsize)
			{
				fprintf(stderr, "Overflow in display_event (%s)\n", display_evt.c_str());
				dst[0] = 0;
				return;
			}
			memcpy(&(dst[output_index]), &(display_evt[input_index]), len);
			output_index += len;
			input_index += len;
			break;
		case 1:
		{
			char arg_idx = display_evt[input_index++];
			if (arg_idx > numargs)
			{
				PrintTextf(sess,
							"HexChat DEBUG: display_event: arg > numargs (%d %d %s)\n",
					arg_idx, numargs, display_evt.c_str());
				break;
			}
			const char* current_argument = args[(int)arg_idx + 1];
			if (current_argument == NULL)
			{
				PrintTextf(sess, "arg[%d] is NULL in print event\n", arg_idx + 1);
			}
			else
			{
				std::string mutable_argument(current_argument);
				if (mutable_argument.size() > dstsize - output_index - 4)
					mutable_argument = mutable_argument.substr(0, dstsize - output_index - 4);
					//current_argument[dstsize - output_index - 4] = 0;	/* Avoid buffer overflow */
				if (stripcolor_args & ARG_FLAG(arg_idx + 1)){ 
					auto result = strip_color2(mutable_argument, STRIP_ALL);
					std::copy(result.cbegin(), result.cend(), &dst[output_index]);
					len = result.size();
				}
				else len = strip_hidden_attribute(mutable_argument, &dst[output_index]);
				output_index += len;
			}
			}
			break;
		case 2:
			dst[output_index++] = '\n';
			dst[output_index++] = 0;
			done_all = true;
			continue;
		case 3:
				if (prefs.hex_text_indent)
					dst[output_index++] = '\t';
				else
					dst[output_index++] = ' ';
			break;
		}
	}
	dst[output_index] = 0;
	if (*dst == '\n')
		dst[0] = 0;
}

static void display_event (session *sess, int event, char **args, 
					unsigned int stripcolor_args, time_t timestamp)
{
	char buf[4096];
	format_event (sess, event, args, buf, sizeof (buf), stripcolor_args);
	if (buf[0])
		PrintTextTimeStamp (sess, buf, timestamp);
}

namespace
{
	struct pevt_stage1
	{
		int len;
		char *data;
		struct pevt_stage1 *next;
	};
} // end anonymous namespace

int pevt_build_string(const std::string& input, std::string & output, int &max_arg)
{
	std::vector<std::vector<char>> events;
	int clen;
	char o[4096], d;
	int output_index, max = -1, x;

	std::string buf = check_special_chars (input, true);

	auto len = buf.size();

	clen = output_index = 0;
	auto input_itr = buf.cbegin();
	auto end = buf.cend();
	for (;;)
	{
		if (input_itr == end)
			break;
		d = *input_itr++;
		if (d != '$')
		{
			o[output_index++] = d;
			continue;
		}
		if (*input_itr == '$')
		{
			o[output_index++] = '$';
			continue;
		}
		if (output_index > 0)
		{
			std::vector<char> evt(output_index + sizeof(int) + 1);
			clen += output_index + sizeof (int) + 1;
			evt[0] = 0;
			memcpy (&(evt[1]), &output_index, sizeof (int));
			memcpy (&(evt[1 + sizeof (int)]), o, output_index);
			output_index = 0;
			events.emplace_back(std::move(evt));
		}
		if (input_itr == end)
		{
			fe_message ("String ends with a $", FE_MSG_WARN);
			return 1;
		}
		d = *input_itr++;
		if (d == 'a')
		{								  /* Hex value */
			if (input_itr == end)
				goto a_len_error;
			d = *input_itr++;
			d -= '0';
			x = d * 100;
			if (input_itr == end)
				goto a_len_error;
			d = *input_itr++;
			d -= '0';
			x += d * 10;
			if (input_itr == end)
				goto a_len_error;
			d = *input_itr++;
			d -= '0';
			x += d;
			if (x > 255)
				goto a_range_error;
			o[output_index++] = x;
			continue;

		 a_len_error:
			fe_message ("String ends in $a", FE_MSG_WARN);
			return 1;
		 a_range_error:
			fe_message ("$a value is greater than 255", FE_MSG_WARN);
			return 1;
		}
		if (d == 't')
		{
			std::vector<char> evt { 3 };
			events.emplace_back(std::move(evt));
			clen += 1;
			continue;
		}
		if (d < '1' || d > '9')
		{
			snprintf (o, sizeof (o), "Error, invalid argument $%c\n", d);
			fe_message (o, FE_MSG_WARN);
			return 1;
		}
		d -= '0';
		if (max < d)
			max = d;
		std::vector<char> evt{ 1, static_cast<char>(d - 1)  };
		clen += 2;
		events.emplace_back(std::move(evt));
	}
	if (output_index > 0)
	{
		std::vector<char> evt(output_index + sizeof(int) + 1);
		clen += output_index + sizeof (int) + 1;
		evt[0] = 0;
		memcpy (&(evt[1]), &output_index, sizeof (int));
		memcpy (&(evt[1 + sizeof (int)]), o, output_index);
		output_index = 0;
		events.emplace_back(std::move(evt));
	}

	std::vector<char>evt{ 2 };
	events.emplace_back(std::move(evt));
	clen += 1;

	std::string obuf(clen, '\0');
	auto o_index = obuf.begin();
	for (const auto& evt : events)
	{
		std::copy(evt.cbegin(), evt.cend(), o_index);
		o_index += evt.size();
	}

	max_arg = max;

	output = std::move(obuf);
	return 0;
}


/* black n white(0/1) are bad colors for nicks, and we'll use color 2 for us */
/* also light/dark gray (14/15) */
/* 5,7,8 are all shades of yellow which happen to look damn near the same */

static const char rcolors[] = { 19, 20, 22, 24, 25, 26, 27, 28, 29 };

int text_color_of(const boost::string_ref &name)
{
	int sum = std::accumulate(name.cbegin(), name.cend(), 0);
	sum %= sizeof (rcolors) / sizeof (char);
	return rcolors[sum];
}


/* called by EMIT_SIGNAL macro */

void text_emit (int index, session *sess, char *a, char *b, char *c, char *d,
			  time_t timestamp)
{
	unsigned int stripcolor_args = (chanopt_is_set (prefs.hex_text_stripcolor_msg, sess->text_strip) ? 0xFFFFFFFF : 0);
	char tbuf[NICKLEN + 4];

	if (prefs.hex_text_color_nicks && (index == XP_TE_CHANACTION || index == XP_TE_CHANMSG))
	{
		snprintf (tbuf, sizeof (tbuf), "\003%d%s", text_color_of (a), a);
		a = tbuf;
		stripcolor_args &= ~ARG_FLAG(1);	/* don't strip color from this argument */
	}
	std::string empty("\000");
	std::string name(te[index].name);
	name.push_back(0);
	char *word[PDIWORDS] = { 0 };
	word[0] = &name[0];
	word[1] = (a ? a : &empty[0]);
	word[2] = (b ? b : &empty[0]);
	word[3] = (c ? c : &empty[0]);
	word[4] = (d ? d : &empty[0]);
	for (int i = 5; i < PDIWORDS; i++)
		word[i] = &empty[0];

	if (plugin_emit_print (sess, word, timestamp))
		return;

	/* If a plugin's callback executes "/close", 'sess' may be invalid */
	if (!is_session (sess))
		return;

	switch (index)
	{
	case XP_TE_JOIN:
	case XP_TE_PART:
	case XP_TE_PARTREASON:
	case XP_TE_QUIT:
		/* implement ConfMode / Hide Join and Part Messages */
		if (chanopt_is_set (prefs.hex_irc_conf_mode, sess->text_hidejoinpart))
			return;
		break;

	/* ===Private message=== */
	case XP_TE_PRIVMSG:
	case XP_TE_DPRIVMSG:
	case XP_TE_PRIVACTION:
	case XP_TE_DPRIVACTION:
		if (chanopt_is_set (prefs.hex_input_beep_priv, sess->alert_beep) && (!prefs.hex_away_omit_alerts || !sess->server->is_away))
			sound_beep (sess);
		if (chanopt_is_set (prefs.hex_input_flash_priv, sess->alert_taskbar) && (!prefs.hex_away_omit_alerts || !sess->server->is_away))
			fe_flash_window (sess);
		/* why is this one different? because of plugin-tray.c's hooks! ugly */
		if (sess->alert_tray == SET_ON)
			fe_tray_set_icon (FE_ICON_MESSAGE);
		break;

	/* ===Highlighted message=== */
	case XP_TE_HCHANACTION:
	case XP_TE_HCHANMSG:
		if (chanopt_is_set (prefs.hex_input_beep_hilight, sess->alert_beep) && (!prefs.hex_away_omit_alerts || !sess->server->is_away))
			sound_beep (sess);
		if (chanopt_is_set (prefs.hex_input_flash_hilight, sess->alert_taskbar) && (!prefs.hex_away_omit_alerts || !sess->server->is_away))
			fe_flash_window (sess);
		if (sess->alert_tray == SET_ON)
			fe_tray_set_icon (FE_ICON_MESSAGE);
		break;

	/* ===Channel message=== */
	case XP_TE_CHANACTION:
	case XP_TE_CHANMSG:
		if (chanopt_is_set (prefs.hex_input_beep_chans, sess->alert_beep) && (!prefs.hex_away_omit_alerts || !sess->server->is_away))
			sound_beep (sess);
		if (chanopt_is_set (prefs.hex_input_flash_chans, sess->alert_taskbar) && (!prefs.hex_away_omit_alerts || !sess->server->is_away))
			fe_flash_window (sess);
		if (sess->alert_tray == SET_ON)
			fe_tray_set_icon (FE_ICON_MESSAGE);
		break;

	/* ===Nick change message=== */
	case XP_TE_CHANGENICK:
		if (prefs.hex_irc_hide_nickchange)
			return;
		break;
	}

	sound_play_event (index);
	display_event (sess, index, word, stripcolor_args, timestamp);
}

const char * text_find_format_string (const char name[])
{
	int i = pevent_find (name, i);
	return i >= 0 ? pntevts_text[i].c_str() : nullptr;
}

int text_emit_by_name (char *name, session *sess, time_t timestamp,
				   char *a, char *b, char *c, char *d)
{
	int i = 0;

	i = pevent_find (name, i);
	if (i >= 0)
	{
		text_emit (i, sess, a, b, c, d, timestamp);
		return 1;
	}

	return 0;
}

void pevent_save(const char file_name[])
{
	int fd;
	if (!file_name)
		fd = hexchat_open_file ("pevents.conf", O_CREAT | O_TRUNC | O_WRONLY,
			0x180, io::fs::XOF_DOMODE);
	else
		fd = hexchat_open_file (file_name, O_CREAT | O_TRUNC | O_WRONLY, 0x180,
			io::fs::XOF_FULLPATH | io::fs::XOF_DOMODE);
	if (fd == -1)
	{
		/*
		   fe_message ("Error opening config file\n", FALSE); 
		   If we get here when X-Chat is closing the fe-message causes a nice & hard crash
		   so we have to use perror which doesn't rely on GTK
		 */

		perror ("Error opening config file\n");
		return;
	}

	for (int i = 0; i < NUM_XP; i++)
	{
		char buf[1024];
		write (fd, buf, snprintf (buf, sizeof (buf),
										  "event_name=%s\n", te[i].name));
		write (fd, buf, snprintf (buf, sizeof (buf),
										  "event_text=%s\n\n", pntevts_text[i].c_str()));
	}

	close (fd);
}

/* =========================== */
/* ========== SOUND ========== */
/* =========================== */
std::array<std::string, NUM_XP> sound_files;
//char *sound_files[NUM_XP];

void sound_beep (session *sess)
{
	if (!prefs.hex_gui_focus_omitalerts || !fe_gui_info (sess, 0) == 1)
	{
		if (!sound_files[XP_TE_BEEP].empty())
			/* user defined beep _file_ */
			sound_play_event (XP_TE_BEEP);
		else
			/* system beep */
			fe_beep (sess);
	}
}

void sound_play(const boost::string_ref & file, bool quiet)
{
	namespace bfs = boost::filesystem;

	/* the pevents GUI editor triggers this after removing a soundfile */
	if (file.empty())
	{
		return;
	}
	bfs::path wavfile;
#ifdef WIN32
	/* check for fullpath */
	if (file[0] == '\\' || (((file[0] >= 'A' && file[0] <= 'Z') || (file[0] >= 'a' && file[0] <= 'z')) && file[1] == ':'))
#else
	if (file[0] == '/')
#endif
	{
		wavfile = file.to_string();
	}
	else
	{
		wavfile = bfs::path(config::config_dir()) / HEXCHAT_SOUND_DIR / file.to_string();
	}

	if (g_access (wavfile.string().c_str(), R_OK) == 0)
	{
#ifdef WIN32
		PlaySoundW (wavfile.c_str(), nullptr, SND_NODEFAULT|SND_FILENAME|SND_ASYNC);
#else
#ifdef USE_LIBCANBERRA
		if (ca_con == NULL)
		{
			ca_context_create (&ca_con);
			ca_context_change_props (ca_con,
											CA_PROP_APPLICATION_ID, "hexchat",
											CA_PROP_APPLICATION_NAME, "HexChat",
											CA_PROP_APPLICATION_ICON_NAME, "hexchat", NULL);
		}

		if (ca_context_play (ca_con, 0, CA_PROP_MEDIA_FILENAME, wavfile.c_str(), NULL) != 0)
#endif
		{
			glib_string cmd (g_find_program_in_path ("play"));
	
			if (cmd)
			{
				glib_string buf(g_strdup_printf ("%s \"%s\"", cmd.get(), wavfile.c_str()));
				hexchat_exec (buf.get());
			}
		}
#endif
	}
	else
	{
		if (!quiet)
		{
			std::ostringstream buf;
			buf << boost::format(_("Cannot read sound file:\n%s")) % wavfile;
			fe_message (buf.str(), FE_MSG_ERROR);
		}
	}
}

void sound_play_event (int i)
{
	sound_play(sound_files[i], false);
}

// file is intended to be an R-Value
static void sound_load_event (const std::string & evt, std::string file)
{
	int i = 0;

	if (!file.empty() && pevent_find (evt.c_str(), i) != -1)
	{
		sound_files[i] = std::move(file);
	}
}

void sound_load ()
{
	namespace bfs = boost::filesystem;
	auto path = bfs::path(config::config_dir()) / "sound.conf";
	bfs::ifstream instream(path, std::ios::in | std::ios::binary);
	std::string evt;
	for(std::string line; std::getline(instream, line, '\n');)
	{
		if (boost::starts_with(line, "event="))
		{
			evt = line.substr(6);
		}
		else if (boost::starts_with(line, "sound="))
		{
			if (!evt.empty())
			{
				sound_load_event (evt, line.substr(6));
			}
		}
	}
}

void sound_save ()
{
	int fd = hexchat_open_file ("sound.conf", O_CREAT | O_TRUNC | O_WRONLY, 0x180,
		 io::fs::XOF_DOMODE);
	if (fd == -1)
		return;

	for (int i = 0; i < NUM_XP; i++)
	{
		if (!sound_files[i].empty())
		{
			char buf[512];
			write (fd, buf, snprintf (buf, sizeof (buf),
											  "event=%s\n", te[i].name));
			write (fd, buf, snprintf (buf, sizeof (buf),
											  "sound=%s\n\n", sound_files[i].c_str()));
		}
	}

	close (fd);
}
