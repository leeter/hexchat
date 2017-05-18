/* X-Chat
 * Copyright (C) 1998-2006 Peter Zelezny.
 * Copyright (C) 2006 Damjan Jovanovic
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
 *
 * Wayne Conrad, 3 Apr 1999: Color-coded DCC file transfer status windows
 * Bernhard Valenti <bernhard.valenti@gmx.net> 2000-11-21: Fixed DCC send behind nat
 *
 * 2001-03-08 Added support for getting "dcc_ip" config parameter.
 * Jim Seymour (jseymour@LinxNet.com)
 */

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif
/* we only use 32 bits, but without this define, you get only 31! */
#define _FILE_OFFSET_BITS 64
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <sys/stat.h>
#include <fcntl.h>
#include <boost/utility/string_ref.hpp>

#define WANTSOCKET
#define WANTARPA
#define WANTDNS
#include "inet.hpp"

#ifdef WIN32
#include <windows.h>
#include <io.h>
#include "w32dcc_security.hpp"
#else
#include <unistd.h>
using SOCKET = int;
#define INVALID_SOCKET -1
#endif

#include "dcc.hpp"
#include "hexchat.hpp"
#include "util.hpp"
#include "fe.hpp"
#include "outbound.hpp"
#include "inbound.hpp"
#include "network.hpp"
#include "plugin.hpp"
#include "server.hpp"
#include "text.hpp"
#include "url.hpp"
#include "hexchatc.hpp"
#include "filesystem.hpp"
#include "session.hpp"

#ifdef USE_DCC64
#define BIG_STR_TO_INT(x) std::strtoull(x,nullptr,10)
#ifdef WIN32
#define stat _stat64
#endif
#else
#define BIG_STR_TO_INT(x) strtoul(x,nullptr,10)
#endif
namespace fe = ::hexchat::fe::dcc;
namespace dcc = ::hexchat::dcc;
static constexpr std::array<const char *, 4>dcctypes = { { "SEND", "RECV", "CHAT", "CHAT" } };

namespace hexchat{
namespace dcc{
extern const struct dccstat_info dccstat[] = {
	{  N_("Waiting"), 1 /*black */ },
	{  N_("Active"), 12 /*cyan */ },
	{  N_("Failed"), 4 /*red */ },
	{  N_("Done"),  3 /*green */ },
	{  N_("Connect"), 1 /*black */ },
	{  N_("Aborted"),  4 /*red */ },
};
}
}


/*static*/ int dcc_sendcpssum, dcc_getcpssum;
namespace {
static int dcc_global_throttle;	/* 0x1 = sends, 0x2 = gets */
static ::dcc::DCC *new_dcc (void);
static void dcc_close (::dcc::DCC *dcc, ::hexchat::dcc_state dccstat, bool destroy);
static gboolean dcc_send_data (GIOChannel *, GIOCondition, ::dcc::DCC *);
static gboolean dcc_read (GIOChannel *, GIOCondition, ::dcc::DCC *);
static gboolean dcc_read_ack (GIOChannel *source, GIOCondition condition, ::dcc::DCC *dcc);
static int is_resumable(::dcc::DCC *dcc);

static int new_id()
{
	static int id = 0;
	if (id == 0)
	{
		/* start the first ID at a random number for pseudo security */
		/* 1 - 255 */
		id = RAND_INT(255) + 1;
		/* ignore overflows, since it can go to 2 billion */
	}
	return id++;
}

static double
timeval_diff (GTimeVal *greater,
				 GTimeVal *less)
{
	long usecdiff;
	double result;
	
	result = greater->tv_sec - less->tv_sec;
	usecdiff = (long) greater->tv_usec - less->tv_usec;
	result += (double) usecdiff / 1000000;
	
	return result;
}

static void
dcc_unthrottle (::dcc::DCC *dcc)
{
	/* don't unthrottle here, but delegate to funcs */
	if (dcc->type == ::dcc::DCC::dcc_type::TYPE_RECV)
		dcc_read(nullptr, static_cast<GIOCondition>(0), dcc);
	else
		dcc_send_data(nullptr, static_cast<GIOCondition>(0), dcc);
}

static void
dcc_calc_cps (::dcc::DCC *dcc)
{
	GTimeVal now;
	int oldcps;
	double timediff, startdiff;
	int glob_throttle_bit, wasthrottled;
	int *cpssum, glob_limit;
	::dcc::DCC_SIZE pos, posdiff;

	g_get_current_time (&now);

	/* the pos we use for sends is an average
		between pos and ack */
	if (dcc->type == ::dcc::DCC::dcc_type::TYPE_SEND)
	{
		/* carefull to avoid 32bit overflow */
		pos = dcc->pos - ((dcc->pos - dcc->ack) / 2);
		glob_throttle_bit = 0x1;
		cpssum = &dcc_sendcpssum;
		glob_limit = prefs.hex_dcc_global_max_send_cps;
	}
	else
	{
		pos = dcc->pos;
		glob_throttle_bit = 0x2;
		cpssum = &dcc_getcpssum;
		glob_limit = prefs.hex_dcc_global_max_get_cps;
	}

	if (!dcc->firstcpstv.tv_sec && !dcc->firstcpstv.tv_usec)
		dcc->firstcpstv = now;
	else
	{
		startdiff = timeval_diff (&now, &dcc->firstcpstv);
		if (startdiff < 1)
			startdiff = 1;
		else if (startdiff > CPS_AVG_WINDOW)
			startdiff = CPS_AVG_WINDOW;

		timediff = timeval_diff (&now, &dcc->lastcpstv);
		if (timediff > startdiff)
			timediff = startdiff = 1;

		posdiff = pos - dcc->lastcpspos;
		oldcps = dcc->cps;
		dcc->cps = static_cast<int>(((double) posdiff / timediff) * (timediff / startdiff) +
			(double) dcc->cps * (1.0 - (timediff / startdiff)));

		*cpssum += dcc->cps - oldcps;
	}

	dcc->lastcpspos = pos;
	dcc->lastcpstv = now;

	/* now check cps against set limits... */
	wasthrottled = dcc->throttled;

	/* check global limits first */
	dcc->throttled &= ~0x2;
	if (glob_limit > 0 && *cpssum >= glob_limit)
	{
		dcc_global_throttle |= glob_throttle_bit;
		if (dcc->maxcps >= 0)
			dcc->throttled |= 0x2;
	}
	else
		dcc_global_throttle &= ~glob_throttle_bit;

	/* now check per-connection limit */
	if (dcc->maxcps > 0 && dcc->cps > dcc->maxcps)
		dcc->throttled |= 0x1;
	else
		dcc->throttled &= ~0x1;

	/* take action */
	if (wasthrottled && !dcc->throttled)
		dcc_unthrottle (dcc);
}

static void
dcc_remove_from_sum (::dcc::DCC *dcc)
{
	if (dcc->dccstat != ::hexchat::dcc_state::active)
		return;
	if (dcc->type == ::dcc::DCC::dcc_type::TYPE_SEND)
		dcc_sendcpssum -= dcc->cps;
	else if (dcc->type == ::dcc::DCC::dcc_type::TYPE_RECV)
		dcc_getcpssum -= dcc->cps;
}

static int
dcc_lookup_proxy (char *host, struct sockaddr_in *addr)
{
	static std::string cache_host;
	static guint32 cache_addr;

	/* too lazy to thread this, so we cache results */
	if (!cache_host.empty())
	{
		if (cache_host == host)
		{
			memcpy (&addr->sin_addr, &cache_addr, 4);
			return true;
		}
		cache_host.erase();
	}

	auto h = gethostbyname (host);
	if (h != nullptr && h->h_length == 4 && h->h_addr_list[0] != nullptr)
	{
		memcpy (&addr->sin_addr, h->h_addr, 4);
		memcpy (&cache_addr, h->h_addr, 4);
		cache_host = host;
		/* cppcheck-suppress memleak */
		return true;
	}

	return false;
}

#define DCC_USE_PROXY() (prefs.hex_net_proxy_host[0] && prefs.hex_net_proxy_type>0 && prefs.hex_net_proxy_type<5 && prefs.hex_net_proxy_use!=1)

static SOCKET
dcc_connect_sok (::dcc::DCC *dcc)
{
	struct sockaddr_in addr = { 0 };

	auto sok = socket (AF_INET, SOCK_STREAM, 0);
	if (sok == INVALID_SOCKET)
		return -1;

	addr.sin_family = AF_INET;
	if (DCC_USE_PROXY ())
	{
		if (!dcc_lookup_proxy (prefs.hex_net_proxy_host, &addr))
		{
			closesocket (sok);
			return -1;
		}
		addr.sin_port = htons (static_cast<std::uint16_t>(prefs.hex_net_proxy_port));
	}
	else
	{
		addr.sin_port = htons (static_cast<std::uint16_t>(dcc->port));
		addr.sin_addr.s_addr = htonl (dcc->addr);
	}

	set_nonblocking (sok);
	connect (sok, (struct sockaddr *) &addr, sizeof (addr));

	return sok;
}

static void
dcc_close (::dcc::DCC *dcc, ::hexchat::dcc_state dccstat, bool destroy)
{
	if (dcc->wiotag)
	{
		fe_input_remove (dcc->wiotag);
		dcc->wiotag = 0;
	}

	if (dcc->iotag)
	{
		fe_input_remove (dcc->iotag);
		dcc->iotag = 0;
	}

	if (dcc->sok != -1)
	{
		closesocket (dcc->sok);
		dcc->sok = -1;
	}

	dcc_remove_from_sum (dcc);

	if (dcc->fp != -1)
	{
		close (dcc->fp);
		dcc->fp = -1;

		if(dccstat == ::hexchat::dcc_state::done)
		{
			/* if we just completed a dcc receive, move the */
			/* completed file to the completed directory */
			if(dcc->type == ::dcc::DCC::dcc_type::TYPE_RECV)
			{			
				/* mgl: change this to handle the case where dccwithnick is set */
				move_file (prefs.hex_dcc_dir, prefs.hex_dcc_completed_dir, 
									 file_part (dcc->destfile), prefs.hex_dcc_permissions);
#ifdef WIN32
				auto path = io::fs::make_path(prefs.hex_dcc_completed_dir);
				auto file = io::fs::make_path(dcc->destfile).filename();
				path /= file;
				path = boost::filesystem::canonical(path);
				w32::file::mark_file_as_downloaded(path.wstring());
#endif
			}

		}
	}

	dcc->dccstat = dccstat;
	if (dcc->dccchat)
	{
		delete dcc->dccchat;
		dcc->dccchat = nullptr;
	}

	if (destroy)
	{
		dcc_list = g_slist_remove (dcc_list, dcc);
		::fe::fe_dcc_remove (dcc);
		delete dcc->proxy;
		delete[] dcc->file;
		g_free (dcc->destfile);
		free (dcc->nick);
		delete dcc;
		return;
	}

	::fe::fe_dcc_update (dcc);
}

/* returns: 0 - ok
1 - the dcc is closed! */

static int
dcc_chat_line(::dcc::DCC *dcc, char *line)
{
	session *sess;
	char *word[PDIWORDS];
	char *po;
	int ret, i;
	gsize utf_len = 0;
	char portbuf[32];
	message_tags_data no_tags = message_tags_data();

	auto len = strlen(line);
	if (dcc->serv->using_cp1255)
		len++;	/* include the NUL terminator */

	glib_string utf;
	if (dcc->serv->using_irc) /* using "IRC" encoding (CP1252/UTF-8 hybrid) */
		utf = nullptr;
	else if (dcc->serv->encoding)     /* system */
		utf.reset(g_locale_to_utf8(line, len, nullptr, &utf_len, nullptr));
	else
		utf.reset(g_convert(line, len, "UTF-8", dcc->serv->encoding->c_str(), 0, &utf_len, 0));

	if (utf)
	{
		line = utf.get();
		len = utf_len;
	}

	if (dcc->serv->using_cp1255 && len > 0)
		len--;

	/* we really need valid UTF-8 now */
	auto conv = text_validate(boost::string_ref{ line, len });

	sess = find_dialog(*(dcc->serv), dcc->nick);
	if (!sess)
		sess = dcc->serv->front_session;

	snprintf(portbuf, sizeof(portbuf), "%d", dcc->port);
	std::string ip = net_ip(dcc->addr);
	word[0] = "DCC Chat Text";
	word[1] = ip.data();
	word[2] = portbuf;
	word[3] = dcc->nick;
	word[4] = conv.data();
	for (i = 5; i < PDIWORDS; i++)
		word[i] = "\000";

	ret = plugin_emit_print(sess, word, 0);

	/* did the plugin close it? */
	if (!g_slist_find(dcc_list, dcc))
	{
		return 1;
	}

	/* did the plugin eat the event? */
	if (ret)
	{
		return 0;
	}

	url_check_line(boost::string_ref(line, len));

	if (line[0] == 1 && !g_ascii_strncasecmp(line + 1, "ACTION", 6))
	{
		po = strchr(line + 8, '\001');
		if (po)
			po[0] = 0;
		inbound_action(sess, dcc->serv->m_nick, dcc->nick, "", line + 8, false,
			false, &no_tags);
	}
	else
	{
		inbound_privmsg(*(dcc->serv), dcc->nick, "", line, false, &no_tags);
	}
	return 0;
}

static gboolean
dcc_read_chat(GIOChannel * /*source*/, GIOCondition /*condition*/, ::dcc::DCC *dcc)
{
	int i, len, dead;
	char portbuf[32];
	char lbuf[2050];

	for (;;)
	{
		if (dcc->throttled)
		{
			fe_input_remove(dcc->iotag);
			dcc->iotag = 0;
			return false;
		}

		if (!dcc->iotag)
			dcc->iotag = fe_input_add(dcc->sok, FIA_READ | FIA_EX, (GIOFunc)dcc_read_chat, dcc);

		len = recv(dcc->sok, lbuf, sizeof(lbuf) - 2, 0);
		if (len < 1)
		{
			if (len < 0)
			{
				if (would_block())
					return true;
			}
			sprintf(portbuf, "%d", dcc->port);
			EMIT_SIGNAL(XP_TE_DCCCHATF, dcc->serv->front_session, gsl::ensure_z(dcc->nick),
				gsl::ensure_z(net_ip(dcc->addr)), gsl::ensure_z(portbuf),
				gsl::ensure_z(errorstring((len < 0) ? sock_error() : 0)), 0);
			dcc_close(dcc, ::hexchat::dcc_state::failed, false);
			return true;
		}
		i = 0;
		lbuf[len] = 0;
		while (i < len)
		{
			switch (lbuf[i])
			{
			case '\r':
				break;
			case '\n':
				dcc->dccchat->linebuf[dcc->dccchat->pos] = 0;
				dead = dcc_chat_line(dcc, dcc->dccchat->linebuf);

				if (dead || !dcc->dccchat) /* the dcc has been closed, don't use (DCC *)! */
					return true;

				dcc->pos += dcc->dccchat->pos;
				dcc->dccchat->pos = 0;
				::fe::fe_dcc_update(dcc);
				break;
			default:
				dcc->dccchat->linebuf[dcc->dccchat->pos] = lbuf[i];
				if (dcc->dccchat->pos < (sizeof(dcc->dccchat->linebuf) - 1))
					dcc->dccchat->pos++;
			}
			i++;
		}
	}
}

static void
dcc_calc_average_cps(::dcc::DCC *dcc)
{
	time_t sec;

	sec = time(0) - dcc->starttime;
	if (sec < 1)
		sec = 1;
	if (dcc->type == ::dcc::DCC::dcc_type::TYPE_SEND)
		dcc->cps = (dcc->ack - dcc->resumable) / sec;
	else
		dcc->cps = gsl::narrow_cast<int>((dcc->pos - dcc->resumable) / sec);
}

static void
dcc_send_ack(::dcc::DCC *dcc)
{
	/* send in 32-bit big endian */
	guint32 pos = htonl(dcc->pos & 0xffffffff);
	send(dcc->sok, (char *)&pos, 4, 0);
}

static gboolean
dcc_read(GIOChannel * /*source*/, GIOCondition /*condition*/, ::dcc::DCC *dcc)
{
	char *old;
	char buf[4096];
	int n;
	gboolean need_ack = false;

	if (dcc->fp == -1)
	{

		/* try to create the download dir (even if it exists, no harm) */
		g_mkdir(prefs.hex_dcc_dir, 0700);

		if (dcc->resumable)
		{
			dcc->fp = g_open(dcc->destfile, O_WRONLY | O_APPEND | OFLAGS, 0);
			dcc->pos = dcc->resumable;
			dcc->ack = dcc->resumable;
		}
		else
		{
			if (g_access(dcc->destfile, F_OK) == 0)
			{
				n = 0;
				do
				{
					n++;
					snprintf(buf, sizeof(buf), "%s.%d", dcc->destfile, n);
				} while (access(buf, F_OK) == 0);

				old = dcc->destfile;
				dcc->destfile = new_strdup(buf);

				EMIT_SIGNAL(XP_TE_DCCRENAME, dcc->serv->front_session,
					gsl::ensure_z(old), gsl::ensure_z(dcc->destfile), nullptr, nullptr, 0);
				g_free(old);
			}
			dcc->fp =
				g_open(dcc->destfile, OFLAGS | O_TRUNC | O_WRONLY | O_CREAT,
				prefs.hex_dcc_permissions);
		}
	}
	if (dcc->fp == -1)
	{
		/* the last executed function is open(), errno should be valid */
		EMIT_SIGNAL(XP_TE_DCCFILEERR, dcc->serv->front_session, gsl::ensure_z(dcc->destfile),
			gsl::ensure_z(errorstring(errno)), nullptr, nullptr, 0);
		dcc_close(dcc, ::hexchat::dcc_state::failed, false);
		return true;
	}
	for (;;)
	{
		if (dcc->throttled)
		{
			if (need_ack)
				dcc_send_ack(dcc);

			fe_input_remove(dcc->iotag);
			dcc->iotag = 0;
			return false;
		}

		if (!dcc->iotag)
			dcc->iotag = fe_input_add(dcc->sok, FIA_READ | FIA_EX, (GIOFunc)dcc_read, dcc);

		n = recv(dcc->sok, buf, sizeof(buf), 0);
		if (n < 1)
		{
			if (n < 0)
			{
				if (would_block())
				{
					if (need_ack)
						dcc_send_ack(dcc);
					return true;
				}
			}
			EMIT_SIGNAL(XP_TE_DCCRECVERR, dcc->serv->front_session, gsl::ensure_z(dcc->file),
				gsl::ensure_z(dcc->destfile), gsl::ensure_z(dcc->nick),
				gsl::ensure_z(errorstring((n < 0) ? sock_error() : 0)), 0);
			/* send ack here? but the socket is dead */
			/*if (need_ack)
			dcc_send_ack (dcc);*/
			dcc_close(dcc, ::hexchat::dcc_state::failed, false);
			return true;
		}

		if (write(dcc->fp, buf, n) == -1) /* could be out of hdd space */
		{
			EMIT_SIGNAL(XP_TE_DCCRECVERR, dcc->serv->front_session, gsl::ensure_z(dcc->file),
				gsl::ensure_z(dcc->destfile), gsl::ensure_z(dcc->nick), gsl::ensure_z(errorstring(errno)), 0);
			if (need_ack)
				dcc_send_ack(dcc);
			dcc_close(dcc, ::hexchat::dcc_state::failed, false);
			return true;
		}

		dcc->lasttime = time(0);
		dcc->pos += n;
		need_ack = true;	/* send ack when we're done recv()ing */

		if (dcc->pos >= dcc->size)
		{
			dcc_send_ack(dcc);
			dcc_close(dcc, ::hexchat::dcc_state::done, false);
			dcc_calc_average_cps(dcc);	/* this must be done _after_ dcc_close, or dcc_remove_from_sum will see the wrong value in dcc->cps */
			/* cppcheck-suppress deallocuse */
			const auto ibuf = std::to_string(dcc->cps);
			EMIT_SIGNAL(XP_TE_DCCRECVCOMP, dcc->serv->front_session,
				gsl::ensure_z(dcc->file), gsl::ensure_z(dcc->destfile), gsl::ensure_z(dcc->nick), ibuf, 0);
			return true;
		}
	}
}

static void
dcc_open_query(server &serv, const char *nick)
{
	if (prefs.hex_gui_autoopen_dialog)
		open_query(serv, nick, false);
}


static gboolean
dcc_did_connect(GIOChannel * /*source*/, GIOCondition condition, ::dcc::DCC *dcc)
{
	int er;

#ifdef WIN32
	if (condition & G_IO_ERR)
	{
		int len;

		/* find the last errno for this socket */
		len = sizeof(er);
		getsockopt(dcc->sok, SOL_SOCKET, SO_ERROR, (char *)&er, &len);
		EMIT_SIGNAL(XP_TE_DCCCONFAIL, dcc->serv->front_session,
			gsl::ensure_z(dcctypes[static_cast<std::size_t>(dcc->type)]), gsl::ensure_z(dcc->nick), gsl::ensure_z(errorstring(er)),
			nullptr, 0);
		dcc->dccstat = ::hexchat::dcc_state::failed;
		::fe::fe_dcc_update(dcc);
		return false;
	}

#else
	struct sockaddr_in addr = { 0 };
	addr.sin_port = htons(dcc->port);
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(dcc->addr);

	/* check if it's already connected; This always fails on winXP */
	if (connect(dcc->sok, (struct sockaddr *) &addr, sizeof(addr)) != 0)
	{
		er = sock_error();
		if (er != EISCONN)
		{
			EMIT_SIGNAL(XP_TE_DCCCONFAIL, dcc->serv->front_session,
				dcctypes[static_cast<std::size_t>(dcc->type)], dcc->nick, errorstring(er),
				nullptr, 0);
			dcc->dccstat = ::hexchat::dcc_state::failed;
			::fe::fe_dcc_update(dcc);
			return false;
		}
	}
#endif

	return true;
}

static gboolean
dcc_connect_finished(GIOChannel *source, GIOCondition condition, ::dcc::DCC *dcc)
{
	char host[128];

	if (dcc->iotag)
	{
		fe_input_remove(dcc->iotag);
		dcc->iotag = 0;
	}

	if (!dcc_did_connect(source, condition, dcc))
		return true;

	dcc->dccstat = ::hexchat::dcc_state::active;
	snprintf(host, sizeof host, "%s:%d", net_ip(dcc->addr), dcc->port);

	switch (dcc->type)
	{
	case ::dcc::DCC::dcc_type::TYPE_RECV:
		dcc->iotag = fe_input_add(gsl::narrow_cast<int>(dcc->sok), FIA_READ | FIA_EX, (GIOFunc)dcc_read, dcc);
		EMIT_SIGNAL(XP_TE_DCCCONRECV, dcc->serv->front_session,
			gsl::ensure_z(dcc->nick), gsl::ensure_z(host), gsl::ensure_z(dcc->file), nullptr, 0);
		break;
	case ::dcc::DCC::dcc_type::TYPE_SEND:
		/* passive send */
		dcc->fastsend = !!prefs.hex_dcc_fast_send;
		if (dcc->fastsend)
			dcc->wiotag = fe_input_add(gsl::narrow_cast<int>(dcc->sok), FIA_WRITE, (GIOFunc)dcc_send_data, dcc);
		dcc->iotag = fe_input_add(gsl::narrow_cast<int>(dcc->sok), FIA_READ | FIA_EX, (GIOFunc)dcc_read_ack, dcc);
		dcc_send_data(nullptr, static_cast<GIOCondition>(0), dcc);
		EMIT_SIGNAL(XP_TE_DCCCONSEND, dcc->serv->front_session,
			gsl::ensure_z(dcc->nick), gsl::ensure_z(host), gsl::ensure_z(dcc->file), nullptr, 0);
		break;
	case ::dcc::DCC::dcc_type::TYPE_CHATSEND:	/* pchat */
		dcc_open_query(*dcc->serv, dcc->nick);
	case ::dcc::DCC::dcc_type::TYPE_CHATRECV:	/* normal chat */
		dcc->iotag = fe_input_add(gsl::narrow_cast<int>(dcc->sok), FIA_READ | FIA_EX, (GIOFunc)dcc_read_chat, dcc);
		dcc->dccchat = new struct ::dcc::dcc_chat;
		if (!dcc->dccchat)
			return false;
		dcc->dccchat->pos = 0;
		EMIT_SIGNAL(XP_TE_DCCCONCHAT, dcc->serv->front_session,
			gsl::ensure_z(dcc->nick), gsl::ensure_z(host), nullptr, nullptr, 0);
		break;
	}
	dcc->starttime = time(0);
	dcc->lasttime = dcc->starttime;
	::fe::fe_dcc_update(dcc);

	return true;
}

static gboolean
read_proxy(::dcc::DCC *dcc)
{
	::dcc::proxy_state *proxy = dcc->proxy;
	while (proxy->bufferused < proxy->buffersize)
	{
		int ret = recv(dcc->sok, (char*)&proxy->buffer[proxy->bufferused],
			proxy->buffersize - proxy->bufferused, 0);
		if (ret > 0)
			proxy->bufferused += ret;
		else
		{
			if (would_block())
				return false;
			else
			{
				dcc->dccstat = ::hexchat::dcc_state::failed;
				::fe::fe_dcc_update(dcc);
				if (dcc->iotag)
				{
					fe_input_remove(dcc->iotag);
					dcc->iotag = 0;
				}
				return false;
			}
		}
	}
	return true;
}

static gboolean
write_proxy(::dcc::DCC *dcc)
{
	::dcc::proxy_state *proxy = dcc->proxy;
	while (proxy->bufferused < proxy->buffersize)
	{
		int ret = send(dcc->sok, (char*)&proxy->buffer[proxy->bufferused],
			proxy->buffersize - proxy->bufferused, 0);
		if (ret >= 0)
			proxy->bufferused += ret;
		else
		{
			if (would_block())
				return false;
			else
			{
				dcc->dccstat = ::hexchat::dcc_state::failed;
				::fe::fe_dcc_update(dcc);
				if (dcc->wiotag)
				{
					fe_input_remove(dcc->wiotag);
					dcc->wiotag = 0;
				}
				return false;
			}
		}
	}
	return true;
}

static gboolean
proxy_read_line(::dcc::DCC *dcc)
{
	::dcc::proxy_state *proxy = dcc->proxy;
	while (1)
	{
		proxy->buffersize = proxy->bufferused + 1;
		if (!read_proxy(dcc))
			return false;
		if (proxy->buffer[proxy->bufferused - 1] == '\n'
			|| proxy->bufferused == hexchat::dcc::MAX_PROXY_BUFFER)
		{
			proxy->buffer[proxy->bufferused - 1] = 0;
			return true;
		}
	}
}

static gboolean
dcc_wingate_proxy_traverse(GIOChannel *source, GIOCondition /*condition*/, ::dcc::DCC *dcc)
{
	::dcc::proxy_state *proxy = dcc->proxy;
	if (proxy->phase == 0)
	{
		proxy->buffersize = snprintf((char*)proxy->buffer, hexchat::dcc::MAX_PROXY_BUFFER,
			"%s %d\r\n", net_ip(dcc->addr),
			dcc->port);
		proxy->bufferused = 0;
		dcc->wiotag = fe_input_add(gsl::narrow_cast<int>(dcc->sok), FIA_WRITE | FIA_EX,
			(GIOFunc)dcc_wingate_proxy_traverse, dcc);
		++proxy->phase;
	}
	if (proxy->phase == 1)
	{
		if (!read_proxy(dcc))
			return true;
		fe_input_remove(dcc->wiotag);
		dcc->wiotag = 0;
		dcc_connect_finished(source, static_cast<GIOCondition>(0), dcc);
	}
	return true;
}

struct sock_connect
{
	char version;
	char type;
	guint16 port;
	guint32 address;
	char username[10];
};
static gboolean
dcc_socks_proxy_traverse(GIOChannel *source, GIOCondition /*condition*/, ::dcc::DCC *dcc)
{
	::dcc::proxy_state *proxy = dcc->proxy;

	if (proxy->phase == 0)
	{
		struct sock_connect sc = { 0 };
		sc.version = 4;
		sc.type = 1;
		sc.port = htons(gsl::narrow_cast<std::uint16_t>(dcc->port));
		sc.address = htonl(dcc->addr);

		safe_strcpy(sc.username, prefs.hex_irc_user_name);
		memcpy(proxy->buffer, &sc, sizeof(sc));
		proxy->buffersize = 8 + strlen(sc.username) + 1;
		proxy->bufferused = 0;
		dcc->wiotag = fe_input_add(gsl::narrow_cast<int>(dcc->sok), FIA_WRITE | FIA_EX,
			(GIOFunc)dcc_socks_proxy_traverse, dcc);
		++proxy->phase;
	}

	if (proxy->phase == 1)
	{
		if (!write_proxy(dcc))
			return true;
		fe_input_remove(dcc->wiotag);
		dcc->wiotag = 0;
		proxy->bufferused = 0;
		proxy->buffersize = 8;
		dcc->iotag = fe_input_add(gsl::narrow_cast<int>(dcc->sok), FIA_READ | FIA_EX,
			(GIOFunc)dcc_socks_proxy_traverse, dcc);
		++proxy->phase;
	}

	if (proxy->phase == 2)
	{
		if (!read_proxy(dcc))
			return true;
		fe_input_remove(dcc->iotag);
		dcc->iotag = 0;
		if (proxy->buffer[1] == 90)
			dcc_connect_finished(source, static_cast<GIOCondition>(0), dcc);
		else
		{
			dcc->dccstat = ::hexchat::dcc_state::failed;
			::fe::fe_dcc_update(dcc);
		}
	}

	return true;
}

struct sock5_connect1
{
	char version;
	char nmethods;
	char method;
};
static gboolean
dcc_socks5_proxy_traverse(GIOChannel *source, GIOCondition /*condition*/, ::dcc::DCC *dcc)
{
	::dcc::proxy_state *proxy = dcc->proxy;
	int auth = prefs.hex_net_proxy_auth && prefs.hex_net_proxy_user[0] && prefs.hex_net_proxy_pass[0];

	if (proxy->phase == 0)
	{
		struct sock5_connect1 sc1 = { 0 };
		sc1.version = 5;
		sc1.nmethods = 1;
		sc1.method = 0;

		if (auth)
			sc1.method = 2;
		memcpy(proxy->buffer, &sc1, 3);
		proxy->buffersize = 3;
		proxy->bufferused = 0;
		dcc->wiotag = fe_input_add(gsl::narrow_cast<int>(dcc->sok), FIA_WRITE | FIA_EX,
			(GIOFunc)dcc_socks5_proxy_traverse, dcc);
		++proxy->phase;
	}

	if (proxy->phase == 1)
	{
		if (!write_proxy(dcc))
			return true;
		fe_input_remove(dcc->wiotag);
		dcc->wiotag = 0;
		proxy->bufferused = 0;
		proxy->buffersize = 2;
		dcc->iotag = fe_input_add(gsl::narrow_cast<int>(dcc->sok), FIA_READ | FIA_EX,
			(GIOFunc)dcc_socks5_proxy_traverse, dcc);
		++proxy->phase;
	}

	if (proxy->phase == 2)
	{
		if (!read_proxy(dcc))
			return true;
		fe_input_remove(dcc->iotag);
		dcc->iotag = 0;

		/* did the server say no auth required? */
		if (proxy->buffer[0] == 5 && proxy->buffer[1] == 0)
			auth = 0;

		/* Set up authentication I/O */
		if (auth)
		{
			int len_u = 0, len_p = 0;

			/* authentication sub-negotiation (RFC1929) */
			if (proxy->buffer[0] != 5 || proxy->buffer[1] != 2)  /* UPA not supported by server */
			{
				PrintText(dcc->serv->front_session, "SOCKS\tServer doesn't support UPA authentication.\n");
				dcc->dccstat = ::hexchat::dcc_state::failed;
				::fe::fe_dcc_update(dcc);
				return true;
			}

			memset(proxy->buffer, 0, hexchat::dcc::MAX_PROXY_BUFFER);

			/* form the UPA request */
			len_u = strlen(prefs.hex_net_proxy_user);
			len_p = strlen(prefs.hex_net_proxy_pass);
			proxy->buffer[0] = 1;
			proxy->buffer[1] = gsl::narrow_cast<unsigned char>(len_u);
			memcpy(proxy->buffer + 2, prefs.hex_net_proxy_user, len_u);
			proxy->buffer[2 + len_u] = gsl::narrow_cast<unsigned char>(len_p);
			memcpy(proxy->buffer + 3 + len_u, prefs.hex_net_proxy_pass, len_p);

			proxy->buffersize = 3 + len_u + len_p;
			proxy->bufferused = 0;
			dcc->wiotag = fe_input_add(gsl::narrow_cast<int>(dcc->sok), FIA_WRITE | FIA_EX,
				(GIOFunc)dcc_socks5_proxy_traverse, dcc);
			++proxy->phase;
		}
		else
		{
			if (proxy->buffer[0] != 5 || proxy->buffer[1] != 0)
			{
				PrintText(dcc->serv->front_session, "SOCKS\tAuthentication required but disabled in settings.\n");
				dcc->dccstat = ::hexchat::dcc_state::failed;
				::fe::fe_dcc_update(dcc);
				return true;
			}
			proxy->phase += 2;
		}
	}

	if (proxy->phase == 3)
	{
		if (!write_proxy(dcc))
			return true;
		fe_input_remove(dcc->wiotag);
		dcc->wiotag = 0;
		proxy->buffersize = 2;
		proxy->bufferused = 0;
		dcc->iotag = fe_input_add(gsl::narrow_cast<int>(dcc->sok), FIA_READ | FIA_EX,
			(GIOFunc)dcc_socks5_proxy_traverse, dcc);
		++proxy->phase;
	}

	if (proxy->phase == 4)
	{
		if (!read_proxy(dcc))
			return true;
		if (dcc->iotag)
		{
			fe_input_remove(dcc->iotag);
			dcc->iotag = 0;
		}
		if (proxy->buffer[1] != 0)
		{
			PrintText(dcc->serv->front_session, "SOCKS\tAuthentication failed. "
				"Is username and password correct?\n");
			dcc->dccstat = ::hexchat::dcc_state::failed;
			::fe::fe_dcc_update(dcc);
			return true;
		}
		++proxy->phase;
	}

	if (proxy->phase == 5)
	{
		proxy->buffer[0] = 5;	/* version (socks 5) */
		proxy->buffer[1] = 1;	/* command (connect) */
		proxy->buffer[2] = 0;	/* reserved */
		proxy->buffer[3] = 1;	/* address type (IPv4) */
		proxy->buffer[4] = (dcc->addr >> 24) & 0xFF;	/* IP address */
		proxy->buffer[5] = (dcc->addr >> 16) & 0xFF;
		proxy->buffer[6] = (dcc->addr >> 8) & 0xFF;
		proxy->buffer[7] = (dcc->addr & 0xFF);
		proxy->buffer[8] = (dcc->port >> 8) & 0xFF;		/* port */
		proxy->buffer[9] = (dcc->port & 0xFF);
		proxy->buffersize = 10;
		proxy->bufferused = 0;
		dcc->wiotag = fe_input_add(gsl::narrow_cast<int>(dcc->sok), FIA_WRITE | FIA_EX,
			(GIOFunc)dcc_socks5_proxy_traverse, dcc);
		++proxy->phase;
	}

	if (proxy->phase == 6)
	{
		if (!write_proxy(dcc))
			return true;
		fe_input_remove(dcc->wiotag);
		dcc->wiotag = 0;
		proxy->buffersize = 4;
		proxy->bufferused = 0;
		dcc->iotag = fe_input_add(gsl::narrow_cast<int>(dcc->sok), FIA_READ | FIA_EX,
			(GIOFunc)dcc_socks5_proxy_traverse, dcc);
		++proxy->phase;
	}

	if (proxy->phase == 7)
	{
		if (!read_proxy(dcc))
			return true;
		if (proxy->buffer[0] != 5 || proxy->buffer[1] != 0)
		{
			fe_input_remove(dcc->iotag);
			dcc->iotag = 0;
			if (proxy->buffer[1] == 2)
				PrintText(dcc->serv->front_session, "SOCKS\tProxy refused to connect to host (not allowed).\n");
			else
				PrintTextf(dcc->serv->front_session, "SOCKS\tProxy failed to connect to host (error %d).\n", proxy->buffer[1]);
			dcc->dccstat = ::hexchat::dcc_state::failed;
			::fe::fe_dcc_update(dcc);
			return true;
		}
		switch (proxy->buffer[3])
		{
		case 1: proxy->buffersize += 6; break;
		case 3: proxy->buffersize += 1; break;
		case 4: proxy->buffersize += 18; break;
		};
		++proxy->phase;
	}

	if (proxy->phase == 8)
	{
		if (!read_proxy(dcc))
			return true;
		/* handle domain name case */
		if (proxy->buffer[3] == 3)
		{
			proxy->buffersize = 5 + proxy->buffer[4] + 2;
		}
		/* everything done? */
		if (proxy->bufferused == proxy->buffersize)
		{
			fe_input_remove(dcc->iotag);
			dcc->iotag = 0;
			dcc_connect_finished(source, static_cast<GIOCondition>(0), dcc);
		}
	}
	return true;
}

static gboolean
dcc_http_proxy_traverse(GIOChannel *source, GIOCondition /*condition*/, ::dcc::DCC *dcc)
{
	::dcc::proxy_state *proxy = dcc->proxy;

	if (proxy->phase == 0)
	{
		char buf[256];
		char auth_data[128];
		char auth_data2[68];
		int n, n2;

		n = snprintf(buf, sizeof(buf), "CONNECT %s:%d HTTP/1.0\r\n",
			net_ip(dcc->addr), dcc->port);
		if (prefs.hex_net_proxy_auth)
		{
			n2 = snprintf(auth_data2, sizeof(auth_data2), "%s:%s",
				prefs.hex_net_proxy_user, prefs.hex_net_proxy_pass);
			base64_encode(auth_data, auth_data2, n2);
			n += snprintf(buf + n, sizeof(buf) - n, "Proxy-Authorization: Basic %s\r\n", auth_data);
		}
		n += snprintf(buf + n, sizeof(buf) - n, "\r\n");
		proxy->buffersize = n;
		proxy->bufferused = 0;
		memcpy(proxy->buffer, buf, proxy->buffersize);
		dcc->wiotag = fe_input_add(gsl::narrow_cast<int>(dcc->sok), FIA_WRITE | FIA_EX,
			(GIOFunc)dcc_http_proxy_traverse, dcc);
		++proxy->phase;
	}

	if (proxy->phase == 1)
	{
		if (!write_proxy(dcc))
			return true;
		fe_input_remove(dcc->wiotag);
		dcc->wiotag = 0;
		proxy->bufferused = 0;
		dcc->iotag = fe_input_add(gsl::narrow_cast<int>(dcc->sok), FIA_READ | FIA_EX,
			(GIOFunc)dcc_http_proxy_traverse, dcc);
		++proxy->phase;
	}

	if (proxy->phase == 2)
	{
		if (!proxy_read_line(dcc))
			return true;
		/* "HTTP/1.0 200 OK" */
		if (proxy->bufferused < 12 ||
			memcmp(proxy->buffer, "HTTP/", 5) || memcmp(proxy->buffer + 9, "200", 3))
		{
			fe_input_remove(dcc->iotag);
			dcc->iotag = 0;
			PrintText(dcc->serv->front_session, (char*)proxy->buffer);
			dcc->dccstat = ::hexchat::dcc_state::failed;
			::fe::fe_dcc_update(dcc);
			return true;
		}
		proxy->bufferused = 0;
		++proxy->phase;
	}

	if (proxy->phase == 3)
	{
		for (;;)
		{
			/* read until blank line */
			if (proxy_read_line(dcc))
			{
				if (proxy->bufferused < 1 ||
					(proxy->bufferused == 2 && proxy->buffer[0] == '\r'))
				{
					break;
				}
				if (proxy->bufferused > 1)
					PrintText(dcc->serv->front_session, (char*)proxy->buffer);
				proxy->bufferused = 0;
			}
			else
				return true;
		}
		fe_input_remove(dcc->iotag);
		dcc->iotag = 0;
		dcc_connect_finished(source, static_cast<GIOCondition>(0), dcc);
	}

	return true;
}

static gboolean
dcc_proxy_connect(GIOChannel *source, GIOCondition condition, ::dcc::DCC *dcc)
{
	fe_input_remove(dcc->iotag);
	dcc->iotag = 0;

	if (!dcc_did_connect(source, condition, dcc))
		return true;

	dcc->proxy = new ::dcc::proxy_state();
	if (!dcc->proxy)
	{
		dcc->dccstat = ::hexchat::dcc_state::failed;
		::fe::fe_dcc_update(dcc);
		return true;
	}

	switch (prefs.hex_net_proxy_type)
	{
	case 1: return dcc_wingate_proxy_traverse(source, condition, dcc);
	case 2: return dcc_socks_proxy_traverse(source, condition, dcc);
	case 3: return dcc_socks5_proxy_traverse(source, condition, dcc);
	case 4: return dcc_http_proxy_traverse(source, condition, dcc);
	}
	return true;
}

static int dcc_listen_init(::dcc::DCC *, struct session *);

static void
dcc_connect(::dcc::DCC *dcc)
{
	int ret;
	char tbuf[400];

	if (dcc->dccstat == ::hexchat::dcc_state::connecting)
		return;
	dcc->dccstat = ::hexchat::dcc_state::connecting;

	if (dcc->pasvid && dcc->port == 0)
	{
		/* accepted a passive dcc send */
		ret = dcc_listen_init(dcc, dcc->serv->front_session);
		if (!ret)
		{
			dcc_close(dcc, ::hexchat::dcc_state::failed, false);
			return;
		}
		/* possible problems with filenames containing spaces? */
		if (dcc->type == ::dcc::DCC::dcc_type::TYPE_RECV)
			snprintf(tbuf, sizeof(tbuf), strchr(dcc->file, ' ') ?
			"DCC SEND \"%s\" %u %d %" DCC_SFMT " %d" :
			"DCC SEND %s %u %d %" DCC_SFMT " %d", dcc->file,
			dcc->addr, dcc->port, dcc->size, dcc->pasvid);
		else
			snprintf(tbuf, sizeof(tbuf), "DCC CHAT chat %u %d %d",
			dcc->addr, dcc->port, dcc->pasvid);
		dcc->serv->p_ctcp(dcc->nick, tbuf);
	}
	else
	{
		dcc->sok = dcc_connect_sok(dcc);
		if (dcc->sok == -1)
		{
			dcc->dccstat = ::hexchat::dcc_state::failed;
			::fe::fe_dcc_update(dcc);
			return;
		}
		if (DCC_USE_PROXY())
			dcc->iotag = fe_input_add(gsl::narrow_cast<int>(dcc->sok), FIA_WRITE | FIA_EX, (GIOFunc)dcc_proxy_connect, dcc);
		else
			dcc->iotag = fe_input_add(gsl::narrow_cast<int>(dcc->sok), FIA_WRITE | FIA_EX, (GIOFunc)dcc_connect_finished, dcc);
	}

	::fe::fe_dcc_update(dcc);
}

static gboolean
dcc_send_data(GIOChannel * /*source*/, GIOCondition /*condition*/, ::dcc::DCC *dcc)
{
	int len, sent;
	auto sok = dcc->sok;

	if (prefs.hex_dcc_blocksize < 1) /* this is too little! */
		prefs.hex_dcc_blocksize = 1024;

	if (prefs.hex_dcc_blocksize > 102400)	/* this is too much! */
		prefs.hex_dcc_blocksize = 102400;

	if (dcc->throttled)
	{
		fe_input_remove(dcc->wiotag);
		dcc->wiotag = 0;
		return false;
	}

	if (!dcc->fastsend)
	{
		if (dcc->ack < dcc->pos)
			return true;
	}
	else if (!dcc->wiotag)
		dcc->wiotag = fe_input_add(gsl::narrow_cast<int>(sok), FIA_WRITE, (GIOFunc)dcc_send_data, dcc);

	std::vector<char> buf(prefs.hex_dcc_blocksize);
	lseek(dcc->fp, dcc->pos, SEEK_SET);
	len = read(dcc->fp, &buf[0], prefs.hex_dcc_blocksize);
	if (len < 1)
		goto abortit;
	sent = send(sok, &buf[0], len, 0);

	if (sent < 0 && !(would_block()))
	{
	abortit:
		EMIT_SIGNAL(XP_TE_DCCSENDFAIL, dcc->serv->front_session,
			gsl::ensure_z(file_part(dcc->file)), gsl::ensure_z(dcc->nick),
			gsl::ensure_z(errorstring(sock_error())), nullptr, 0);
		dcc_close(dcc, ::hexchat::dcc_state::failed, false);
		return true;
	}
	if (sent > 0)
	{
		dcc->pos += sent;
		dcc->lasttime = time(0);
	}

	/* have we sent it all yet? */
	if (dcc->pos >= dcc->size)
	{
		/* it's all sent now, so remove the WRITE/SEND handler */
		if (dcc->wiotag)
		{
			fe_input_remove(dcc->wiotag);
			dcc->wiotag = 0;
		}
	}
	return true;
}

static gboolean
dcc_handle_new_ack(::dcc::DCC *dcc)
{
	guint32 ack;
	gboolean done = false;

	memcpy(&ack, dcc->ack_buf, 4);
	dcc->ack = ntohl(ack);

	/* this could mess up when xfering >32bit files */
	if (dcc->size <= 0xffffffff)
	{
		/* fix for BitchX */
		if (dcc->ack < dcc->resumable)
			dcc->ackoffset = true;
		if (dcc->ackoffset)
			dcc->ack += dcc->resumable;
	}

	/* DCC complete check */
	if (dcc->pos >= dcc->size && dcc->ack >= (dcc->size & 0xffffffff))
	{
		dcc->ack = dcc->size;	/* force 100% ack for >4 GB */
		dcc_close(dcc, ::hexchat::dcc_state::done, false);
		dcc_calc_average_cps(dcc);	/* this must be done _after_ dcc_close, or dcc_remove_from_sum will see the wrong value in dcc->cps */
		const auto buf = std::to_string(dcc->cps);
		EMIT_SIGNAL(XP_TE_DCCSENDCOMP, dcc->serv->front_session,
			gsl::ensure_z(file_part(dcc->file)), gsl::ensure_z(dcc->nick), buf, nullptr, 0);
		done = true;
	}
	else if ((!dcc->fastsend) && (dcc->ack >= (dcc->pos & 0xffffffff)))
	{
		dcc_send_data(nullptr, static_cast<GIOCondition>(0), dcc);
	}

#ifdef USE_DCC64
	/* take the top 32 of "bytes send" and bottom 32 of "ack" */
	dcc->ack = (dcc->pos & G_GINT64_CONSTANT(0xffffffff00000000)) |
		(dcc->ack & 0xffffffff);
	/* dcc->ack is only used for CPS and PERCENTAGE calcs from now on... */
#endif

	return done;
}

static gboolean
dcc_read_ack(GIOChannel * /*source*/, GIOCondition /*condition*/, ::dcc::DCC *dcc)
{
	int len;

	for (;;)
	{
		/* try to fill up 4 bytes */
		len = recv(dcc->sok, (char*)dcc->ack_buf, 4 - dcc->ack_pos, 0);
		if (len < 1)
		{
			if (len < 0)
			{
				if (would_block())	/* ok - keep waiting */
					return true;
			}
			EMIT_SIGNAL(XP_TE_DCCSENDFAIL, dcc->serv->front_session,
				gsl::ensure_z(file_part(dcc->file)), gsl::ensure_z(dcc->nick),
				gsl::ensure_z(errorstring((len < 0) ? sock_error() : 0)), nullptr, 0);
			dcc_close(dcc, ::hexchat::dcc_state::failed, false);
			return true;
		}

		dcc->ack_pos += len;
		if (dcc->ack_pos >= 4)
		{
			dcc->ack_pos = 0;
			if (dcc_handle_new_ack(dcc))
				return true;
		}
		/* loop again until would_block() returns true */
	}
}

static gboolean
dcc_accept(GIOChannel * /*source*/, GIOCondition /*condition*/, ::dcc::DCC *dcc)
{
	char host[128];
	struct sockaddr_in CAddr;
	socklen_t len;

	len = sizeof(CAddr);
	auto sok = accept(dcc->sok, (struct sockaddr *) &CAddr, &len);
	fe_input_remove(dcc->iotag);
	dcc->iotag = 0;
	closesocket(dcc->sok);
	if (sok != INVALID_SOCKET)
	{
		dcc->sok = INVALID_SOCKET;
		dcc_close(dcc, ::hexchat::dcc_state::failed, false);
		return true;
	}
	set_nonblocking(sok);
	dcc->sok = sok;
	dcc->addr = ntohl(CAddr.sin_addr.s_addr);

	if (dcc->pasvid)
		return dcc_connect_finished(nullptr, static_cast<GIOCondition>(0), dcc);

	dcc->dccstat = ::hexchat::dcc_state::active;
	dcc->lasttime = dcc->starttime = time(0);
	dcc->fastsend = !!prefs.hex_dcc_fast_send;

	snprintf(host, sizeof(host), "%s:%d", net_ip(dcc->addr), dcc->port);

	switch (dcc->type)
	{
	case ::dcc::DCC::dcc_type::TYPE_SEND:
		if (dcc->fastsend)
			dcc->wiotag = fe_input_add(gsl::narrow_cast<int>(sok), FIA_WRITE, (GIOFunc)dcc_send_data, dcc);
		dcc->iotag = fe_input_add(gsl::narrow_cast<int>(sok), FIA_READ | FIA_EX, (GIOFunc)dcc_read_ack, dcc);
		dcc_send_data(nullptr, GIOCondition(), dcc);
		EMIT_SIGNAL(XP_TE_DCCCONSEND, dcc->serv->front_session,
			gsl::ensure_z(dcc->nick), gsl::ensure_z(host), gsl::ensure_z(dcc->file), nullptr, 0);
		break;

	case ::dcc::DCC::dcc_type::TYPE_CHATSEND:
		dcc_open_query(*dcc->serv, dcc->nick);
		dcc->iotag = fe_input_add(gsl::narrow_cast<int>(dcc->sok), FIA_READ | FIA_EX, (GIOFunc)dcc_read_chat, dcc);
		dcc->dccchat = new struct ::dcc::dcc_chat;
		dcc->dccchat->pos = 0;
		EMIT_SIGNAL(XP_TE_DCCCONCHAT, dcc->serv->front_session,
			gsl::ensure_z(dcc->nick), gsl::ensure_z(host), nullptr, nullptr, 0);
		break;
	}

	::fe::fe_dcc_update(dcc);

	return true;
}

static int
dcc_listen_init(::dcc::DCC *dcc, session *sess)
{
	guint32 my_addr;
	struct sockaddr_in SAddr = { 0 };
	int i, bindretval = -1;
	socklen_t len;

	dcc->sok = socket(AF_INET, SOCK_STREAM, 0);
	if (dcc->sok == -1)
		return false;

	len = sizeof(SAddr);
	getsockname(dcc->serv->sok, (struct sockaddr *) &SAddr, &len);

	SAddr.sin_family = AF_INET;

	/*if local_ip is specified use that*/
	if (prefs.local_ip != 0xffffffff)
	{
		my_addr = prefs.local_ip;
		SAddr.sin_addr.s_addr = prefs.local_ip;
	}
	/*otherwise use the default*/
	else
		my_addr = SAddr.sin_addr.s_addr;

	/*if we have a valid portrange try to use that*/
	if (prefs.hex_dcc_port_first > 0)
	{
		SAddr.sin_port = 0;
		i = 0;
		while ((prefs.hex_dcc_port_last > ntohs(SAddr.sin_port)) &&
			(bindretval == -1))
		{
			SAddr.sin_port = htons(gsl::narrow_cast<std::uint16_t>(prefs.hex_dcc_port_first + i));
			i++;
			/*printf("Trying to bind against port: %d\n",ntohs(SAddr.sin_port));*/
			bindretval = bind(dcc->sok, (struct sockaddr *) &SAddr, sizeof(SAddr));
		}

		/* with a small port range, reUseAddr is needed */
		len = 1;
		setsockopt(dcc->sok, SOL_SOCKET, SO_REUSEADDR, (char *)&len, sizeof(len));

	}
	else
	{
		/* try random port */
		SAddr.sin_port = 0;
		bindretval = bind(dcc->sok, (struct sockaddr *) &SAddr, sizeof(SAddr));
	}

	if (bindretval == -1)
	{
		/* failed to bind */
		PrintText(sess, "Failed to bind to any address or port.\n");
		return false;
	}

	len = sizeof(SAddr);
	getsockname(dcc->sok, (struct sockaddr *) &SAddr, &len);

	dcc->port = ntohs(SAddr.sin_port);

	/*if we have a dcc_ip, we use that, so the remote client can connect*/
	/*else we try to take an address from hex_dcc_ip*/
	/*if something goes wrong we tell the client to connect to our LAN ip*/
	dcc->addr = ::dcc::dcc_get_my_address();

	/*if nothing else worked we use the address we bound to*/
	if (dcc->addr == 0)
		dcc->addr = my_addr;

	dcc->addr = ntohl(dcc->addr);

	set_nonblocking(dcc->sok);
	listen(dcc->sok, 1);
	set_blocking(dcc->sok);

	dcc->iotag = fe_input_add(gsl::narrow_cast<std::uint16_t>(dcc->sok), FIA_READ | FIA_EX, (GIOFunc)dcc_accept, dcc);

	return true;
}

static struct session *dccsess;
static const char *dccto;				  /* lame!! */
static int dccmaxcps;
static int recursive = false;

static void
dcc_send_wild(char *file)
{
	::dcc::dcc_send(dccsess, dccto, file, dccmaxcps, 0);
}

static ::dcc::DCC *
new_dcc(void)
{
	::dcc::DCC *dcc = new ::dcc::DCC();
	dcc->sok = INVALID_SOCKET;
	dcc->fp = -1;
	dcc_list = g_slist_prepend(dcc_list, dcc);
	return (dcc);
}

static void
dcc_confirm_send(void *ud)
{
	::dcc::DCC *dcc = (::dcc::DCC *) ud;
	dcc_get(dcc);
}

static void
dcc_deny_send(void *ud)
{
	::dcc::DCC *dcc = (::dcc::DCC *) ud;
	dcc_abort(dcc->serv->front_session, dcc);
}

static void
dcc_confirm_chat(void *ud)
{
	::dcc::DCC *dcc = (::dcc::DCC *) ud;
	dcc_connect(dcc);
}

static void
dcc_deny_chat(void *ud)
{
	::dcc::DCC *dcc = (::dcc::DCC *) ud;
	dcc_abort(dcc->serv->front_session, dcc);
}

static ::dcc::DCC *
dcc_add_chat(session *sess, char *nick, int port, guint32 addr, int pasvid)
{
	::dcc::DCC *dcc;

	dcc = new_dcc();
	if (dcc)
	{
		dcc->serv = sess->server;
		dcc->type = ::dcc::DCC::dcc_type::TYPE_CHATRECV;
		dcc->dccstat = ::hexchat::dcc_state::queued;
		dcc->addr = addr;
		dcc->port = port;
		dcc->pasvid = pasvid;
		dcc->nick = new_strdup(nick);
		dcc->starttime = time(0);

		EMIT_SIGNAL(XP_TE_DCCCHATOFFER, sess->server->front_session, gsl::ensure_z(nick),
			nullptr, nullptr, nullptr, 0);

		if (prefs.hex_gui_autoopen_chat)
		{
			if (fe_dcc_open_chat_win(true))	/* already open? add only */
				::fe::fe_dcc_add(dcc);
		}
		else
			::fe::fe_dcc_add(dcc);

		if (prefs.hex_dcc_auto_chat)
		{
			dcc_connect(dcc);
		}
		else
		{
			char buff[128];
			snprintf(buff, sizeof(buff), "%s is offering DCC Chat. Do you want to accept?", nick);
			fe_confirm(buff, dcc_confirm_chat, dcc_deny_chat, dcc);
		}
	}

	return dcc;
}

static ::dcc::DCC *
dcc_add_file(session *sess, char *file, ::dcc::DCC_SIZE size, int port, char *nick, guint32 addr, int pasvid)
{
	::dcc::DCC *dcc;
	char tbuf[512];

	dcc = new_dcc();
	if (dcc)
	{
		dcc->file = new_strdup(file);

		dcc->destfile = static_cast<char*>(g_malloc(strlen(prefs.hex_dcc_dir) + strlen(nick) +
			strlen(file) + 4));

		strcpy(dcc->destfile, prefs.hex_dcc_dir);
		if (prefs.hex_dcc_dir[strlen(prefs.hex_dcc_dir) - 1] != G_DIR_SEPARATOR)
			strcat(dcc->destfile, G_DIR_SEPARATOR_S);
		if (prefs.hex_dcc_save_nick)
		{
#ifdef WIN32
			char *t = strlen(dcc->destfile) + dcc->destfile;
			strcpy(t, nick);
			while (*t)
			{
				if (*t == '\\' || *t == '|')
					*t = '_';
				t++;
			}
#else
			strcat(dcc->destfile, nick);
#endif
			strcat(dcc->destfile, ".");
		}
		strcat(dcc->destfile, file);

		dcc->resumable = 0;
		dcc->pos = 0;
		dcc->serv = sess->server;
		dcc->type = ::dcc::DCC::dcc_type::TYPE_RECV;
		dcc->dccstat = ::hexchat::dcc_state::queued;
		dcc->addr = addr;
		dcc->port = port;
		dcc->pasvid = pasvid;
		dcc->size = size;
		dcc->nick = new_strdup(nick);
		dcc->maxcps = prefs.hex_dcc_max_get_cps;

		is_resumable(dcc);

		if (prefs.hex_dcc_auto_recv == 1)
		{
			snprintf(tbuf, sizeof(tbuf), _("%s is offering \"%s\". Do you want to accept?"), nick, file);
			fe_confirm(tbuf, dcc_confirm_send, dcc_deny_send, dcc);
		}
		else if (prefs.hex_dcc_auto_recv == 2)
		{
			dcc_get(dcc);
		}
		if (prefs.hex_gui_autoopen_recv)
		{
			if (fe_dcc_open_recv_win(true))	/* was already open? just add*/
				::fe::fe_dcc_add(dcc);
		}
		else
			::fe::fe_dcc_add(dcc);
	}
	sprintf(tbuf, "%" DCC_SFMT, size);
	snprintf(tbuf + 24, 300, "%s:%d", net_ip(addr), port);
	EMIT_SIGNAL(XP_TE_DCCSENDOFFER, sess->server->front_session, gsl::ensure_z(nick),
		gsl::ensure_z(file), gsl::ensure_z(tbuf), gsl::ensure_z(tbuf + 24), 0);

	return dcc;
}

static void
dcc_malformed(struct session *sess, char *nick, char *data)
{
	EMIT_SIGNAL(XP_TE_MALFORMED, sess, gsl::ensure_z(nick), gsl::ensure_z(data), nullptr, nullptr, 0);
}

static ::dcc::DCC *
find_dcc_from_id(int id, ::dcc::DCC::dcc_type type)
{
	::dcc::DCC *dcc;
	GSList *list = dcc_list;
	while (list)
	{
		dcc = (::dcc::DCC *) list->data;
		if (dcc->pasvid == id &&
			dcc->dccstat == ::hexchat::dcc_state::queued && dcc->type == type)
			return dcc;
		list = list->next;
	}
	return 0;
}

static ::dcc::DCC *
find_dcc_from_port(int port, ::dcc::DCC::dcc_type type)
{
	::dcc::DCC *dcc;
	GSList *list = dcc_list;
	while (list)
	{
		dcc = (::dcc::DCC *) list->data;
		if (dcc->port == port &&
			dcc->dccstat == ::hexchat::dcc_state::queued && dcc->type == type)
			return dcc;
		list = list->next;
	}
	return 0;
}

/* is the destination file the same? new_dcc is not opened yet */

static int
is_same_file(::dcc::DCC *dcc, ::dcc::DCC *new_dcc)
{
#ifndef WIN32
	GStatBuf st_a, st_b;
#endif

	/* if it's the same filename, must be same */
	if (strcmp(dcc->destfile, new_dcc->destfile) == 0)
		return true;

	/* now handle case-insensitive Filesystems: HFS+, FAT */
#ifdef WIN32
	/* warning no win32 implementation - behaviour may be unreliable */
#else
	/* this fstat() shouldn't really fail */
	if ((dcc->fp == -1 ? g_stat(dcc->destfile, &st_a) : fstat(dcc->fp, &st_a)) == -1)
		return false;
	if (g_stat(new_dcc->destfile, &st_b) == -1)
		return false;

	/* same inode, same device, same file! */
	if (st_a.st_ino == st_b.st_ino &&
		st_a.st_dev == st_b.st_dev)
		return true;
#endif

	return false;
}

static int
is_resumable(::dcc::DCC *dcc)
{
	dcc->resumable = 0;

	/* Check the file size */
	if (g_access(dcc->destfile, W_OK) == 0)
	{
		GStatBuf st;

		if (g_stat(dcc->destfile, &st) != -1)
		{
			if (st.st_size < dcc->size)
			{
				dcc->resumable = st.st_size;
				dcc->pos = st.st_size;
			}
			else
				dcc->resume_error = 2;
		}
		else
		{
			dcc->resume_errno = errno;
			dcc->resume_error = 1;
		}
	}
	else
	{
		dcc->resume_errno = errno;
		dcc->resume_error = 1;
	}

	/* Now verify that this DCC is not already in progress from someone else */

	if (dcc->resumable)
	{
		GSList *list = dcc_list;
		::dcc::DCC *d;
		while (list)
		{
			d = static_cast< ::dcc::DCC*>(list->data);
			if (d->type == ::dcc::DCC::dcc_type::TYPE_RECV && d->dccstat != ::hexchat::dcc_state::aborted &&
				d->dccstat != ::hexchat::dcc_state::done && d->dccstat != ::hexchat::dcc_state::failed)
			{
				if (d != dcc && is_same_file(d, dcc))
				{
					dcc->resume_error = 3;	/* dccgui.c uses it */
					dcc->resumable = 0;
					dcc->pos = 0;
					break;
				}
			}
			list = list->next;
		}
	}

	return gsl::narrow_cast<int>(dcc->resumable);
}

}

namespace hexchat{
namespace dcc{

bool
is_dcc(::dcc::DCC *dcc)
{
	GSList *list = dcc_list;
	while (list)
	{
		if (list->data == dcc)
			return true;
		list = list->next;
	}
	return false;
}

bool
is_dcc_completed(::dcc::DCC *dcc)
{
	if (dcc != nullptr)
		return (dcc->dccstat == ::hexchat::dcc_state::failed || dcc->dccstat == ::hexchat::dcc_state::done || dcc->dccstat == ::hexchat::dcc_state::aborted);

	return false;
}

/* this is called from hexchat.c:hexchat_misc_checks() every 1 second. */

void
dcc_check_timeouts(void)
{
	::dcc::DCC *dcc;
	time_t tim = time(0);
	GSList *next, *list = dcc_list;

	while (list)
	{
		dcc = static_cast< ::dcc::DCC *>(list->data);
		next = list->next;

		switch (dcc->dccstat)
		{
		case ::hexchat::dcc_state::active:
			dcc_calc_cps(dcc);
			::fe::fe_dcc_update(dcc);

			if (dcc->type == ::dcc::DCC::dcc_type::TYPE_SEND || dcc->type == ::dcc::DCC::dcc_type::TYPE_RECV)
			{
				if (prefs.hex_dcc_stall_timeout > 0)
				{
					if (!dcc->throttled
						&& tim - dcc->lasttime > prefs.hex_dcc_stall_timeout)
					{
						EMIT_SIGNAL(XP_TE_DCCSTALL, dcc->serv->front_session,
							gsl::ensure_z(dcctypes[static_cast<std::size_t>(dcc->type)]),
							gsl::ensure_z(file_part(dcc->file)), gsl::ensure_z(dcc->nick), nullptr, 0);
						dcc_close(dcc, ::hexchat::dcc_state::aborted, false);
					}
				}
			}
			break;
		case ::hexchat::dcc_state::queued:
			if (dcc->type == ::dcc::DCC::dcc_type::TYPE_SEND || dcc->type == ::dcc::DCC::dcc_type::TYPE_CHATSEND)
			{
				if (tim - dcc->offertime > prefs.hex_dcc_timeout)
				{
					if (prefs.hex_dcc_timeout > 0)
					{
						EMIT_SIGNAL(XP_TE_DCCTOUT, dcc->serv->front_session,
							gsl::ensure_z(dcctypes[static_cast<std::size_t>(dcc->type)]),
							gsl::ensure_z(file_part(dcc->file)), gsl::ensure_z(dcc->nick), nullptr, 0);
						dcc_close(dcc, ::hexchat::dcc_state::aborted, false);
					}
				}
			}
			break;
		case ::hexchat::dcc_state::done:
		case ::hexchat::dcc_state::failed:
		case ::hexchat::dcc_state::aborted:
			if (prefs.hex_dcc_remove)
				dcc_close(dcc, ::hexchat::dcc_state::queued, true);
			break;
		}
		list = next;
	}
}

void
dcc_abort (session *sess, ::dcc::DCC *dcc)
{
	if (dcc)
	{
		switch (dcc->dccstat)
		{
		case ::hexchat::dcc_state::queued:
		case ::hexchat::dcc_state::connecting:
		case ::hexchat::dcc_state::active:
			dcc_close (dcc, ::hexchat::dcc_state::aborted, false);
			switch (dcc->type)
			{
			case ::dcc::DCC::dcc_type::TYPE_CHATSEND:
			case ::dcc::DCC::dcc_type::TYPE_CHATRECV:
				EMIT_SIGNAL (XP_TE_DCCCHATABORT, sess, gsl::ensure_z(dcc->nick), nullptr, nullptr,
								 nullptr, 0);
				break;
			case ::dcc::DCC::dcc_type::TYPE_SEND:
				EMIT_SIGNAL (XP_TE_DCCSENDABORT, sess, gsl::ensure_z(dcc->nick),
					gsl::ensure_z(file_part (dcc->file)), nullptr, nullptr, 0);
				break;
			case ::dcc::DCC::dcc_type::TYPE_RECV:
				EMIT_SIGNAL (XP_TE_DCCRECVABORT, sess, gsl::ensure_z(dcc->nick),
					gsl::ensure_z(dcc->file), nullptr, nullptr, 0);
			}
			break;
		default:
			dcc_close (dcc, ::hexchat::dcc_state::queued, true);
		}
	}
}

void
dcc_notify_kill (struct server *serv)
{
	struct server *replaceserv = 0;
	::dcc::DCC *dcc;
	GSList *list = dcc_list;
	if (serv_list)
		replaceserv = (struct server *) serv_list->data;
	while (list)
	{
		dcc = static_cast< ::dcc::DCC *>(list->data);
		if (dcc->serv == serv)
			dcc->serv = replaceserv;
		list = list->next;
	}
}

DCC *
dcc_write_chat (const char *nick, const char *text)
{
	auto dcc = ::dcc::find_dcc(nick, "", ::dcc::DCC::dcc_type::TYPE_CHATRECV);
	if (!dcc)
		dcc = ::dcc::find_dcc(nick, "", ::dcc::DCC::dcc_type::TYPE_CHATSEND);
	if (dcc && dcc->dccstat == ::hexchat::dcc_state::active)
	{
		const auto len = strlen (text);
		// TODO: Fix this to use ASIO
		/*tcp_send_real (nullptr, dcc->sok, dcc->serv->encoding->c_str(), dcc->serv->using_irc,
							text, len, nullptr);*/
		send (dcc->sok, "\n", 1, 0);
		dcc->size += len;
		::fe::fe_dcc_update (dcc);
		return dcc;
	}
	return 0;
}

guint32 
dcc_get_my_address (void)	/* the address we'll tell the other person */
{
	struct hostent *dns_query;
	guint32 addr = 0;

	if (prefs.hex_dcc_ip_from_server && prefs.dcc_ip)
		addr = prefs.dcc_ip;
	else if (prefs.hex_dcc_ip[0])
	{
	   dns_query = gethostbyname ((const char *) prefs.hex_dcc_ip);

	   if (dns_query != nullptr &&
		   dns_query->h_length == 4 &&
		   dns_query->h_addr_list[0] != nullptr)
		{
			/*we're offered at least one IPv4 address: we take the first*/
			addr = *((guint32*) dns_query->h_addr_list[0]);
		}
	}

	return addr;
}



void
dcc_send (struct session *sess, const char *to, char *file, int maxcps, int passive)
{
	char outbuf[512];
	GStatBuf st;

	file = expand_homedir (file);

	if (!recursive && (strchr (file, '*') || strchr (file, '?')))
	{
		char path[256];
		char wild[256];

		safe_strcpy (wild, file_part (file), sizeof (wild));
		path_part (file, path, sizeof (path));
		if (path[0] != '/' || path[1] != '\0')
			path[strlen (path) - 1] = 0;	/* remove trailing slash */

		dccsess = sess;
		dccto = to;
		dccmaxcps = maxcps;

		free (file);

		recursive = true;
		for_files (path, wild, dcc_send_wild);
		recursive = false;

		return;
	}

	auto dcc = new_dcc ();
	if (!dcc)
	{
		free (file);
		return;
	}
	dcc->file = file;
	dcc->maxcps = maxcps;

	if (g_stat (file, &st) != -1)
	{

#ifndef USE_DCC64
		if (sizeof (st.st_size) > 4 && st.st_size > 4294967295U)
		{
			PrintText (sess, "Cannot send files larger than 4 GB.\n");
			goto xit;
		}
#endif

		if (!(*file_part (file)) || S_ISDIR (st.st_mode) || st.st_size < 1)
		{
			PrintText (sess, "Cannot send directories or empty files.\n");
			goto xit;
		}

		dcc->starttime = dcc->offertime = time (0);
		dcc->serv = sess->server;
		dcc->dccstat = ::hexchat::dcc_state::queued;
		dcc->size = st.st_size;
		dcc->type = DCC::dcc_type::TYPE_SEND;
		dcc->fp = g_open (file, OFLAGS | O_RDONLY, 0);
		if (dcc->fp != -1)
		{
			if (passive || dcc_listen_init (dcc, sess))
			{
				char havespaces = 0;
				while (*file)
				{
					if (*file == ' ')
					{
						if (prefs.hex_dcc_send_fillspaces)
							*file = '_';
						else
						havespaces = 1;
					}
					file++;
				}
				dcc->nick = new_strdup (to);
				if (prefs.hex_gui_autoopen_send)
				{
					if (fe_dcc_open_send_win (true))	/* already open? add */
						::fe::fe_dcc_add (dcc);
				} else
					::fe::fe_dcc_add (dcc);

				if (passive)
				{
					dcc->pasvid = new_id();
					snprintf (outbuf, sizeof (outbuf), (havespaces) ?
							"DCC SEND \"%s\" 199 0 %" DCC_SFMT " %d" :
							"DCC SEND %s 199 0 %" DCC_SFMT " %d",
							file_part (dcc->file),
							dcc->size, dcc->pasvid);
				}
				else
				{
					snprintf (outbuf, sizeof (outbuf), (havespaces) ?
							"DCC SEND \"%s\" %u %d %" DCC_SFMT :
							"DCC SEND %s %u %d %" DCC_SFMT,
							file_part (dcc->file), dcc->addr,
							dcc->port, dcc->size);
				}
				sess->server->p_ctcp (to, outbuf);

				std::string mutable_to(to);
				EMIT_SIGNAL (XP_TE_DCCOFFER, sess, gsl::ensure_z(file_part (dcc->file)),
								 mutable_to, gsl::ensure_z(dcc->file), nullptr, 0);
			} else
			{
				dcc_close (dcc, ::hexchat::dcc_state::queued, true);
			}
			return;
		}
	}
	PrintTextf (sess, _("Cannot access %s\n"), dcc->file);
	PrintTextf (sess, "%s %d: %s\n", _("Error"), errno, errorstring (errno));
xit:
	dcc_close (dcc, ::hexchat::dcc_state::queued, true);		/* dcc_close will free dcc->file */
}

::dcc::DCC *
find_dcc (const char *nick, const char *file, DCC::dcc_type type)
{
	GSList *list = dcc_list;
	::dcc::DCC *dcc;
	while (list)
	{
		dcc = (::dcc::DCC *) list->data;
		if (nick == nullptr || !rfc_casecmp (nick, dcc->nick))
		{
			if (type == DCC::dcc_type::TYPE_ERROR || dcc->type == type)
			{
				if (!file[0])
					return dcc;
				if (!g_ascii_strcasecmp (file, file_part (dcc->file)))
					return dcc;
				if (!g_ascii_strcasecmp (file, dcc->file))
					return dcc;
			}
		}
		list = list->next;
	}
	return 0;
}

/* called when we receive a NICK change from server */

void
dcc_change_nick (const server &serv, const char *oldnick, const char *newnick)
{
	::dcc::DCC *dcc;
	GSList *list = dcc_list;

	while (list)
	{
		dcc = (::dcc::DCC *) list->data;
		if (dcc->serv == &serv)
		{
			if (!serv.p_cmp (dcc->nick, oldnick))
			{
				if (dcc->nick)
					free (dcc->nick);
				dcc->nick = new_strdup (newnick);
			}
		}
		list = list->next;
	}
}

void
dcc_get (::dcc::DCC *dcc)
{
	switch (dcc->dccstat)
	{
	case ::hexchat::dcc_state::queued:
		if (dcc->type != DCC::dcc_type::TYPE_CHATSEND)
		{
			if (dcc->type == DCC::dcc_type::TYPE_RECV && prefs.hex_dcc_auto_resume && dcc->resumable)
			{
				::dcc::dcc_resume (dcc);
			}
			else
			{
				dcc->resumable = 0;
				dcc->pos = 0;
				dcc_connect (dcc);
			}
		}
		break;
	case ::hexchat::dcc_state::done:
	case ::hexchat::dcc_state::failed:
	case ::hexchat::dcc_state::aborted:
		dcc_close (dcc, ::hexchat::dcc_state::queued, true);
		break;
	}
}

/* for "Save As..." dialog */

void
dcc_get_with_destfile (::dcc::DCC *dcc, char *file)
{
	g_free (dcc->destfile);
	dcc->destfile = new_strdup (file);	/* utf-8 */

	/* since destfile changed, must check resumability again */
	is_resumable (dcc);

	dcc_get (dcc);
}

void
dcc_get_nick (struct session *sess, char *nick)
{
	::dcc::DCC *dcc;
	GSList *list = dcc_list;
	while (list)
	{
		dcc = (::dcc::DCC *) list->data;
		if (!sess->server->p_cmp (nick, dcc->nick))
		{
			if (dcc->dccstat == ::hexchat::dcc_state::queued && dcc->type == DCC::dcc_type::TYPE_RECV)
			{
				dcc->resumable = 0;
				dcc->pos = 0;
				dcc->ack = 0;
				dcc_connect (dcc);
				return;
			}
		}
		list = list->next;
	}
	if (sess)
		EMIT_SIGNAL (XP_TE_DCCIVAL, sess, nullptr, nullptr, nullptr, nullptr, 0);
}

void
dcc_chat (struct session *sess, char *nick, int passive)
{
	char outbuf[512];
	::dcc::DCC *dcc;

	dcc = ::dcc::find_dcc(nick, "", DCC::dcc_type::TYPE_CHATSEND);
	if (dcc)
	{
		switch (dcc->dccstat)
		{
		case ::hexchat::dcc_state::active:
		case ::hexchat::dcc_state::queued:
		case ::hexchat::dcc_state::connecting:
			EMIT_SIGNAL (XP_TE_DCCCHATREOFFER, sess, gsl::ensure_z(nick), nullptr, nullptr, nullptr, 0);
			return;
		case ::hexchat::dcc_state::aborted:
		case ::hexchat::dcc_state::failed:
			dcc_close (dcc, ::hexchat::dcc_state::queued, true);
		}
	}
	dcc = ::dcc::find_dcc(nick, "", DCC::dcc_type::TYPE_CHATRECV);
	if (dcc)
	{
		switch (dcc->dccstat)
		{
		case ::hexchat::dcc_state::queued:
			dcc_connect (dcc);
			break;
		case ::hexchat::dcc_state::failed:
		case ::hexchat::dcc_state::aborted:
			dcc_close (dcc, ::hexchat::dcc_state::queued, true);
		}
		return;
	}
	/* offer DCC CHAT */

	dcc = new_dcc ();
	if (!dcc)
		return;
	dcc->starttime = dcc->offertime = time (0);
	dcc->serv = sess->server;
	dcc->dccstat = ::hexchat::dcc_state::queued;
	dcc->type = DCC::dcc_type::TYPE_CHATSEND;
	dcc->nick = new_strdup (nick);
	if (passive || dcc_listen_init (dcc, sess))
	{
		if (prefs.hex_gui_autoopen_chat)
		{
			if (fe_dcc_open_chat_win (true))	/* already open? add only */
				::fe::fe_dcc_add (dcc);
		} else
			::fe::fe_dcc_add (dcc);

		if (passive)
		{
			dcc->pasvid = new_id ();
			snprintf (outbuf, sizeof (outbuf), "DCC CHAT chat 199 %d %d",
						 dcc->port, dcc->pasvid);
		} else
		{
			snprintf (outbuf, sizeof (outbuf), "DCC CHAT chat %u %d",
						 dcc->addr, dcc->port);
		}
		dcc->serv->p_ctcp (nick, outbuf);
		EMIT_SIGNAL (XP_TE_DCCCHATOFFERING, sess, gsl::ensure_z(nick), nullptr, nullptr, nullptr, 0);
	} else
	{
		dcc_close (dcc, ::hexchat::dcc_state::queued, true);
	}
}

int
dcc_resume (::dcc::DCC *dcc)
{
	char tbuf[500];

	if (dcc->dccstat == ::hexchat::dcc_state::queued && dcc->resumable)
	{
		dcc->resume_sent = 1;
		/* filename contains spaces? Quote them! */
		snprintf (tbuf, sizeof (tbuf) - 10, strchr (dcc->file, ' ') ?
					  "DCC RESUME \"%s\" %d %" DCC_SFMT :
					  "DCC RESUME %s %d %" DCC_SFMT,
					  dcc->file, dcc->port, dcc->resumable);

		if (dcc->pasvid)
			sprintf (tbuf + strlen (tbuf), " %d", dcc->pasvid);

		dcc->serv->p_ctcp (dcc->nick, tbuf);
		return 1;
	}

	return 0;
}

void
handle_dcc (struct session *sess, char *nick, char *word[], char *word_eol[],
				const message_tags_data *tags_data)
{
	char tbuf[512];
	::dcc::DCC *dcc;
	char *type = word[5];
	int port, pasvid = 0;
	guint32 addr;
	::dcc::DCC_SIZE size;
	int psend = 0;

	if (!g_ascii_strcasecmp (type, "CHAT"))
	{
		port = atoi (word[8]);
		addr = strtoul (word[7], nullptr, 10);

		if (port == 0)
			pasvid = atoi (word[9]);
		else if (word[9][0] != 0)
		{
			pasvid = atoi (word[9]);
			psend = 1;
		}

		if (!addr /*|| (port < 1024 && port != 0)*/
			|| port > 0xffff || (port == 0 && pasvid == 0))
		{
			dcc_malformed (sess, nick, word_eol[4] + 2);
			return;
		}

		if (psend)
		{
			dcc = find_dcc_from_id(pasvid, DCC::dcc_type::TYPE_CHATSEND);
			if (dcc)
			{
				dcc->addr = addr;
				dcc->port = port;
				dcc_connect (dcc);
			} else
			{
				dcc_malformed (sess, nick, word_eol[4] + 2);
			}
			return;
		}

		dcc = ::dcc::find_dcc(nick, "", DCC::dcc_type::TYPE_CHATSEND);
		if (dcc)
			dcc_close (dcc, ::hexchat::dcc_state::queued, true);

		dcc = ::dcc::find_dcc(nick, "", DCC::dcc_type::TYPE_CHATRECV);
		if (dcc)
			dcc_close (dcc, ::hexchat::dcc_state::queued, true);

		dcc_add_chat (sess, nick, port, addr, pasvid);
		return;
	}

	if (!g_ascii_strcasecmp (type, "Resume"))
	{
		port = atoi (word[7]);

		if (port == 0)
		{ /* PASSIVE */
			pasvid = atoi(word[9]);
			dcc = find_dcc_from_id(pasvid, DCC::dcc_type::TYPE_SEND);
		} else
		{
			dcc = find_dcc_from_port(port, DCC::dcc_type::TYPE_SEND);
		}
		if (!dcc)
			dcc = ::dcc::find_dcc(nick, word[6], DCC::dcc_type::TYPE_SEND);
		if (dcc)
		{
			size = BIG_STR_TO_INT (word[8]);
			dcc->resumable = size;
			if (dcc->resumable < dcc->size)
			{
				dcc->pos = dcc->resumable;
				dcc->ack = dcc->resumable;
				lseek (dcc->fp, dcc->pos, SEEK_SET);

				/* Checking if dcc is passive and if filename contains spaces */
				if (dcc->pasvid)
					snprintf (tbuf, sizeof (tbuf), strchr (file_part (dcc->file), ' ') ?
							"DCC ACCEPT \"%s\" %d %" DCC_SFMT " %d" :
							"DCC ACCEPT %s %d %" DCC_SFMT " %d",
							file_part (dcc->file), port, dcc->resumable, dcc->pasvid);
				else
					snprintf (tbuf, sizeof (tbuf), strchr (file_part (dcc->file), ' ') ?
							"DCC ACCEPT \"%s\" %d %" DCC_SFMT :
							"DCC ACCEPT %s %d %" DCC_SFMT,
							file_part (dcc->file), port, dcc->resumable);

				dcc->serv->p_ctcp (dcc->nick, tbuf);
			}
			sprintf (tbuf, "%" DCC_SFMT, dcc->pos);
			EMIT_SIGNAL_TIMESTAMP (XP_TE_DCCRESUMEREQUEST, sess, gsl::ensure_z(nick),
				gsl::ensure_z(file_part (dcc->file)), tbuf, nullptr, 0,
										  tags_data->timestamp);
		}
		return;
	}
	if (!g_ascii_strcasecmp (type, "Accept"))
	{
		port = atoi (word[7]);
		dcc = find_dcc_from_port(port, DCC::dcc_type::TYPE_RECV);
		if (dcc && dcc->dccstat == ::hexchat::dcc_state::queued)
		{
			dcc_connect (dcc);
		}
		return;
	}
	if (!g_ascii_strcasecmp (type, "SEND"))
	{
		char *file = file_part (word[6]);

		port = atoi (word[8]);
		addr = strtoul (word[7], nullptr, 10);
		size = BIG_STR_TO_INT (word[9]);

		if (port == 0) /* Passive dcc requested */
			pasvid = atoi (word[10]);
		else if (word[10][0] != 0)
		{
			/* Requesting passive dcc.
			 * Destination user of an active dcc is giving his
			 * true address/port/pasvid data.
			 * This information will be used later to
			 * establish the connection to the user.
			 * We can recognize this type of dcc using word[10]
			 * because this field is always null (no pasvid)
			 * in normal dcc sends.
			 */
			pasvid = atoi (word[10]);
			psend = 1;
		}


		if (!addr || !size /*|| (port < 1024 && port != 0)*/
			|| port > 0xffff || (port == 0 && pasvid == 0))
		{
			dcc_malformed (sess, nick, word_eol[4] + 2);
			return;
		}

		if (psend)
		{
			/* Third Step of Passive send.
			 * Connecting to the destination and finally
			 * sending file.
			 */
			dcc = find_dcc_from_id(pasvid, DCC::dcc_type::TYPE_SEND);
			if (dcc)
			{
				dcc->addr = addr;
				dcc->port = port;
				dcc_connect (dcc);
			} else
			{
				dcc_malformed (sess, nick, word_eol[4] + 2);
			}
			return;
		}

		dcc_add_file (sess, file, size, port, nick, addr, pasvid);

	} else
	{
		EMIT_SIGNAL_TIMESTAMP (XP_TE_DCCGENERICOFFER, sess->server->front_session,
			gsl::ensure_z(word_eol[4] + 2), gsl::ensure_z(nick), nullptr, nullptr, 0,
									  tags_data->timestamp);
	}
}

void
dcc_show_list (struct session *sess)
{
	int i = 0;
	::dcc::DCC *dcc;
	GSList *list = dcc_list;

	EMIT_SIGNAL (XP_TE_DCCHEAD, sess, nullptr, nullptr, nullptr, nullptr, 0);
	while (list)
	{
		dcc = (::dcc::DCC *) list->data;
		i++;
		PrintTextf (sess, " %s  %-10.10s %-7.7s %-7" DCC_SFMT " %-7" DCC_SFMT " %s\n",
					 dcctypes[static_cast<std::size_t>(dcc->type)], dcc->nick,
					 _(dccstat[static_cast<int>(dcc->dccstat)].name), dcc->size, dcc->pos,
					 file_part (dcc->file));
		list = list->next;
	}
	if (!i)
		PrintText (sess, _("No active DCCs\n"));
}

} // ::hexchat::dcc::
} // ::hexchat::
