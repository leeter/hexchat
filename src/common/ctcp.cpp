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
#include <cstring>
#include <locale>
#include <string>
#include <boost/utility/string_ref.hpp>
#include <boost/algorithm/string.hpp>

#ifndef WIN32
#include <unistd.h>
#endif

#include "hexchat.hpp"
#include "cfgfiles.hpp"
#include "util.hpp"
#include "modes.hpp"
#include "outbound.hpp"
#include "ignore.hpp"
#include "inbound.hpp"
#include "dcc.hpp"
#include "text.hpp"
#include "ctcp.hpp"
#include "server.hpp"
#include "hexchatc.hpp"
#include "session.hpp"
#include "sound.hpp"

namespace dcc = hexchat::dcc;

namespace {
static void
	ctcp_reply(session *sess, char *nick, char *word[], char *word_eol[],
	const std::string & conf)
{
	char tbuf[4096] = {};	/* can receive 2048 from IRC, so this is enough */

	/* process %C %B etc */
	auto confs = check_special_chars(conf, true);
	auto_insert({ tbuf, sizeof tbuf }, reinterpret_cast<const unsigned char*>(confs.c_str()), word, word_eol, "", "", word_eol[5],
		sess->server->get_network(true).data(), "", "", nick, "");
	handle_command(sess, tbuf, false);
}

static bool
	ctcp_check(session *sess, char *nick, char *word[], char *word_eol[],
	char *ctcp)
{
	bool ret = false;
	char *po;

	po = std::strchr(ctcp, '\001');
	if (po)
		*po = 0;

	po = std::strchr(word_eol[5], '\001');
	if (po)
		*po = 0;

	std::locale locale;
	for (const auto & pop : ctcp_list)
	{
		if (boost::iequals(pop.name, ctcp, locale))
		{
			ctcp_reply(sess, nick, word, word_eol, pop.cmd);
			ret = true;
		}
	}
	return ret;
}
}

void
ctcp_handle (session *sess, char *to, char *nick, char *ip,
				 char *msg, char *word[], char *word_eol[], bool id,
				 const message_tags_data *tags_data)
{
	char *po;
	session *chansess;
	server *serv = sess->server;
	char outbuf[1024];
	int ctcp_offset = 2;

	if (serv->have_idmsg && (word[4][1] == '+' || word[4][1] == '-') )
			ctcp_offset = 3;

	/* consider DCC to be different from other CTCPs */
	if (!g_ascii_strncasecmp (msg, "DCC", 3))
	{
		/* but still let CTCP replies override it */
		if (!ctcp_check (sess, nick, word, word_eol, word[4] + ctcp_offset))
		{
			if (!ignore_check(word[1], ignore::IG_DCC))
				dcc::handle_dcc (sess, nick, word, word_eol, tags_data);
		}
		return;
	}

	/* consider ACTION to be different from other CTCPs. Check
	  ignore as if it was a PRIV/CHAN. */
	if (!g_ascii_strncasecmp (msg, "ACTION ", 7))
	{
		if (serv->is_channel_name (to))
		{
			/* treat a channel action as a CHAN */
			if (ignore_check(word[1], ignore::IG_CHAN))
				return;
		} else
		{
			/* treat a private action as a PRIV */
			if (ignore_check(word[1], ignore::IG_PRIV))
				return;
		}

		/* but still let CTCP replies override it */
		if (ctcp_check (sess, nick, word, word_eol, word[4] + ctcp_offset))
			goto generic;

		inbound_action (sess, to, nick, ip, msg + 7, false, id, tags_data);
		return;
	}

	if (ignore_check(word[1], ignore::IG_CTCP))
		return;

	if (!g_ascii_strcasecmp (msg, "VERSION") && !prefs.hex_irc_hide_version)
	{
#ifdef WIN32
		snprintf (outbuf, sizeof (outbuf), "VERSION HexChat " PACKAGE_VERSION " [x%d] / %s",
					 get_cpu_arch (), get_sys_str (true));
#else
		snprintf (outbuf, sizeof (outbuf), "VERSION HexChat " PACKAGE_VERSION " / %s",
					 get_sys_str (true));
#endif
		serv->p_nctcp (nick, outbuf);
	}

	if (!ctcp_check (sess, nick, word, word_eol, word[4] + ctcp_offset))
	{
		if (!g_ascii_strncasecmp (msg, "SOUND", 5))
		{
			po = std::strchr (word[5], '\001');
			if (po)
				po[0] = 0;

			if (sess->server->is_channel_name(to))
			{
				chansess = find_channel (*(sess->server), to);
				if (!chansess)
					chansess = sess;

				text_emit (XP_TE_CTCPSNDC, chansess, gsl::ensure_z(word[5]),
					gsl::ensure_z(nick), gsl::ensure_z(to), nullptr, tags_data->timestamp);
			} else
			{
				text_emit (XP_TE_CTCPSND, sess->server->front_session,
					gsl::ensure_z(word[5]), gsl::ensure_z(nick), nullptr, nullptr,
											  tags_data->timestamp);
			}

			/* don't let IRCers specify path */
#ifdef WIN32
			if (std::strchr (word[5], '/') == nullptr && std::strchr (word[5], '\\') == nullptr)
#else
			if (std::strchr (word[5], '/') == nullptr)
#endif
				sound::play (word[5], announce::none);
			return;
		}
	}

generic:
	po = std::strchr (msg, '\001');
	if (po)
		po[0] = 0;

	if (!sess->server->is_channel_name (to))
	{
		EMIT_SIGNAL_TIMESTAMP (XP_TE_CTCPGEN, sess->server->front_session, gsl::ensure_z(msg),
			gsl::ensure_z(nick), nullptr, nullptr, 0, tags_data->timestamp);
	} else
	{
		chansess = find_channel (*(sess->server), to);
		if (!chansess)
			chansess = sess;
		EMIT_SIGNAL_TIMESTAMP (XP_TE_CTCPGENC, chansess, gsl::ensure_z(msg), gsl::ensure_z(nick), gsl::ensure_z(to), nullptr, 0,
									  tags_data->timestamp);
	}
}
