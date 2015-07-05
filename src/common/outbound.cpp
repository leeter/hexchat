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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE	/* for memrchr */
#endif

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <iterator>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <stdexcept>
#include <vector>
#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string_regex.hpp>
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <boost/optional.hpp>
#include <boost/utility/string_ref.hpp>

#define WANTSOCKET
#define WANTARPA
#include "inet.hpp"

#ifndef WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif


#include <sys/stat.h>
#include <fcntl.h>

#include "hexchat.hpp"
#include "plugin.hpp"
#include "ignore.hpp"
#include "util.hpp"
#include "fe.hpp"
#include "cfgfiles.hpp"			  /* hexchat_fopen_file() */
#include "network.hpp"				/* net_ip() */
#include "modes.hpp"
#include "notify.hpp"
#include "inbound.hpp"
#include "text.hpp"
#include "hexchatc.hpp"
#include "servlist.hpp"
#include "server.hpp"
#include "outbound.hpp"
#include "chanopt.hpp"
#include "dcc.hpp"
#include "userlist.hpp"
#include "session.hpp"
#include "glist_iterators.hpp"

namespace hexchat{
namespace fe{
namespace notify{
	void fe_notify_ask(char *name, char *networks);
} // ::hexchat::fe::notify
} // ::hexchat::fe
} // ::hexchat

namespace dcc = hexchat::dcc;
namespace fe_notify = hexchat::fe::notify;
static const size_t TBUFSIZE = 4096;

static void help (session *sess, char *tbuf, const char *helpcmd, bool quiet);
static int cmd_server (session *sess, char *tbuf, char *word[], char *word_eol[]);
static void handle_say (session *sess, char *text, int check_spch);
namespace
{
	using sess_itr = glib_helper::glist_iterator < session > ;
	using serv_itr = glib_helper::glist_iterator < server > ;
}

static void
notj_msg (struct session *sess)
{
	PrintText (sess, _("No channel joined. Try /join #<channel>\n"));
}

void
notc_msg (struct session *sess)
{
	PrintText (sess, _("Not connected. Try /server <host> [<port>]\n"));
}

void
server_sendpart(server & serv, const std::string& channel, const boost::optional<const std::string&>& reason)
{
	if (!reason)
	{
		serv.p_part(channel, prefs.hex_irc_part_reason);
	} 
	else
	{
		/* reason set by /quit, /close argument */
		serv.p_part (channel, reason.get());
	}
}

void
server_sendquit (session * sess)
{
	if (sess->quitreason.empty())
	{
		auto colrea = check_special_chars(boost::string_ref{ prefs.hex_irc_quit_reason }, true);
		sess->server->p_quit(colrea);
	} else
	{
		/* reason set by /quit, /close argument */
		sess->server->p_quit (sess->quitreason);
	}
}

void
process_data_init (char *buf, char *cmd, char *word[],
						 char *word_eol[], bool handle_quotes,
						 bool allow_escape_quotes)
{
	int wordcount = 2;
	bool space{ false };
	bool quote{ false };
	size_t j = 0;
	size_t len;

	word[0] = "\000\000";
	word_eol[0] = "\000\000";
	word[1] = (char *)buf;
	word_eol[1] = (char *)cmd;

	for (;;)
	{
		switch (*cmd)
		{
		case 0:
			buf[j] = 0;
			for (j = wordcount; j < PDIWORDS; j++)
			{
				word[j] = "\000\000";
				word_eol[j] = "\000\000";
			}
			return;
		case '\042':
			if (!handle_quotes)
				goto def;
			/* two quotes turn into 1 */
			if (allow_escape_quotes && cmd[1] == '\042')
			{
				cmd++;
				goto def;
			}
			if (quote)
			{
				quote = false;
				space = false;
			} else
				quote = true;
			cmd++;
			break;
		case ' ':
			if (!quote)
			{
				if (!space)
				{
					buf[j] = 0;
					j++;

					if (wordcount < PDIWORDS)
					{
						word[wordcount] = &buf[j];
						word_eol[wordcount] = cmd + 1;
						wordcount++;
					}

					space = true;
				}
				cmd++;
				break;
			}
		default:
def:
			space = false;
			len = g_utf8_skip[((unsigned char *)cmd)[0]];
			if (len == 1)
			{
				buf[j] = *cmd;
				j++;
				cmd++;
			} else
			{
				/* skip past a multi-byte utf8 char */
				std::copy_n(cmd, len, buf + j);
				j += len;
				cmd += len;
			}
		}
	}
}

static int
cmd_addbutton (struct session *sess, char *, char *word[],
					char *word_eol[])
{
	if (*word[2] && *word_eol[3])
	{
		if (sess->type == session::SESS_DIALOG)
		{
			dlgbutton_list.emplace_back(word_eol[3], word[2]);
			fe_dlgbuttons_update (sess);
		} else
		{
			button_list.emplace_back(word_eol[3], word[2]);
			fe_buttons_update (sess);
		}
		return true;
	}
	return false;
}

/* ADDSERVER <networkname> <serveraddress>, add a new network and server to the network list */
static int
cmd_addserver (struct session *sess, char *, char *word[], char *word_eol[])
{
	ircnet *network;

	/* do we have enough arguments given? */
	if (*word[2] && *word_eol[3])
	{
		network = servlist_net_find (word[2], nullptr, strcmp);

		/* if the given network doesn't exist yet, add it */
		if (!network)
		{
			network = servlist_net_add (word[2], "", true);
			network->encoding = new_strdup (IRC_DEFAULT_CHARSET);
		}
		/* if we had the network already, check if the given server already exists */
		else if (servlist_server_find (network, word_eol[3], nullptr))
		{
			PrintTextf (sess, _("Server %s already exists on network %s.\n"), word_eol[3], word[2]);
			return true;	/* unsuccessful, but the syntax was correct so we don't want to show the help */
		}

		/* server added to new or existing network, doesn't make any difference */
		servlist_server_add (network, word_eol[3]);
		PrintTextf (sess, _("Added server %s to network %s.\n"), word_eol[3], word[2]);
		return true;		/* success */
	}
	else
	{
		return false;		/* print help */
	}
}

static int
cmd_allchannels (session *sess, char *, char *[], char *word_eol[])
{
	GSList *list = sess_list;

	if (!*word_eol[2])
		return false;

	while (list)
	{
		sess = static_cast<session*>(list->data);
		if (sess->type == session::SESS_CHANNEL && sess->channel[0] && sess->server->connected)
		{
			handle_command (sess, word_eol[2], false);
		}
		list = list->next;
	}

	return true;
}

static int
cmd_allchannelslocal (session *sess, char *, char *[], char *word_eol[])
{
	GSList *list = sess_list;
	server *serv = sess->server;

	if (!*word_eol[2])
		return false;

	while (list)
	{
		sess = static_cast<session*>(list->data);
		if (sess->type == session::SESS_CHANNEL && sess->channel[0] &&
			 sess->server->connected && sess->server == serv)
		{
			handle_command (sess, word_eol[2], false);
		}
		list = list->next;
	}

	return true;
}

static int
cmd_allservers (struct session *, char *, char *[],
					 char *word_eol[])
{
	GSList *list;
	server *serv;

	if (!*word_eol[2])
		return false;

	list = serv_list;
	while (list)
	{
		serv = static_cast<server*>(list->data);
		if (serv->connected)
			handle_command (serv->front_session, word_eol[2], false);
		list = list->next;
	}

	return true;
}

static int
cmd_away (struct session *sess, char *, char *[], char *word_eol[])
{
	std::string reason = word_eol[2];

	if (reason.empty())
	{
		if (sess->server->is_away)
		{
			if (!sess->server->last_away_reason.empty())
				PrintTextf (sess, boost::format(_("Already marked away: %s\n")) % sess->server->last_away_reason);
			return false;
		}
		std::string prefs_reason(prefs.hex_away_reason);
		reason = sess->server->reconnect_away ? sess->server->last_away_reason : prefs_reason;
	}
	sess->server->p_set_away (reason);

	if (sess->server->last_away_reason != reason)
	{
			sess->server->last_away_reason = reason;
	}

	if (!sess->server->connected)
		sess->server->reconnect_away = 1;

	return true;
}

static int
cmd_back (struct session *sess, char *, char *[], char *[])
{
	if (sess->server->is_away)
	{
		sess->server->p_set_back ();
	}
	else
	{
		PrintText (sess, _("Already marked back.\n"));
	}

	sess->server->last_away_reason.clear();
	return true;
}

static std::string
create_mask(session * sess, std::string mask, const boost::string_ref &mode, const std::string &typestr, bool deop)
{
	int type;
	std::ostringstream buf;

	auto user = userlist_find (sess, mask);
	if (user && user->hostname)  /* it's a nickname, let's find a proper ban mask */
	{
		const std::string & p2 = deop ? user->nick : std::string{};

		mask = user->hostname.get();

		auto at = mask.find_first_of('@'); /* FIXME: utf8 */	
		if (at == std::string::npos)
			return nullptr;					  /* can't happen? */

		auto submask = mask.substr(0, at);
		std::ostringstream username;
		if (mask[0] == '~' || mask[0] == '+' ||
			mask[0] == '=' || mask[0] == '^' || mask[0] == '-')
		{
			/* the ident is prefixed with something, we replace that sign with an * */
			submask.erase(0, 1);
			username << '*';
		} else if (at - submask.size() < USERNAMELEN)
		{
			/* we just add an * in the begining of the ident */
			username << '*';
		} /*else
		{

			/* ident might be too long, we just ban what it gives and add an * in the end */
			//safe_strcpy (username, mask, sizeof (username));
		//} 
		username << submask;
		auto fullhost = mask.substr(at + 1);

		auto dot = fullhost.find_first_of('.');
		auto domain = dot != std::string::npos ? fullhost.substr(dot) : fullhost;

		if (!typestr.empty())
			type = std::stoi(typestr);
		else
			type = prefs.hex_irc_ban_type;

		if (inet_addr (fullhost.c_str()) != ~0)	/* "fullhost" is really a IP number */
		{
			auto lastdot = fullhost.find_last_of('.');
			if (lastdot == std::string::npos)
				return nullptr;				  /* can't happen? */

			domain = fullhost.substr(0, lastdot);
			buf << mode << " " << p2 << " *!";
			switch (type)
			{
			case 0:
				buf << "*@" << domain << ".*";
				//snprintf (buf, sizeof (buf), "%s %s *!*@%s.*", mode.c_str(), p2, domain.c_str());
				break;

			case 1:
				buf << "*@" << fullhost;
				//snprintf (buf, sizeof (buf), "%s %s *!*@%s", mode, p2, fullhost);
				break;

			case 2:
				buf << username.str() << "@" << domain << ".*";
				//snprintf (buf, sizeof (buf), "%s %s *!%s@%s.*", mode, p2, username, domain);
				break;

			case 3:
				buf << username.str() << "@" << fullhost;
				//snprintf (buf, sizeof (buf), "%s %s *!%s@%s", mode, p2, username, fullhost);
				break;
			}
		} else
		{
			switch (type)
			{
			case 0:
				buf << "*@*" << domain;
				//snprintf (buf, sizeof (buf), "%s %s *!*@*%s", mode, p2, domain);
				break;

			case 1:
				buf << "*@" << fullhost;
				//snprintf (buf, sizeof (buf), "%s %s *!*@%s", mode, p2, fullhost);
				break;

			case 2:
				buf << username.str() << "@*" << domain;
				//snprintf (buf, sizeof (buf), "%s %s *!%s@*%s", mode, p2, username, domain);
				break;

			case 3:
				buf << username.str() << "@" << fullhost;
				//snprintf (buf, sizeof (buf), "%s %s *!%s@%s", mode, p2, username, fullhost);
				break;
			}
		}

	} else
	{
		buf << mode << " " << mask;
		//snprintf (buf, sizeof (buf), "%s %s", mode, mask);
	}
	
	return buf.str();
}

static void
ban(session * sess, std::string mask, const std::string & bantypestr, bool deop)
{
	std::string banmask = create_mask (sess, std::move(mask), deop ? "-o+b" : "+b", bantypestr, deop);
	server *serv = sess->server;
	serv->p_mode(sess->channel, banmask);
}

static int
cmd_ban (struct session *sess, char *, char *word[], char *[])
{
	char *mask = word[2];

	if (*mask)
	{
		ban (sess, mask, word[3] ? word[3] : "", false);
	} else
	{
		sess->server->p_mode (sess->channel, "+b");	/* banlist */
	}

	return true;
}

static int
cmd_unban (struct session *sess, char *, char *word[], char *[])
{
	/* Allow more than one mask in /unban -- tvk */
	auto words = to_vector_strings(word, PDIWORDS + 1);
	for (int i = 2;; ++i)
	{
		if (!*word[i])
		{
			if (i == 2)
				return false;
			send_channel_modes(sess, words, 2, i, '-', 'b', 0);
			return true;
		}
	}
}

static int
cmd_chanopt (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	int ret;
	
	/* chanopt.c */
	ret = chanopt_command (sess, tbuf, word, word_eol);
	chanopt_save_all ();
	
	return ret;
}

static int
cmd_charset (struct session *sess, char *, char *word[], char *[])
{
	server *serv = sess->server;
	const char *locale = nullptr;
	int offset = 0;

	if (strcmp (word[2], "-quiet") == 0)
		offset++;

	if (!word[2 + offset][0])
	{
		g_get_charset (&locale);
		PrintTextf (sess, "Current charset: %s\n",
						serv->encoding ? serv->encoding->c_str() : locale);
		return true;
	}

	if (servlist_check_encoding (word[2 + offset]))
	{
		serv->set_encoding (word[2 + offset]);
		if (offset < 1)
			PrintTextf (sess, "Charset changed to: %s\n", word[2 + offset]);
	} else
	{
		PrintTextf (sess, "\0034Unknown charset:\017 %s\n", word[2 + offset]);
	}

	return true;
}

static int
cmd_clear (struct session *sess, char *, char *[], char *word_eol[])
{
	GSList *list = sess_list;
	char *reason = word_eol[2];

	if (g_ascii_strcasecmp (reason, "HISTORY") == 0)
	{
		sess->hist.clear();
		return true;
	}

	if (g_ascii_strncasecmp (reason, "all", 3) == 0)
	{
		while (list)
		{
			sess = static_cast<session*>(list->data);
			if (!sess->nick_said)
				fe_text_clear(static_cast<session*>(list->data), 0);
			list = list->next;
		}
		return true;
	}

	if (reason[0] != '-' && !std::isdigit<char> (reason[0], std::locale()) && reason[0] != 0)
		return false;

	fe_text_clear (sess, atoi (reason));
	return true;
}

static int
cmd_close (struct session *sess, char *, char *word[], char *word_eol[])
{
	GSList *list;

	if (strcmp (word[2], "-m") == 0)
	{
		list = sess_list;
		while (list)
		{
			sess = static_cast<session*>(list->data);
			list = list->next;
			if (sess->type == session::SESS_DIALOG)
				fe_close_window (sess);
		}
	} else
	{
		if (*word_eol[2])
			sess->quitreason = word_eol[2];
		fe_close_window (sess);
	}

	return true;
}

static int
cmd_ctcp (struct session *sess, char *, char *word[], char *word_eol[])
{
	int mbl;
	char *to = word[2];
	if (*to)
	{
		char *msg = word_eol[3];
		if (*msg)
		{
			unsigned char *cmd = (unsigned char *)msg;

			/* make the first word upper case (as per RFC) */
			for (;;)
			{
				if (*cmd == ' ' || *cmd == 0)
					break;
				mbl = g_utf8_skip[*cmd];
				if (mbl == 1)
					*cmd = std::toupper (*cmd);
				cmd += mbl;
			}

			sess->server->p_ctcp (to, msg);

			EMIT_SIGNAL (XP_TE_CTCPSEND, sess, to, msg, nullptr, nullptr, 0);

			return true;
		}
	}
	return false;
}

static int
cmd_country (struct session *sess, char *, char *word[], char *[])
{
	char *code = word[2];
	if (!code || !*code)
	{
		return false;
	}
	/* search? */
	if (std::strcmp(code, "-s") == 0)
	{
		country_search(word[3], sess, static_cast<void(*)(session*, const boost::format &)>(PrintTextf));
		return true;
	}

	/* search, but forgot the -s */
	if (std::strchr(code, '*'))
	{
		country_search(code, sess, static_cast<void(*)(session*, const boost::format &)>(PrintTextf));
		return true;
	}

	std::string code_buf(code);
	PrintTextf(sess, boost::format(_("%s = %s\n")) % code_buf % country(code_buf));
	return true;
}

static int
cmd_cycle (struct session *sess, char *, char *word[], char *[])
{
	char *chan = word[2];

	if (!*chan)
		chan = sess->channel;

	if (chan)
	{
		auto chan_sess = find_channel (*(sess->server), chan);

		if (chan_sess && chan_sess->type == session::SESS_CHANNEL)
		{
			auto key = chan_sess->channelkey;
			sess->server->p_cycle (chan, key);
			return true;
		}
	}

	return false;
}

static int
cmd_dcc (struct session *sess, char *, char *word[], char *[])
{
	int goodtype;
	dcc::DCC *dcc = 0;
	char *type = word[2];
	if (*type)
	{
		if (!g_ascii_strcasecmp (type, "HELP"))
			return false;
		if (!g_ascii_strcasecmp (type, "CLOSE"))
		{
			if (*word[3] && *word[4])
			{
				goodtype = 0;
				if (!g_ascii_strcasecmp (word[3], "SEND"))
				{
					dcc = dcc::find_dcc(word[4], word[5], ::dcc::DCC::dcc_type::TYPE_SEND);
					dcc_abort (sess, dcc);
					goodtype = true;
				}
				if (!g_ascii_strcasecmp (word[3], "GET"))
				{
					dcc = dcc::find_dcc(word[4], word[5], ::dcc::DCC::dcc_type::TYPE_RECV);
					dcc_abort (sess, dcc);
					goodtype = true;
				}
				if (!g_ascii_strcasecmp (word[3], "CHAT"))
				{
					dcc = dcc::find_dcc(word[4], "", ::dcc::DCC::dcc_type::TYPE_CHATRECV);
					if (!dcc)
						dcc = dcc::find_dcc(word[4], "", ::dcc::DCC::dcc_type::TYPE_CHATSEND);
					dcc_abort (sess, dcc);
					goodtype = true;
				}

				if (!goodtype)
					return false;

				if (!dcc)
					EMIT_SIGNAL (XP_TE_NODCC, sess, nullptr, nullptr, nullptr, nullptr, 0);

				return true;

			}
			return false;
		}
		if ((!g_ascii_strcasecmp (type, "CHAT")) || (!g_ascii_strcasecmp (type, "PCHAT")))
		{
			char *nick = word[3];
			int passive = (!g_ascii_strcasecmp(type, "PCHAT")) ? 1 : 0;
			if (*nick)
				dcc::dcc_chat (sess, nick, passive);
			return true;
		}
		if (!g_ascii_strcasecmp (type, "LIST"))
		{
			dcc::dcc_show_list (sess);
			return true;
		}
		if (!g_ascii_strcasecmp (type, "GET"))
		{
			char *nick = word[3];
			char *file = word[4];
			if (!*file)
			{
				if (*nick)
					dcc::dcc_get_nick (sess, nick);
			} else
			{
				dcc = dcc::find_dcc(nick, file, ::dcc::DCC::dcc_type::TYPE_RECV);
				if (dcc)
					dcc_get (dcc);
				else
					EMIT_SIGNAL (XP_TE_NODCC, sess, nullptr, nullptr, nullptr, nullptr, 0);
			}
			return true;
		}
		if ((!g_ascii_strcasecmp (type, "SEND")) || (!g_ascii_strcasecmp (type, "PSEND")))
		{
			int i = 3, maxcps;
			char *nick, *file;
			int passive = (!g_ascii_strcasecmp(type, "PSEND")) ? 1 : 0;

			nick = word[i];
			if (!*nick)
				return false;

			maxcps = prefs.hex_dcc_max_send_cps;
			if (!g_ascii_strncasecmp(nick, "-maxcps=", 8))
			{
				maxcps = atoi(nick + 8);
				i++;
				nick = word[i];
				if (!*nick)
					return false;
			}

			i++;

			file = word[i];
			if (!*file)
			{
				fe_dcc_send_filereq (sess, nick, maxcps, passive);
				return true;
			}

			do
			{
				dcc::dcc_send (sess, nick, file, maxcps, passive);
				i++;
				file = word[i];
			}
			while (*file);

			return true;
		}

		return false;
	}

	dcc::dcc_show_list (sess);
	return true;
}

static int
cmd_debug (struct session *sess, char*, char *[], char *[])
{
	PrintText (sess, _("Session   T Channel    WaitChan  WillChan  Server\n"));
	for (sess_itr s{ sess_list }, end; s != end; ++s)
	{
		std::ostringstream out;
		out << boost::format(_("%p %1x %-10.10s %-10.10s %-10.10s %p\n")) % &(*s) % s->type % s->channel % s->waitchannel %
			s->willjoinchannel % s->server;
		PrintText (sess, out.str());
	}

	PrintText (sess, _("Server    Sock  Name\n"));
	for (serv_itr v{ serv_list }, end; v != end; ++v)
	{
		std::ostringstream out;
		out << boost::format(_("%p %-5d %s\n")) % &(*v) % v->sok % v->servername;
		PrintText (sess, out.str());
	}

	std::ostringstream out;
	out << boost::format(_(
		"\nfront_session: %p\n"
		"current_tab: %p\n\n"
		)) % sess->server->front_session % current_tab;
	PrintText (sess, out.str());

	return true;
}

static int
cmd_delbutton (struct session *sess, char *, char *word[],
					char *[])
{
	if (*word[2])
	{
		if (sess->type == session::SESS_DIALOG)
		{
			if (list_delentry (dlgbutton_list, word[2]))
				fe_dlgbuttons_update (sess);
		} else
		{
			if (list_delentry (button_list, word[2]))
				fe_buttons_update (sess);
		}
		return true;
	}
	return false;
}

static int
cmd_dehop (struct session *sess, char *, char *word[], char *[])
{
	auto words = to_vector_strings(word, PDIWORDS + 1);
	for (int i = 2;; ++i)
	{
		if (!*word[i])
		{
			if (i == 2)
				return false;
			send_channel_modes (sess, words, 2, i, '-', 'h', 0);
			return true;
		}
	}
}

static int
cmd_deop (struct session *sess, char *, char *word[], char *[])
{
	auto  words = to_vector_strings(word, PDIWORDS + 1);
	for (int i = 2;; ++i)
	{
		if (!*word[i])
		{
			if (i == 2)
				return false;
			send_channel_modes (sess, words, 2, i, '-', 'o', 0);
			return true;
		}
	}
}

static int
cmd_mdehop (struct session *sess, char *, char *[], char *[])
{
	std::vector<std::string> nicks;
	for (const auto & user : sess->usertree)
	{
		if (user->hop && !user->me)
		{
			nicks.emplace_back(user->nick);
		}
	}
	send_channel_modes (sess, nicks, 0, nicks.size(), '-', 'h', 0);

	return true;
}

static int
cmd_mdeop (struct session *sess, char *, char *[], char *[])
{
	std::vector<std::string> nicks;
	for (const auto & user : sess->usertree)
	{
		if (user->op && !user->me)
		{
				nicks.emplace_back(user->nick);
		}
	}
	send_channel_modes (sess, nicks, 0, nicks.size(), '-', 'o', 0);

	return true;
}

std::vector<std::unique_ptr<menu_entry> > menu_list;

/* strings equal? but ignore underscores */
bool menu_streq (const char s1[], const char s2[], bool def)
{
	/* for separators */
	if (s1 == nullptr && s2 == nullptr)
		return false;
	if (s1 == nullptr || s2 == nullptr)
		return true;
	while (*s1)
	{
		if (*s1 == '_')
			s1++;
		if (*s2 == '_')
			s2++;
		if (*s1 != *s2)
			return true;
		s1++;
		s2++;
	}
	if (!*s2)
		return false;
	return def;
}

static menu_entry *menu_entry_find (const char path[], const char label[])
{
	auto result = std::find_if(menu_list.cbegin(), menu_list.cend(), [path, label](const std::unique_ptr<menu_entry> &me){
		return me->label && label && (me->label && me->label.get() == label);
	});
	if (result != menu_list.cend())
		return result->get();
	return nullptr;
}

static void
menu_del_children (const char path[], const char label[])
{
	char buf[512];

	if (!label)
		label = "";
	if (path[0])
		snprintf (buf, sizeof (buf), "%s/%s", path, label);
	else
		snprintf (buf, sizeof (buf), "%s", label);

	menu_list.erase(std::remove_if(menu_list.begin(), menu_list.end(), [&buf](const std::unique_ptr<menu_entry> & me){
		return !menu_streq(buf, me->path.c_str(), false);
	}), menu_list.end());
}

static bool menu_del (const char path[], const char label[])
{
	auto me = std::find_if(menu_list.begin(), menu_list.end(), [label, path](const std::unique_ptr<menu_entry> & me)
	{
		return !menu_streq(me->label ? me->label->c_str() : nullptr, label, true)
			&& !menu_streq(me->path.c_str(), path, true);
	});

	if (me == menu_list.end())
	{
		return false;
	}

	fe_menu_del(me->get());
	menu_list.erase(me);
	
	/* delete this item's children, if any */
	menu_del_children(path, label);
	return true;
}

static bool
menu_is_mainmenu_root (const char path[], gint16 &offset)
{
	static const char *const menus[] = {"\x4$TAB","\x5$TRAY","\x4$URL","\x5$NICK","\x5$CHAN"};

	for (int i = 0; i < 5; i++)
	{
		if (!strncmp (path, menus[i] + 1, menus[i][0]))
		{
			offset = menus[i][0];	/* number of bytes to offset the root */
			if (path[offset] != '\0')
				offset += 1;
			return false;	/* is not main menu */
		}
	}

	offset = 0;
	return true;	/* is main menu */
}

static void
menu_add (const char path[], const char label[], const char cmd[], const char ucmd[], int pos, int state, bool markup, bool enable, int mod, int key, const char group[], const char icon[])
{
	/* already exists? */
	menu_entry *me = menu_entry_find(path, label);
	if (me)
	{
		/* update only */
		me->state = state;
		me->enable = enable;
		fe_menu_update (me);
		return;
	}
	std::unique_ptr<menu_entry> me_ptr(new menu_entry);
	me = me_ptr.get();
	me->pos = pos;
	me->modifier = mod;
	me->is_main = menu_is_mainmenu_root (path, me->root_offset);
	me->state = state;
	me->markup = markup;
	me->enable = enable;
	me->key = key;
	me->path = path;

	if (label)
		me->label = std::string(label);
	if (cmd)
		me->cmd = std::string(cmd);
	if (ucmd)
		me->ucmd = std::string(ucmd);
	if (group)
		me->group = std::string(group);
	if (icon)
		me->icon = std::string(icon);

	menu_list.emplace_back(std::move(me_ptr));
	glib_string menu_label(fe_menu_add (me)); /* this is from pango */
	if (menu_label)
	{
		/* FE has given us a stripped label */
		me->label = std::string(menu_label.get());
	}
}

static int
cmd_menu (struct session *, char *tbuf, char *word[], char *[])
{
	if (!word[2][0] || !word[3][0])
		return false;
	int idx = 2;	
	bool enable = true;
	/* -eX enabled or not? */
	if (word[idx][0] == '-' && word[idx][1] == 'e')
	{
		enable = !!atoi (word[idx] + 2);
		idx++;
	}
	char *icon = nullptr;
	/* -i<ICONFILE> */
	if (word[idx][0] == '-' && word[idx][1] == 'i')
	{
		icon = word[idx] + 2;
		idx++;
	}

	int key = 0;
	int mod = 0;
	/* -k<mod>,<key> key binding */
	if (word[idx][0] == '-' && word[idx][1] == 'k')
	{
		const char *comma = strchr (word[idx], ',');
		if (!comma)
			return false;
		mod = atoi (word[idx] + 2);
		key = atoi (comma + 1);
		idx++;
	}
	bool markup = false;
	/* -m to specify PangoMarkup language */
	if (word[idx][0] == '-' && word[idx][1] == 'm')
	{
		markup = true;
		idx++;
	}
	int pos = 0xffff;
	/* -pX to specify menu position */
	if (word[idx][0] == '-' && word[idx][1] == 'p')
	{
		pos = atoi (word[idx] + 2);
		idx++;
	}

	char *group = nullptr;
	int state = 0;
	/* -rSTATE,GROUP to specify a radio item */
	if (word[idx][0] == '-' && word[idx][1] == 'r')
	{
		state = atoi (word[idx] + 2);
		group = word[idx] + 4;
		idx++;
	}
	
	bool toggle = false;
	/* -tX to specify toggle item with default state */
	if (word[idx][0] == '-' && word[idx][1] == 't')
	{
		state = atoi (word[idx] + 2);
		idx++;
		toggle = true;
	}

	if (word[idx+1][0] == 0)
		return false;

	/* the path */
	path_part (word[idx+1], tbuf, 512);
	auto len = strlen (tbuf);
	if (len)
		tbuf[len - 1] = 0;

	/* the name of the item */
	auto label = file_part (word[idx + 1]);
	if (label[0] == '-' && label[1] == 0)
		label = nullptr;	/* separator */

	if (markup)
	{
		/* to force pango closing tags through */
		for (auto p = label; *p; p++)
			if (*p == 3)
				*p = '/';
	}

	if (!g_ascii_strcasecmp (word[idx], "ADD"))
	{
		if (toggle)
		{
			menu_add (tbuf, label, word[idx + 2], word[idx + 3], pos, state, markup, enable, mod, key, nullptr, nullptr);
		} else
		{
			if (word[idx + 2][0])
				menu_add (tbuf, label, word[idx + 2], nullptr, pos, state, markup, enable, mod, key, group, icon);
			else
				menu_add (tbuf, label, nullptr, nullptr, pos, state, markup, enable, mod, key, group, icon);
		}
		return true;
	}

	if (!g_ascii_strcasecmp (word[idx], "DEL"))
	{
		menu_del (tbuf, label);
		return true;
	}

	return false;
}

static int
cmd_mkick (struct session *sess, char *, char *[], char *word_eol[])
{
	const std::string reason = word_eol[2] ? word_eol[2] : std::string();
	for (auto & user : sess->usertree)
	{
		if (user->op && !user->me)
		{
			sess->server->p_kick(sess->channel, user->nick, reason);
		}
	}
	for (auto & user : sess->usertree)
	{
		if (!user->op && !user->me)
		{
			sess->server->p_kick(sess->channel, user->nick, reason);
		}
	}
	return true;
}

static int
cmd_devoice (struct session *sess, char *, char *word[], char *[])
{
	auto words = to_vector_strings(word, PDIWORDS + 1);
	for (int i = 2;; ++i)
	{
		if (!*word[i])
		{
			if (i == 2)
				return false;
			send_channel_modes (sess, words, 2, i, '-', 'v', 0);
			return true;
		}
	}
}

static int
cmd_discon (struct session *sess, char *, char *[], char *[])
{
	sess->server->disconnect (sess, true, -1);
	return true;
}

static int
cmd_dns (struct session *sess, char *, char *word[], char *[])
{
	const char *nick = word[2];
	if (*nick)
	{
		message_tags_data no_tags = message_tags_data();
		auto user = userlist_find (sess, nick);
		if (user)
		{
			if (user->hostname)
			{
				do_dns (sess, user->nick.c_str(), user->hostname->c_str(), &no_tags);
			} else
			{
				sess->server->p_get_ip (nick);
				sess->server->doing_dns = true;
			}
		} else
		{
			do_dns (sess, nullptr, nick, &no_tags);
		}
		return true;
	}
	return false;
}

static int
cmd_echo (struct session *sess, char *, char *[], char *word_eol[])
{
	PrintText (sess, word_eol[2]);
	return true;
}

#ifndef WIN32

static void
exec_check_process (struct session *sess)
{
	if (sess->running_exec == nullptr)
		return;
	auto val = waitpid (sess->running_exec->childpid, nullptr, WNOHANG);
	if (val == -1 || val > 0)
	{
		close (sess->running_exec->myfd);
		fe_input_remove (sess->running_exec->iotag);
		free (sess->running_exec);
		sess->running_exec = nullptr;
	}
}

#ifndef __EMX__
static int
cmd_execs (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	exec_check_process (sess);
	if (sess->running_exec == nullptr)
	{
		EMIT_SIGNAL (XP_TE_NOCHILD, sess, nullptr, nullptr, nullptr, nullptr, 0);
		return false;
	}
	int r = kill (sess->running_exec->childpid, SIGSTOP);
	if (r == -1)
		PrintText (sess, "Error in kill(2)\n");

	return true;
}

static int
cmd_execc (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	int r;

	exec_check_process (sess);
	if (sess->running_exec == nullptr)
	{
		EMIT_SIGNAL (XP_TE_NOCHILD, sess, nullptr, nullptr, nullptr, nullptr, 0);
		return false;
	}
	r = kill (sess->running_exec->childpid, SIGCONT);
	if (r == -1)
		PrintText (sess, "Error in kill(2)\n");

	return true;
}

static int
cmd_execk (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	int r;

	exec_check_process (sess);
	if (sess->running_exec == nullptr)
	{
		EMIT_SIGNAL (XP_TE_NOCHILD, sess, nullptr, nullptr, nullptr, nullptr, 0);
		return false;
	}
	if (strcmp (word[2], "-9") == 0)
		r = kill (sess->running_exec->childpid, SIGKILL);
	else
		r = kill (sess->running_exec->childpid, SIGTERM);
	if (r == -1)
		PrintText (sess, "Error in kill(2)\n");

	return true;
}

/* OS/2 Can't have the /EXECW command because it uses pipe(2) not socketpair
   and thus it is simplex --AGL */
static int
cmd_execw (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	exec_check_process (sess);
	if (sess->running_exec == nullptr)
	{
		EMIT_SIGNAL (XP_TE_NOCHILD, sess, nullptr, nullptr, nullptr, nullptr, 0);
		return false;
	}
	std::string temp(word_eol[2]);
	temp += "\n\0";
	PrintText(sess, temp);
	write(sess->running_exec->myfd, temp.c_str(), temp.size());
	return true;
}
#endif /* !__EMX__ */

/* convert ANSI escape color codes to mIRC codes */

static short escconv[] =
/* 0 1 2 3 4 5  6 7  0 1 2 3 4  5  6  7 */
{  1,4,3,5,2,10,6,1, 1,7,9,8,12,11,13,1 };

static void
exec_handle_colors (std::string & buf, int len)
{
	char numb[16] = { 0 };
	int i = 0, j = 0, k = 0, firstn = 0, col, colf = 0, colb = 0;
	bool esc = false, backc = false, bold = false;

	/* any escape codes in this text? */
	if (buf.find_first_of(27) == std::string::npos)
		return;

	std::string nbuf(len + 1, '\0');
	std::locale locale;
	while (i < len)
	{
		switch (buf[i])
		{
		case '\r':
			break;
		case 27:
			esc = true;
			break;
		case ';':
			if (!esc)
				goto norm;
			backc = true;
			numb[k] = 0;
			firstn = atoi (numb);
			k = 0;
			break;
		case '[':
			if (!esc)
				goto norm;
			break;
		default:
			if (esc)
			{
				if (buf[i] >= 'A' && buf[i] <= 'z')
				{
					if (buf[i] == 'm')
					{
						/* ^[[0m */
						if (k == 0 || (numb[0] == '0' && k == 1))
						{
							nbuf[j] = '\017';
							j++;
							bold = false;
							goto cont;
						}

						numb[k] = 0;
						col = atoi (numb);
						backc = false;

						if (firstn == 1)
							bold = true;

						if (firstn >= 30 && firstn <= 37)
							colf = firstn - 30;

						if (col >= 40)
						{
							colb = col - 40;
							backc = true;
						}

						if (col >= 30 && col <= 37)
							colf = col - 30;

						if (bold)
							colf += 8;

						if (backc)
						{
							colb = escconv[colb % 14];
							colf = escconv[colf % 14];
							j += sprintf (&nbuf[j], "\003%d,%02d", colf, colb);
						} else
						{
							colf = escconv[colf % 14];
							j += sprintf (&nbuf[j], "\003%02d", colf);
						}
					}
cont:				esc = false;
					backc = false;
					k = 0;
				} else
				{
					if (std::isdigit (buf[i], locale) && k < (sizeof (numb) - 1))
					{
						numb[k] = buf[i];
						k++;
					}
				}
			} else
			{
norm:			nbuf[j] = buf[i];
				j++;
			}
		}
		i++;
	}

	nbuf[j] = 0;
	std::copy_n (nbuf.cbegin(), j + 1, buf.begin());
}

#ifndef HAVE_MEMRCHR
static void *
memrchr (const void *block, int c, size_t size)
{
	for (auto p = (unsigned char *)block + size; p != block; p--)
		if (*p == c)
			return p;
	return nullptr;
}
#endif

static gboolean
exec_data (GIOChannel *source, GIOCondition condition, struct nbexec *s)
{
	char *readpos;
	int rd;
	int sok = s->myfd;
	std::string buf;

	auto len = s->linebuf.size();
	if (!s->linebuf.empty()) {
		/* append new data to buffered incomplete line */
		buf = s->linebuf;
		buf.resize(len + 2050);
		readpos = &buf[0] + len;
		s->linebuf.erase();
	}
	else
	{
		buf.resize(2050, '\0');
		readpos = &buf[0];
	}

	rd = read (sok, readpos, 2048);
	if (rd < 1)
	{
		/* The process has died */
		kill(s->childpid, SIGKILL);
		if (len) {
			buf[len] = '\0';
			exec_handle_colors(buf, len);
			if (s->tochannel)
			{
				/* must turn off auto-completion temporarily */
				unsigned int old = prefs.hex_completion_auto;
				prefs.hex_completion_auto = 0;
				handle_multiline (s->sess, &buf[0], false, true);
				prefs.hex_completion_auto = old;
			}
			else
				PrintText (s->sess, buf);
		}
		waitpid (s->childpid, nullptr, 0);
		s->sess->running_exec = nullptr;
		fe_input_remove (s->iotag);
		close (sok);
		delete s;
		return true;
	}
	len += rd;
	buf[len] = '\0';

	auto rest_pos = buf.find_last_of('\n');// static_cast<char*>(memrchr(buf, '\n', len));
	auto rest = buf.begin();
	if (rest_pos != std::string::npos)
		rest += rest_pos + 1;

	if (*rest) {
		s->linebuf.resize(len - (rest - buf.begin()));
		std::copy_n(rest, s->linebuf.size(), s->linebuf.begin());
		*rest = '\0';
		len -= s->linebuf.size(); /* possibly 0 */
	}

	if (len) {
		exec_handle_colors (buf, len);
		if (s->tochannel)
			handle_multiline (s->sess, &buf[0], false, true);
		else
			PrintText (s->sess, buf);
	}
	return true;
}

static int
cmd_exec (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	int tochannel = false;
	char *cmd = word_eol[2];
	int fds[2], pid = 0;
	struct nbexec *s;
	int shell = true;
	int fd;

	if (*cmd)
	{
		exec_check_process (sess);
		if (sess->running_exec != nullptr)
		{
			EMIT_SIGNAL (XP_TE_ALREADYPROCESS, sess, nullptr, nullptr, nullptr, nullptr, 0);
			return true;
		}

		if (!strcmp (word[2], "-d"))
		{
			if (!*word[3])
				return false;
			cmd = word_eol[3];
			shell = false;
		}
		else if (!strcmp (word[2], "-o"))
		{
			if (!*word[3])
				return false;
			cmd = word_eol[3];
			tochannel = true;
		}

		if (shell)
		{
			if (access ("/bin/sh", X_OK) != 0)
			{
				fe_message (_("I need /bin/sh to run!\n"), FE_MSG_ERROR);
				return true;
			}
		}

#ifdef __EMX__						  /* if os/2 */
		if (pipe (fds) < 0)
		{
			PrintText (sess, "Pipe create error\n");
			return false;
		}
		setmode (fds[0], O_BINARY);
		setmode (fds[1], O_BINARY);
#else
		if (socketpair (PF_UNIX, SOCK_STREAM, 0, fds) == -1)
		{
			PrintText (sess, "socketpair(2) failed\n");
			return false;
		}
#endif
		s = new nbexec(sess);
		s->myfd = fds[0];
		s->tochannel = tochannel;

		pid = fork ();
		if (pid == 0)
		{
			/* This is the child's context */
			close (0);
			close (1);
			close (2);
			/* Close parent's end of pipe */
			close(s->myfd);
			/* Copy the child end of the pipe to stdout and stderr */
			dup2 (fds[1], 1);
			dup2 (fds[1], 2);
			/* Also copy it to stdin so we can write to it */
			dup2 (fds[1], 0);
			/* Now close all open file descriptors except stdin, stdout and stderr */
			for (fd = 3; fd < 1024; fd++) close(fd);
			/* Now we call /bin/sh to run our cmd ; made it more friendly -DC1 */
			if (shell)
			{
				execl ("/bin/sh", "sh", "-c", cmd, nullptr);
			} else
			{
				char **argv;
				int argc;

				g_shell_parse_argv (cmd, &argc, &argv, nullptr);
				execvp (argv[0], argv);

				g_strfreev (argv);
			}
			/* not reached unless error */
			/*printf("exec error\n");*/
			fflush (stdout);
			_exit (0);
		}
		if (pid == -1)
		{
			/* Parent context, fork() failed */

			PrintText (sess, "Error in fork(2)\n");
			close(fds[0]);
			close(fds[1]);
			delete s;
		} else
		{
			/* Parent path */
			close(fds[1]);
			s->childpid = pid;
			s->iotag = fe_input_add (s->myfd, FIA_READ|FIA_EX, (GIOFunc)exec_data, s);
			sess->running_exec = s;
			return true;
		}
	}
	return false;
}

#endif

#if 0
/* export config stub */
static int
cmd_exportconf (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	/* this is pretty much the same as in hexchat_exit() */
	save_config ();
	if (prefs.save_pevents)
	{
		pevent_save (nullptr);
	}
	sound_save ();
	notify_save ();
	ignore_save ();
	free_sessions ();
	chanopt_save_all ();

	return true;		/* success */
	return false;		/* fail */
}
#endif

static int
cmd_flushq (struct session *sess, char *, char *[], char *[])
{
	PrintTextf(sess, boost::format(_("Flushing server send queue, %d bytes.\n")) % sess->server->sendq_len);
	sess->server->flush_queue ();
	return true;
}

static int
cmd_quit (struct session *sess, char *, char *[], char *word_eol[])
{
	if (word_eol[2] && *word_eol[2])
		sess->quitreason = word_eol[2];
	sess->server->disconnect (sess, true, -1);
	sess->quitreason.erase();
	return 2;
}

static int
cmd_gate (struct session *sess, char *, char *word[], char *[])
{
	char *server_name = word[2];
	if (*server_name)
	{
		server *serv = sess->server;
		char *port = word[3];
#ifdef USE_OPENSSL
		serv->use_ssl = false;
#endif
		server_fill_her_up (*serv);
		if (*port)
			serv->connect (server_name, atoi (port), true);
		else
			serv->connect (server_name, 23, true);
		return true;
	}
	return false;
}
namespace{

struct getvalinfo
{
	std::string cmd;
	session *sess;
};

static void
get_bool_cb (int val, getvalinfo *info)
{
	char buf[512];
	std::unique_ptr<getvalinfo> info_ptr(info);
	snprintf (buf, sizeof (buf), "%s %d", info->cmd.c_str(), val);
	if (is_session (info->sess))
		handle_command (info->sess, buf, false);
}

static int
cmd_getbool (struct session *sess, char *, char *word[], char *word_eol[])
{
	if (!word[4][0])
		return false;

	getvalinfo * info = new getvalinfo;
	info->cmd = word[2];
	info->sess = sess;

	fe_get_bool(word[3], word_eol[4], (GSourceFunc)get_bool_cb, info);

	return true;
}

static void
get_int_cb (int cancel, int val, getvalinfo *info)
{
	std::unique_ptr<getvalinfo> info_ptr(info);
	if (!cancel)
	{
		char buf[512];
		snprintf (buf, sizeof (buf), "%s %d", info->cmd.c_str(), val);
		if (is_session (info->sess))
			handle_command (info->sess, buf, false);
	}
}

static int
cmd_getint (struct session *sess, char *, char *word[], char *[])
{
	if (!word[4][0])
		return false;

	getvalinfo* info = new getvalinfo;
	info->cmd = word[3];
	info->sess = sess;

	fe_get_int(word[4], atoi(word[2]), (GSourceFunc)get_int_cb, info);

	return true;
}

static void
get_file_cb (char *cmd, char *file)
{
	/* execute the command once per file, then once more with
	  no args */
	if (file)
	{
		char buf[1024 + 128];
		snprintf (buf, sizeof (buf), "%s %s", cmd, file);
		handle_command (current_sess, buf, false);
	}
	else
	{
		std::unique_ptr<char[]> cmd_ptr(cmd);
		handle_command (current_sess, cmd, false);
	}
}

static int
cmd_getfile (struct session *, char *, char *word[], char *[])
{
	if (!word[3][0])
		return false;

	int idx = 2;
	int flags = 0;

	if (!std::strcmp (word[2], "-folder"))
	{
		flags |= FRF_CHOOSEFOLDER;
		idx++;
	}

	if (!std::strcmp (word[idx], "-multi"))
	{
		flags |= FRF_MULTIPLE;
		idx++;
	}

	if (!std::strcmp (word[idx], "-save"))
	{
		flags |= FRF_WRITE;
		idx++;
	}

	fe_get_file (word[idx+1], word[idx+2], (void (*)(void*, char*))get_file_cb, new_strdup (word[idx]), flags);

	return true;
}

static void
get_str_cb (int cancel, const char val[], getvalinfo *info)
{
	std::unique_ptr<getvalinfo> info_ptr(info);
	if (!cancel)
	{
		char buf[512];
		snprintf (buf, sizeof (buf), "%s %s", info->cmd.c_str(), val);
		if (is_session (info->sess))
			handle_command (info->sess, buf, false);
	}
}

static int
cmd_getstr (struct session *sess, char *, char *word[], char *[])
{
	if (!word[4][0])
		return false;

	getvalinfo* info = new getvalinfo;
	info->cmd = word[3];
	info->sess = sess;

	fe_get_str(word[4], word[2], (GSourceFunc)get_str_cb, info);

	return true;
}

static int
cmd_ghost (struct session *sess, char *, char *word[], char *[])
{
	if (!word[2][0])
		return false;

	sess->server->p_ns_ghost (word[2], word[3]);
	return true;
}

static int
cmd_gui (struct session *sess, char *, char *word[], char *[])
{
	switch (str_ihash ((const unsigned char*)word[2]))
	{
	case 0x058b836e: fe_ctrl_gui (sess, FE_GUI_APPLY, 0); break; /* APPLY */
	case 0xac1eee45: fe_ctrl_gui(sess, FE_GUI_ATTACH, 2); break; /* ATTACH */
	case 0x05a72f63: fe_ctrl_gui(sess, FE_GUI_COLOR, std::atoi(word[3])); break; /* COLOR */
	case 0xb06a1793: fe_ctrl_gui(sess, FE_GUI_ATTACH, 1); break; /* DETACH */
	case 0x05cfeff0: fe_ctrl_gui(sess, FE_GUI_FLASH, 0); break; /* FLASH */
	case 0x05d154d8: fe_ctrl_gui(sess, FE_GUI_FOCUS, 0); break; /* FOCUS */
	case 0x0030dd42: fe_ctrl_gui(sess, FE_GUI_HIDE, 0); break; /* HIDE */
	case 0x61addbe3: fe_ctrl_gui(sess, FE_GUI_ICONIFY, 0); break; /* ICONIFY */
	case 0xc0851aaa: fe_message (word[3], FE_MSG_INFO|FE_MSG_MARKUP); break; /* MSGBOX */
	case 0x0035dafd: fe_ctrl_gui (sess, FE_GUI_SHOW, 0); break; /* SHOW */
	case 0x0033155f: /* MENU */
		if (!g_ascii_strcasecmp (word[3], "TOGGLE"))
			fe_ctrl_gui (sess, FE_GUI_MENU, 0);
		else
			return false;
		break;
	default:
		return false;
	}

	return true;
}

struct help_list
{
	int longfmt;
	int i, t;
	char *buf;
};

static void
show_help_line (session *sess, help_list *hl, const char *name, const char *usage)
{
	int j, len, max;
	const char *p;

	if (name[0] == '.')	/* hidden command? */
		return;

	if (hl->longfmt)	/* long format for /HELP -l */
	{
		if (!usage || usage[0] == 0)
			PrintTextf (sess, "   \0034%s\003 :\n", name);
		else
			PrintTextf (sess, "   \0034%s\003 : %s\n", name, _(usage));
		return;
	}

	/* append the name into buffer, but convert to uppercase */
	len = strlen (hl->buf);
	p = name;
	while (*p)
	{
		hl->buf[len] = toupper ((unsigned char) *p);
		len++;
		p++;
	}
	hl->buf[len] = 0;

	hl->t++;
	if (hl->t == 5)
	{
		hl->t = 0;
		strcat (hl->buf, "\n");
		PrintText (sess, hl->buf);
		hl->buf[0] = ' ';
		hl->buf[1] = ' ';
		hl->buf[2] = 0;
	} else
	{
		/* append some spaces after the command name */
		max = strlen (name);
		if (max < 10)
		{
			max = 10 - max;
			for (j = 0; j < max; j++)
			{
				hl->buf[len] = ' ';
				len++;
				hl->buf[len] = 0;
			}
		}
	}
}

static int
cmd_help (struct session *sess, char *tbuf, char *word[], char *[])
{
	int i = 0, longfmt = 0;
	const char *helpcmd = "";

	if (tbuf)
		helpcmd = word[2];
	if (*helpcmd && strcmp (helpcmd, "-l") == 0)
		longfmt = 1;

	if (*helpcmd && !longfmt)
	{
		help (sess, tbuf, helpcmd, false);
	} else
	{
		std::string buf(4096, '\0');
		help_list hl;

		hl.longfmt = longfmt;
		hl.buf = &buf[0];

		PrintTextf (sess, "\n%s\n\n", _("Commands Available:"));
		buf[0] = ' ';
		buf[1] = ' ';
		buf[2] = 0;
		hl.t = 0;
		hl.i = 0;
		while (xc_cmds[i].name)
		{
			show_help_line (sess, &hl, xc_cmds[i].name, xc_cmds[i].help);
			i++;
		}
		strcat (&buf[0], "\n");
		PrintText (sess, buf);

		PrintTextf (sess, "\n%s\n\n", _("User defined commands:"));
		buf[0] = ' ';
		buf[1] = ' ';
		buf[2] = 0;
		hl.t = 0;
		hl.i = 0;
		for (const auto & pop : command_list)
		{
			show_help_line (sess, &hl, pop.name.c_str(), pop.cmd.c_str());
		}
		strcat (&buf[0], "\n");
		PrintText (sess, buf);

		PrintTextf (sess, "\n%s\n\n", _("Plugin defined commands:"));
		buf[0] = ' ';
		buf[1] = ' ';
		buf[2] = 0;
		hl.t = 0;
		hl.i = 0;
		plugin_command_foreach (sess, &hl, (void (*)(session*, void*, char*, char*))show_help_line);
		strcat(&buf[0], "\n");
		PrintText(sess, buf);
		PrintTextf (sess, "\n%s\n\n", _("Type /HELP <command> for more information, or /HELP -l"));
	}
	return true;
}

static int
cmd_id (struct session *sess, char *, char *word[], char *[])
{
	if (word[2][0])
	{
		sess->server->p_ns_identify (word[2]);
		return true;
	}

	return false;
}

static int
cmd_ignore (struct session *sess, char *tbuf, char *word[], char *[])
{
	int type = 0;
	int quiet = 0;
	char *mask;

	if (!*word[2])
	{
		ignore_showlist (sess);
		return true;
	}
	if (!*word[3])
		word[3] = "ALL";

	for (int i = 3;;++i)
	{
		if (!*word[i])
		{
			if (type == 0)
				return false;

			mask = word[2];
			if (strchr (mask, '?') == nullptr &&
				strchr (mask, '*') == nullptr)
			{
				mask = tbuf;
				snprintf (tbuf, TBUFSIZE, "%s!*@*", word[2]);
			}

			i = ignore_add (mask, type, true);
			if (quiet)
				return true;
			switch (i)
			{
			case 1:
				EMIT_SIGNAL (XP_TE_IGNOREADD, sess, mask, nullptr, nullptr, nullptr, 0);
				break;
			case 2:	/* old ignore changed */
				EMIT_SIGNAL (XP_TE_IGNORECHANGE, sess, mask, nullptr, nullptr, nullptr, 0);
			}
			return true;
		}
		if (!g_ascii_strcasecmp (word[i], "UNIGNORE"))
			type |= ignore::IG_UNIG;
		else if (!g_ascii_strcasecmp (word[i], "ALL"))
			type |= ignore::IG_PRIV | ignore::IG_NOTI | ignore::IG_CHAN | ignore::IG_CTCP | ignore::IG_INVI | ignore::IG_DCC;
		else if (!g_ascii_strcasecmp (word[i], "PRIV"))
			type |= ignore::IG_PRIV;
		else if (!g_ascii_strcasecmp (word[i], "NOTI"))
			type |= ignore::IG_NOTI;
		else if (!g_ascii_strcasecmp (word[i], "CHAN"))
			type |= ignore::IG_CHAN;
		else if (!g_ascii_strcasecmp (word[i], "CTCP"))
			type |= ignore::IG_CTCP;
		else if (!g_ascii_strcasecmp (word[i], "INVI"))
			type |= ignore::IG_INVI;
		else if (!g_ascii_strcasecmp (word[i], "QUIET"))
			quiet = 1;
		else if (!g_ascii_strcasecmp (word[i], "NOSAVE"))
			type |= ignore::IG_NOSAVE;
		else if (!g_ascii_strcasecmp (word[i], "DCC"))
			type |= ignore::IG_DCC;
		else
		{
			sprintf (tbuf, _("Unknown arg '%s' ignored."), word[i]);
			PrintText (sess, tbuf);
		}
	}
}

static int
cmd_invite (struct session *sess, char *, char *word[], char *[])
{
	if (!*word[2])
		return false;
	if (*word[3])
		sess->server->p_invite (word[3], word[2]);
	else
		sess->server->p_invite (sess->channel, word[2]);
	return true;
}

static int
cmd_join (struct session *sess, char *, char *word[], char *[])
{
	char *chan = word[2];
	session *sess_find;
	if (*chan)
	{
		char *po, *pass = word[3];

		sess_find = find_channel (*(sess->server), chan);
		if (!sess_find)
		{
			sess->server->p_join (chan, pass ? pass : "");
			if (sess->channel[0] == 0 && sess->waitchannel[0])
			{
				po = std::strchr (chan, ',');
				if (po)
					*po = 0;
				safe_strcpy (sess->waitchannel, chan, CHANLEN);
			}
		}
		else
			fe_ctrl_gui(sess_find, FE_GUI_FOCUS, 0);	/* bring-to-front */
		
		return true;
	}
	return false;
}

static int
cmd_kick (struct session *sess, char *, char *word[], char *word_eol[])
{
	char *nick = word[2];
	char *reason = word_eol[3];
	if (*nick)
	{
		sess->server->p_kick(sess->channel, nick, reason ? reason : std::string{});
		return true;
	}
	return false;
}

static int
cmd_kickban (struct session *sess, char *, char *word[], char *word_eol[])
{
	char *nick = word[2];
	char *reason = word_eol[3];
	struct User *user;

	if (*nick)
	{
		/* if the reason is a 1 digit number, treat it as a bantype */

		user = userlist_find (sess, nick);

		if (std::isdigit (reason[0], std::locale()) && reason[1] == 0)
		{
			ban (sess, nick, reason, (user && user->op));
			reason[0] = 0;
		} else
			ban (sess, nick, "", (user && user->op));

		sess->server->p_kick (sess->channel, nick, reason ? reason : "");

		return true;
	}
	return false;
}

static int
cmd_killall (struct session *, char *, char *[], char *[])
{
	hexchat_exit();
	return 2;
}

static int
cmd_lagcheck (struct session *, char *, char *[], char *[])
{
	lag_check ();
	return true;
}

static void
lastlog (session *sess, char *search, gtk_xtext_search_flags flags)
{
	session *lastlog_sess;

	if (!is_session (sess))
		return;
	if (!sess->server)
		throw std::runtime_error("Invalid Server reference");

	lastlog_sess = find_dialog (*(sess->server), "(lastlog)");
	if (!lastlog_sess)
		lastlog_sess = new_ircwindow(sess->server, "(lastlog)", session::SESS_DIALOG, false);

	lastlog_sess->lastlog_sess = sess;
	lastlog_sess->lastlog_flags = flags;

	fe_text_clear (lastlog_sess, 0);
	fe_lastlog (sess, lastlog_sess, search, flags);
}

static int
cmd_lastlog (struct session *sess, char *, char *[], char *word_eol[])
{
	int j = 2;
	gtk_xtext_search_flags flags = static_cast<gtk_xtext_search_flags>(0);
	bool doublehyphen = false;

	while (word_eol[j] != nullptr && word_eol [j][0] == '-' && !doublehyphen)
	{
		switch (word_eol [j][1])
		{
			case 'r':
				flags |= regexp;
				break;
			case 'm':
				flags |= case_match;
				break;
			case 'h':
				flags |= highlight;
				break;
			case '-':
				doublehyphen = true;
				break;
			default:
				break;
				/* O dear whatever shall we do here? */
		}
		j++;
	}
	if (word_eol[j] != nullptr && *word_eol[j])
	{
		lastlog (sess, word_eol[j], flags);
		return true;
	}
	else
	{	
		return false;
	}
}

static int
cmd_list (struct session *sess, char *, char *[], char *word_eol[])
{
	fe_open_chan_list (sess->server, word_eol[2] ? word_eol[2] : "", true);

	return true;
}
}// end anonymous namespace

gboolean
load_perform_file (session *sess, const char file[])
{
	char tbuf[1024 + 4];
	char *nl;
	FILE *fp;

	fp = hexchat_fopen_file (file, "r", 0);		/* load files from config dir */
	if (!fp)
		return false;

	tbuf[1024] = 0;
	while (fgets (tbuf, 1024, fp))
	{
		nl = strchr (tbuf, '\n');
		if (nl == tbuf) /* skip empty commands */
			continue;
		if (nl)
			*nl = 0;
		if (tbuf[0] == prefs.hex_input_command_char[0])
			handle_command (sess, tbuf + 1, true);
		else
			handle_command (sess, tbuf, true);
	}
	fclose (fp);
	return true;
}

static int
cmd_load (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	namespace bfs = boost::filesystem;

	if (!word[2][0])
		return false;

	if (strcmp (word[2], "-e") == 0)
	{
		glib_string file{ expand_homedir(word[3]) };
		if (!load_perform_file (sess, file.get()))
		{
			auto path = bfs::path(config::config_dir()) / file.get();
			PrintTextf (sess, boost::format(_("Cannot access %s\n")) % path);
			PrintText (sess, errorstring (errno));
		}
		return true;
	}

#ifdef USE_PLUGIN
	if (g_str_has_suffix (word[2], "." G_MODULE_SUFFIX))
	{
		char* arg = nullptr;
		if (word_eol[3][0])
			arg = word_eol[3];

		glib_string file(expand_homedir (word[2]));
		auto error = plugin_load (sess, file.get(), arg);

		if (error)
			PrintText (sess, error);

		return true;
	}

	sprintf (tbuf, "Unknown file type %s. Maybe you need to install the Perl or Python plugin?\n", word[2]);
	PrintText (sess, tbuf);
#endif

	return false;
}

char *
split_up_text(struct session *sess, char *text, int cmd_length, char *split_text)
{
	size_t space_offset;
	char *space;

	/* maximum allowed text */
	/* :nickname!username@host.com cmd_length */
	size_t max = 512; /* rfc 2812 */
	max -= 3; /* :, !, @ */
	max -= cmd_length;
	max -= std::strlen (sess->server->nick);
	max -= std::strlen (sess->channel);
	if (sess->me && sess->me->hostname)
		max -= sess->me->hostname->size();
	else
	{
		max -= 9;	/* username */
		max -= 65;	/* max possible hostname and '@' */
	}

	if (strlen (text) > max)
	{
		unsigned int i = 0;
		int size;

		/* traverse the utf8 string and find the nearest cut point that
			doesn't split 1 char in half */
		for (;;)
		{
			size = g_utf8_skip[((unsigned char *)text)[i]];
			if ((i + size) >= max)
				break;
			i += size;
		}
		max = i;

		/* Try splitting at last space */
		space = g_utf8_strrchr (text, max, ' ');
		if (space)
		{
			space_offset = g_utf8_pointer_to_offset (text, space);

			/* Only split if last word is of sane length */
			if (max != space_offset && max - space_offset < 20)
				max = space_offset + 1;
		}

		split_text = g_strdup_printf ("%.*s", max, text);

		return split_text;
	}

	return nullptr;
}

static int
cmd_me (struct session *sess, char *tbuf, char *[], char *word_eol[])
{
	char *act = word_eol[2];
	char *split_text = nullptr;
	int cmd_length = 22; /* " PRIVMSG ", " ", :, \001ACTION, " ", \001, \r, \n */
	int offset = 0;
	message_tags_data no_tags = message_tags_data();

	if (!(*act))
		return false;

	if (sess->type == session::SESS_SERVER)
	{
		notj_msg (sess);
		return true;
	}
	
	snprintf (tbuf, TBUFSIZE, "\001ACTION %s\001\r", act);
	/* first try through DCC CHAT */
	if (dcc::dcc_write_chat (sess->channel, tbuf))
	{
		/* print it to screen */
		inbound_action (sess, sess->channel, sess->server->nick, "", act, true, false,
							 &no_tags);
	} else
	{
		/* DCC CHAT failed, try through server */
		if (sess->server->connected)
		{
			while ((split_text = split_up_text (sess, act + offset, cmd_length, split_text)))
			{
				sess->server->p_action (sess->channel, split_text);
				/* print it to screen */
				inbound_action (sess, sess->channel, sess->server->nick, "",
									 split_text, true, false,
									 &no_tags);

				if (*split_text)
					offset += strlen(split_text);

				g_free(split_text);
			}

			sess->server->p_action (sess->channel, act + offset);
			/* print it to screen */
			inbound_action (sess, sess->channel, sess->server->nick, "",
								 act + offset, true, false, &no_tags);
		} else
		{
			notc_msg (sess);
		}
	}

	return true;
}

static int
cmd_mode (struct session *sess, char *, char *word[], char *word_eol[])
{
	/* +channel channels are dying, let those servers whine about modes.
	 * return info about current channel if available and no info is given */
	if ((*word[2] == '+') || (*word[2] == 0) || (!sess->server->is_channel_name(word[2]) &&
				!(rfc_casecmp(sess->server->nick, word[2]) == 0)))
	{
		if(sess->channel[0] == 0)
			return false;
		sess->server->p_mode (sess->channel, word_eol[2]);
	}
	else
		sess->server->p_mode (word[2], word_eol[3]);
	return true;
}

static int
cmd_mop (struct session *sess, char *, char *[], char *[])
{
	std::vector<std::string> nicks;
	for (auto & user : sess->usertree)
	{
		if (!user->op)
		{
			nicks.emplace_back(user->nick);
		}
	}

	send_channel_modes (sess, nicks, 0, nicks.size(), '+', 'o', 0);

	return true;
}

static int
cmd_msg (struct session *sess, char *, char *word[], char *word_eol[])
{
	char *nick = word[2];
	char *msg = word_eol[3];
	struct session *newsess;
	char *split_text = nullptr;
	int cmd_length = 13; /* " PRIVMSG ", " ", :, \r, \n */
	int offset = 0;

	if (*nick)
	{
		if (*msg)
		{
			if (strcmp (nick, ".") == 0)
			{							  /* /msg the last nick /msg'ed */
				if (sess->lastnick[0])
					nick = sess->lastnick;
			} else
			{
				safe_strcpy (sess->lastnick, nick, NICKLEN);	/* prime the last nick memory */
			}

			if (*nick == '=')
			{
				nick++;
				if (!dcc::dcc_write_chat (nick, msg))
				{
					EMIT_SIGNAL (XP_TE_NODCC, sess, nullptr, nullptr, nullptr, nullptr, 0);
					return true;
				}
			} else
			{
				if (!sess->server->connected)
				{
					notc_msg (sess);
					return true;
				}

				while ((split_text = split_up_text (sess, msg + offset, cmd_length, split_text)))
				{
					sess->server->p_message (nick, split_text);

					if (*split_text)
						offset += strlen(split_text);

					g_free(split_text);
				}
				sess->server->p_message (nick, msg + offset);
				offset = 0;
			}
			newsess = find_dialog (*(sess->server), nick);
			if (!newsess)
				newsess = find_channel (*(sess->server), nick);
			if (newsess)
			{
				message_tags_data no_tags = message_tags_data();

				while ((split_text = split_up_text (sess, msg + offset, cmd_length, split_text)))
				{
					inbound_chanmsg (*(newsess->server), nullptr, newsess->channel,
										  newsess->server->nick, split_text, true, false,
										  &no_tags);

					if (*split_text)
						offset += strlen(split_text);

					g_free(split_text);
				}
				inbound_chanmsg (*(newsess->server), nullptr, newsess->channel,
									  newsess->server->nick, msg + offset, true, false,
									  &no_tags);
			}
			else
			{
				/* mask out passwords */
				if (g_ascii_strcasecmp (nick, "nickserv") == 0 &&
					 g_ascii_strncasecmp (msg, "identify ", 9) == 0)
					msg = "identify ****";
				EMIT_SIGNAL (XP_TE_MSGSEND, sess, nick, msg, nullptr, nullptr, 0);
			}

			return true;
		}
	}
	return false;
}

static int
cmd_names (struct session *sess, char *, char *word[], char *[])
{
	if (*word[2])
		sess->server->p_names (word[2]);
	else
		sess->server->p_names (sess->channel);
	return true;
}

static int
cmd_nctcp (struct session *sess, char *, char *word[], char *word_eol[])
{
	if (*word_eol[3])
	{
		sess->server->p_nctcp (word[2], word_eol[3]);
		return true;
	}
	return false;
}

static int
cmd_newserver (struct session *sess, char *tbuf, char *word[],
					char *word_eol[])
{
	if (strcmp (word[2], "-noconnect") == 0)
	{
		new_ircwindow(nullptr, word[3], session::SESS_SERVER, false);
		return true;
	}
	
	sess = new_ircwindow(nullptr, nullptr, session::SESS_SERVER, true);
	cmd_server (sess, tbuf, word, word_eol);
	return true;
}

static int
cmd_nick (struct session *sess, char *, char *word[], char *[])
{
	char *nick = word[2];
	if (*nick)
	{
		if (sess->server->connected)
			sess->server->p_change_nick (nick);
		else
		{
			message_tags_data no_tags = message_tags_data();
			inbound_newnick (*(sess->server), sess->server->nick, nick, true,
								  &no_tags);
		}
		return true;
	}
	return false;
}

static int
cmd_notice (struct session *sess, char *, char *word[], char *word_eol[])
{
	char *text = word_eol[3];
	char *split_text = nullptr;
	int cmd_length = 12; /* " NOTICE ", " ", :, \r, \n */
	int offset = 0;

	if (*word[2] && *word_eol[3])
	{
		while ((split_text = split_up_text (sess, text + offset, cmd_length, split_text)))
		{
			sess->server->p_notice (word[2], split_text);
			EMIT_SIGNAL (XP_TE_NOTICESEND, sess, word[2], split_text, nullptr, nullptr, 0);
			
			if (*split_text)
				offset += strlen(split_text);
			
			g_free(split_text);
		}

		sess->server->p_notice (word[2], text + offset);
		EMIT_SIGNAL (XP_TE_NOTICESEND, sess, word[2], text + offset, nullptr, nullptr, 0);

		return true;
	}
	return false;
}

static int
cmd_notify (struct session *sess, char *, char *word[], char *[])
{
	int i = 1;
	char *net = nullptr;

	if (*word[2])
	{
		if (strcmp (word[2], "-n") == 0)	/* comma sep network list */
		{
			net = word[3];
			i += 2;
		}

		for (;;)
		{
			i++;
			if (!*word[i])
				break;
			if (notify_deluser (word[i]))
			{
				EMIT_SIGNAL (XP_TE_DELNOTIFY, sess, word[i], nullptr, nullptr, nullptr, 0);
				return true;
			}

			if (net && strcmp (net, "ASK") == 0)
				::hexchat::fe::notify::fe_notify_ask (word[i], nullptr);
			else
			{
				notify_adduser (word[i], net);
				EMIT_SIGNAL (XP_TE_ADDNOTIFY, sess, word[i], nullptr, nullptr, nullptr, 0);
			}
		}
	} else
	{
		message_tags_data no_tags = message_tags_data();
		notify_showlist (sess, &no_tags);
	}
	return true;
}

static int
cmd_op (struct session *sess, char *, char *word[], char *[])
{
	int i = 2;
	auto  words = to_vector_strings(word, PDIWORDS + 1);
	for (;;)
	{
		if (!*word[i])
		{
			if (i == 2)
				return false;
			send_channel_modes (sess, words, 2, i, '+', 'o', 0);
			return true;
		}
		i++;
	}
}

static int
cmd_part (struct session *sess, char *, char *word[], char *word_eol[])
{
	char *chan = word[2];
	char *reason = word_eol[3];
	if (!*chan)
		chan = sess->channel;
	if ((*chan) && sess->server->is_channel_name(chan))
	{
		if (reason[0] == 0)
			reason = nullptr;
		server_sendpart (*(sess->server), chan, reason ? boost::make_optional<const std::string&>(std::string(reason)) : boost::none);
		return true;
	}
	return false;
}

static int
cmd_ping (struct session *sess, char *, char *word[], char *[])
{
	char timestring[64];
	unsigned long tim;
	char *to = word[2];

	tim = make_ping_time ();

	snprintf (timestring, sizeof (timestring), "%lu", tim);
	sess->server->p_ping (to, timestring);

	return true;
}

session *
open_query (server &serv, const char nick[], bool focus_existing)
{
	session *sess;

	sess = find_dialog (serv, nick);
	if (!sess)
		sess = new_ircwindow(&serv, nick, session::SESS_DIALOG, focus_existing);
	else if (focus_existing)
		fe_ctrl_gui(sess, FE_GUI_FOCUS, 0);	/* bring-to-front */

	return sess;
}

static int
cmd_query (struct session *sess, char *, char *word[], char *word_eol[])
{
	char *nick = word[2];
	char *msg = word_eol[3];
	char *split_text = nullptr;
	bool focus = true;
	int cmd_length = 13; /* " PRIVMSG ", " ", :, \r, \n */
	int offset = 0;

	if (strcmp (word[2], "-nofocus") == 0)
	{
		nick = word[3];
		msg = word_eol[4];
		focus = false;
	}

	if (*nick && !sess->server->is_channel_name(nick))
	{
		struct session *nick_sess;

		nick_sess = open_query (*sess->server, nick, focus);

		if (*msg)
		{
			message_tags_data no_tags = message_tags_data();

			if (!sess->server->connected)
			{
				notc_msg (sess);
				return true;
			}

			while ((split_text = split_up_text (sess, msg + offset, cmd_length, split_text)))
			{
				sess->server->p_message (nick, split_text);
				inbound_chanmsg (*nick_sess->server, nick_sess, nick_sess->channel,
								 nick_sess->server->nick, split_text, true, false,
								 &no_tags);

				if (*split_text)
					offset += strlen(split_text);

				g_free(split_text);
			}
			sess->server->p_message (nick, msg + offset);
			inbound_chanmsg (*nick_sess->server, nick_sess, nick_sess->channel,
							 nick_sess->server->nick, msg + offset, true, false,
							 &no_tags);
		}

		return true;
	}
	return false;
}

static int
cmd_quiet (struct session *sess, char *, char *word[], char *[])
{
	server *serv = sess->server;

	if (serv->chanmodes.find_first_of('q') == std::string::npos)
	{
		PrintText (sess, _("Quiet is not supported by this server."));
		return true;
	}

	if (*word[2])
	{
		std::string quietmask = create_mask (sess, word[2], "+q", word[3], 0);
		serv->p_mode(sess->channel, quietmask);
	}
	else
	{
		serv->p_mode (sess->channel, "+q");	/* quietlist */
	}

	return true;
}

static int
cmd_unquiet (struct session *sess, char *, char *word[], char *[])
{
	/* Allow more than one mask in /unban -- tvk */
	
	if (sess->server->chanmodes.find_first_of('q') == std::string::npos)
	{
		PrintText (sess, _("Quiet is not supported by this server."));
		return true;
	}
	auto  words = to_vector_strings(word, PDIWORDS + 1);
	for (int i = 2;; ++i)
	{
		if (!*word[i])
		{
			if (i == 2)
				return false;
			send_channel_modes (sess, words, 2, i, '-', 'q', 0);
			return true;
		}
	}
}

static int
cmd_quote (struct session *sess, char *, char *[], char *word_eol[])
{
	char *raw = word_eol[2];

	return sess->server->p_raw (raw);
}

static int
cmd_reconnect (struct session *sess, char *, char *word[], char *[])
{
	int tmp = prefs.hex_net_reconnect_delay;
	server *serv = sess->server;

	prefs.hex_net_reconnect_delay = 0;

	if (!g_ascii_strcasecmp (word[2], "ALL"))
	{
		for (auto list = serv_list; list; list = g_slist_next(list))
		{
			serv = static_cast<server*>(list->data);
			if (serv->connected)
				serv->auto_reconnect (true, -1);
		}
	}
	/* If it isn't "ALL" and there is something
	there it *should* be a server they are trying to connect to*/
	else if (*word[2])
	{
		int offset = 0;
#ifdef USE_OPENSSL
		bool use_ssl = false;

		if (strcmp (word[2], "-ssl") == 0)
		{
			use_ssl = true;
			offset++;	/* args move up by 1 word */
		}
		serv->use_ssl = use_ssl;
		serv->accept_invalid_cert = true;
#endif

		if (*word[4+offset])
			safe_strcpy (serv->password, word[4+offset], sizeof (serv->password));
		if (*word[3+offset])
			serv->port = atoi (word[3+offset]);
		safe_strcpy (serv->hostname, word[2+offset], sizeof (serv->hostname));
		serv->auto_reconnect (true, -1);
	}
	else
	{
		serv->auto_reconnect (true, -1);
	}
	prefs.hex_net_reconnect_delay = tmp;

	return true;
}

static int
cmd_recv (struct session *sess, char *, char *[], char *word_eol[])
{
	if (*word_eol[2])
	{
		sess->server->p_inline (word_eol[2]);
		return true;
	}

	return false;
}

static int
cmd_say (struct session *sess, char *, char *[], char *word_eol[])
{
	char *speech = word_eol[2];
	if (*speech)
	{
		handle_say (sess, speech, false);
		return true;
	}
	return false;
}

static int
cmd_send (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	if (!word[2][0])
		return false;

	struct sockaddr_in SAddr = { 0 };

	auto addr = dcc::dcc_get_my_address ();
	if (addr == 0)
	{
		/* use the one from our connected server socket */
		socklen_t len = sizeof (SAddr);
		getsockname (sess->server->sok, (struct sockaddr *) &SAddr, &len);
		addr = SAddr.sin_addr.s_addr;
	}
	addr = ntohl (addr);

	if ((addr & 0xffff0000) == 0xc0a80000 ||	/* 192.168.x.x */
		 (addr & 0xff000000) == 0x0a000000)		/* 10.x.x.x */
		/* we got a private net address, let's PSEND or it'll fail */
		snprintf (tbuf, 512, "DCC PSEND %s", word_eol[2]);
	else
		snprintf (tbuf, 512, "DCC SEND %s", word_eol[2]);

	handle_command (sess, tbuf, false);

	return true;
}

static int
cmd_setcursor (struct session *sess, char *, char *word[], char *[])
{
	int delta = false;

	if (*word[2])
	{
		if (word[2][0] == '-' || word[2][0] == '+')
			delta = true;
		fe_set_inputbox_cursor (sess, delta, atoi (word[2]));
		return true;
	}

	return false;
}

static int
cmd_settab (struct session *sess, char *, char *[], char *word_eol[])
{
	if (*word_eol[2])
	{
		char channel_name_backup[CHANLEN];
		strcpy(channel_name_backup, sess->channel);
		safe_strcpy (sess->channel, word_eol[2], CHANLEN);
		fe_set_channel (sess);
		strcpy(sess->channel, channel_name_backup);
	}

	return true;
}

static int
cmd_settext (struct session *sess, char *, char *[], char *word_eol[])
{
	fe_set_inputbox_contents (sess, word_eol[2]);
	return true;
}

static int
cmd_splay (struct session *, char *, char *word[], char *[])
{
	if (*word[2])
	{
		sound_play (word[2], false);
		return true;
	}

	return false;
}

static bool
parse_irc_url (char *url, char *server_name[], char *port[], char *channel[], char *key[], bool &use_ssl)
{
	char *co;
#ifdef USE_OPENSSL
	if (g_ascii_strncasecmp ("ircs://", url, 7) == 0)
	{
		use_ssl = true;
		*server_name = url + 7;
		goto urlserv;
	}
#endif

	if (g_ascii_strncasecmp ("irc://", url, 6) == 0)
	{
		*server_name = url + 6;
#ifdef USE_OPENSSL
urlserv:
#endif
		/* check for port */
		co = strchr (*server_name, ':');
		if (co)
		{
			*port = co + 1;
			*co = 0;
		} else
			co = *server_name;
		/* check for channel - mirc style */
		co = strchr (co + 1, '/');
		if (co)
		{
			*co = 0;
			co++;
			if (*co == '#')
				*channel = co+1;
			else if (*co != '\0')
				*channel = co;
				
			/* check for key - mirc style */
			co = strchr (co + 1, '?');
			if (co)
			{
				*co = 0;
				co++;
				*key = co;
			}	
		}
			
		return true;
	}
	return false;
}

static int
cmd_server (struct session *sess, char *, char *word[], char *word_eol[])
{
	int offset = 0;
	char *server_name = nullptr;
	char *port = nullptr;
	char *pass = nullptr;
	char *channel = nullptr;
	char *key = nullptr;
	bool use_ssl = false;
	bool is_url = true;
	server *serv = sess->server;
	ircnet *net = nullptr;

#ifdef USE_OPENSSL
	/* BitchX uses -ssl, mIRC uses -e, let's support both */
	if (std::strcmp (word[2], "-ssl") == 0 || std::strcmp (word[2], "-e") == 0)
	{
		use_ssl = true;
		offset++;	/* args move up by 1 word */
	}
#endif

	if (!parse_irc_url (word[2 + offset], &server_name, &port, &channel, &key, use_ssl))
	{
		is_url = false;
		server_name = word[2 + offset];
	}
	if (port)
		pass = word[3 + offset];
	else
	{
		port = word[3 + offset];
		pass = word[4 + offset];
	}
	
	if (!(*server_name))
		return false;

	sess->server->network = nullptr;

	/* dont clear it for /servchan */
	if (g_ascii_strncasecmp (word_eol[1], "SERVCHAN ", 9))
		sess->willjoinchannel[0] = 0;

	if (channel)
	{
		sess->willjoinchannel[0] = '#';
		safe_strcpy ((sess->willjoinchannel + 1), channel, (CHANLEN - 1));
		if (key)
			safe_strcpy (sess->channelkey, key);
	}

	/* support +7000 style ports like mIRC */
	if (port[0] == '+')
	{
		port++;
#ifdef USE_OPENSSL
		use_ssl = true;
#endif
	}

	if (*pass)
	{
		safe_strcpy (serv->password, pass, sizeof (serv->password));
		serv->loginmethod = LOGIN_PASS;
	}
	else
	{
		/* If part of a known network, login like normal */
		net = servlist_net_find_from_server (server_name);
		if (net && net->pass && *net->pass)
		{
			safe_strcpy (serv->password, net->pass, sizeof (serv->password));
			serv->loginmethod = net->logintype;
		}
		else /* Otherwise ensure no password is sent */
		{
			serv->password[0] = 0;
		}
	}

#ifdef USE_OPENSSL
	serv->use_ssl = use_ssl;
	serv->accept_invalid_cert = true;
#endif

	/* try to connect by Network name */
	if (servlist_connect_by_netname (sess, server_name, !is_url))
		return true;

	if (*port)
	{
		serv->connect (server_name, atoi (port), false);
	} else
	{
		/* -1 for default port */
		serv->connect (server_name, -1, false);
	}

	/* try to associate this connection with a listed network */
	if (!serv->network)
		/* search for this hostname in the entire server list */
		serv->network = servlist_net_find_from_server (server_name);
		/* may return nullptr, but that's OK */

	return true;
}

static int
cmd_servchan (struct session *sess, char *tbuf, char *word[],
				  char *word_eol[])
{
	int offset = 0;

#ifdef USE_OPENSSL
	if (strcmp (word[2], "-ssl") == 0)
		offset++;
#endif

	if (*word[4 + offset])
	{
		safe_strcpy (sess->willjoinchannel, word[4 + offset], CHANLEN);
		return cmd_server (sess, tbuf, word, word_eol);
	}

	return false;
}

static int
cmd_topic (struct session *sess, char *, char *word[], char *word_eol[])
{
	if (word[2][0] && sess->server->is_channel_name(word[2]))
		sess->server->p_topic (word[2], word_eol[3]);
	else
		sess->server->p_topic (sess->channel, word_eol[2]);
	return true;
}

static int
cmd_tray (struct session *, char *, char *word[], char *[])
{
	if (std::strcmp (word[2], "-b") == 0)
	{
		fe_tray_set_balloon (word[3], word[4][0] ? word[4] : nullptr);
		return true;
	}

	if (std::strcmp (word[2], "-t") == 0)
	{
		fe_tray_set_tooltip (word[3][0] ? word[3] : nullptr);
		return true;
	}

	if (std::strcmp (word[2], "-i") == 0)
	{
		fe_tray_set_icon (static_cast<feicon>(std::atoi (word[3])));
		return true;
	}

	if (std::strcmp (word[2], "-f") != 0)
		return false;

	if (!word[3][0])
	{
		fe_tray_set_file (nullptr);	/* default HexChat icon */
		return true;
	}

	if (!word[4][0])
	{
		fe_tray_set_file (word[3]);	/* fixed custom icon */
		return true;
	}

	/* flash between 2 icons */
	fe_tray_set_flash (word[4], word[5][0] ? word[5] : nullptr, std::atoi (word[3]));
	return true;
}

static int
cmd_unignore (struct session *sess, char *tbuf, char *word[],
				  char *[])
{
	char *mask = word[2];
	char *arg = word[3];
	if (*mask)
	{
		if (strchr (mask, '?') == nullptr && strchr (mask, '*') == nullptr)
		{
			mask = tbuf;
			snprintf (tbuf, TBUFSIZE, "%s!*@*", word[2]);
		}
		
		if (ignore_del (mask))
		{
			if (g_ascii_strcasecmp (arg, "QUIET"))
				EMIT_SIGNAL (XP_TE_IGNOREREMOVE, sess, mask, nullptr, nullptr, nullptr, 0);
		}
		return true;
	}
	return false;
}

static int
cmd_unload (struct session *sess, char *, char *word[], char *[])
{
#ifdef USE_PLUGIN
	bool by_file = false;

	if (g_str_has_suffix (word[2], "." G_MODULE_SUFFIX))
		by_file = true;

	switch (plugin_kill (word[2], by_file))
	{
	case 0:
			PrintText (sess, _("No such plugin found.\n"));
			break;
	case 1:
			return true;
	case 2:
			PrintText (sess, _("That plugin is refusing to unload.\n"));
			break;
	}
#endif

	return false;
}

static int
cmd_reload (struct session *sess, char *, char *word[], char *[])
{
#ifdef USE_PLUGIN
	bool by_file = false;

	if (g_str_has_suffix (word[2], "." G_MODULE_SUFFIX))
		by_file = true;

	switch (plugin_reload (sess, word[2], by_file))
	{
	case 0: /* error */
			PrintText (sess, _("No such plugin found.\n"));
			break;
	case 1: /* success */
			return true;
	case 2: /* fake plugin, we know it exists but scripts should handle it. */
			return true;
	}
#endif

	return false;
}

static server *
find_server_from_hostname (const char hostname[])
{
	for (auto list = serv_list; list; list = g_slist_next(list))
	{
		auto serv = static_cast<server*>(list->data);
		if (!g_ascii_strcasecmp (hostname, serv->hostname) && serv->connected)
			return serv;
	}

	return nullptr;
}

static server *
find_server_from_net (void *net)
{
	for (auto list = serv_list; list; list = g_slist_next(list))
	{
		auto serv = static_cast<server*>(list->data);
		if (serv->network == net && serv->connected)
			return serv;
	}

	return nullptr;
}

static void
url_join_only (server *serv, char *, const char channel[], const char key[])
{
	/* already connected, JOIN only. */
	if (!channel)
		return;
	std::string channel_name;
	channel_name.push_back('#');
	channel_name.append(channel, std::min(size_t(256), std::strlen(channel)));
	if (key)
		serv->p_join (channel_name, key);
	else
		serv->p_join(channel_name, std::string{});
}

static int
cmd_url (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	if (!word[2][0])
	{
		return false;
	}

	char *server_name = nullptr;
	char *port = nullptr;
	char *channel = nullptr;
	char *key = nullptr;
	glib_string url{ g_strdup(word[2]) };
	bool use_ssl = false;
	void *net;
	server *serv;

	if (parse_irc_url(url.get(), &server_name, &port, &channel, &key, use_ssl))
	{
		/* maybe we're already connected to this net */

		/* check for "FreeNode" */
		net = servlist_net_find(server_name, nullptr, g_ascii_strcasecmp);
		/* check for "irc.eu.freenode.net" */
		if (!net)
			net = servlist_net_find_from_server(server_name);

		if (net)
		{
			/* found the network, but are we connected? */
			serv = find_server_from_net(net);
			if (serv)
			{
				url_join_only(serv, tbuf, channel, key);
				return true;
			}
		}
		else
		{
			/* an un-listed connection */
			serv = find_server_from_hostname(server_name);
			if (serv)
			{
				url_join_only(serv, tbuf, channel, key);
				return true;
			}
		}

		/* not connected to this net, open new window */
		cmd_newserver(sess, tbuf, word, word_eol);

	}
	else
		fe_open_url(word[2]);
	return true;
}

static int
cmd_uselect (struct session *sess, char *, char *word[], char *[])
{
	int idx = 2;
	int clear = true;
	int scroll = false;

	if (strcmp (word[2], "-a") == 0)	/* ADD (don't clear selections) */
	{
		clear = false;
		idx++;
	}
	if (strcmp (word[idx], "-s") == 0)	/* SCROLL TO */
	{
		scroll = true;
		idx++;
	}
	/* always valid, no args means clear the selection list */
	fe_uselect (sess, word + idx, clear, scroll);
	return true;
}

static int
cmd_userlist (struct session *sess, char *, char *[],
				  char *[])
{
	for (auto & user : sess->usertree)
	{
		time_t lt;

		if (!user->lasttalk)
			lt = 0;
		else
			lt = time(0) - user->lasttalk;
		PrintTextf(sess,
			boost::format("\00306%s\t\00314[\00310%-38s\00314] \017ov\0033=\017%d%d away=%u lt\0033=\017%ld\n") %
			user->nick % (user->hostname ? user->hostname->c_str() : "") % user->op % user->voice % user->away % (long)lt);
	}
	return true;
}

static int
cmd_wallchop (struct session *sess, char *, char *[],
				  char *word_eol[])
{
	if (!(*word_eol[2]))
		return false;

	const char * reason = word_eol[2];
	int i = 0;
	std::ostringstream outbuf("NOTICE ", std::ios::ate);
	for (auto & user : sess->usertree)
	{
		if (user->op)
		{
			if (i)
				outbuf << ',';
			outbuf << user->nick;
			i++;
		}
		if (i == 5)
		{
			i = 0;
			outbuf << boost::format(" :[@%s] %s") % sess->channel % reason;
			sess->server->p_raw(outbuf.str());
			outbuf.str("");
			outbuf.clear();
			outbuf << "NOTICE ";
		}
	}

	if (i)
	{
		outbuf << boost::format(" :[@%s] %s") % sess->channel % word_eol[2];
		sess->server->p_raw (outbuf.str());
	}

	return true;
}

static int
cmd_wallchan (struct session *sess, char *, char *[],
				  char *word_eol[])
{
	GSList *list;

	if (*word_eol[2])
	{
		list = sess_list;
		while (list)
		{
			sess = static_cast<session*>(list->data);
			if (sess->type == session::SESS_CHANNEL)
			{
				message_tags_data no_tags = message_tags_data();

				inbound_chanmsg (*sess->server, nullptr, sess->channel,
									  sess->server->nick, word_eol[2], true, false, 
									  &no_tags);
				sess->server->p_message (sess->channel, word_eol[2]);
			}
			list = list->next;
		}
		return true;
	}
	return false;
}

static int
cmd_hop (struct session *sess, char *, char *word[], char *[])
{
	auto  words = to_vector_strings(word, PDIWORDS + 1);
	for (int i = 2;; ++i)
	{
		if (!*word[i])
		{
			if (i == 2)
				return false;
			send_channel_modes (sess, words, 2, i, '+', 'h', 0);
			return true;
		}
	}
}

static int
cmd_voice (struct session *sess, char *, char *word[], char *[])
{
	auto words = to_vector_strings(word, PDIWORDS + 1);
	for (int i = 2;; ++i)
	{
		if (!*word[i])
		{
			if (i == 2)
				return false;
			send_channel_modes (sess, words, 2, i, '+', 'v', 0);
			return true;
		}
	}
}

/* *MUST* be kept perfectly sorted for the bsearch to work */
const struct commands xc_cmds[] = {
	{"ADDBUTTON", cmd_addbutton, 0, 0, 1,
	 N_("ADDBUTTON <name> <action>, adds a button under the user-list")},
	{"ADDSERVER", cmd_addserver, 0, 0, 1, N_("ADDSERVER <NewNetwork> <newserver/6667>, adds a new network with a new server to the network list")},
	{"ALLCHAN", cmd_allchannels, 0, 0, 1,
	 N_("ALLCHAN <cmd>, sends a command to all channels you're in")},
	{"ALLCHANL", cmd_allchannelslocal, 0, 0, 1,
	 N_("ALLCHANL <cmd>, sends a command to all channels on the current server")},
	{"ALLSERV", cmd_allservers, 0, 0, 1,
	 N_("ALLSERV <cmd>, sends a command to all servers you're in")},
	{"AWAY", cmd_away, 1, 0, 1, N_("AWAY [<reason>], sets you away")},
	{"BACK", cmd_back, 1, 0, 1, N_("BACK, sets you back (not away)")},
	{"BAN", cmd_ban, 1, 1, 1,
	 N_("BAN <mask> [<bantype>], bans everyone matching the mask from the current channel. If they are already on the channel this doesn't kick them (needs chanop)")},
	{"CHANOPT", cmd_chanopt, 0, 0, 1, N_("CHANOPT [-quiet] <variable> [<value>]")},
	{"CHARSET", cmd_charset, 0, 0, 1, N_("CHARSET [<encoding>], get or set the encoding used for the current connection")},
	{"CLEAR", cmd_clear, 0, 0, 1, N_("CLEAR [ALL|HISTORY|[-]<amount>], Clears the current text window or command history")},
	{"CLOSE", cmd_close, 0, 0, 1, N_("CLOSE [-m], Closes the current window/tab or all queries")},

	{"COUNTRY", cmd_country, 0, 0, 1,
	 N_("COUNTRY [-s] <code|wildcard>, finds a country code, eg: au = australia")},
	{"CTCP", cmd_ctcp, 1, 0, 1,
	 N_("CTCP <nick> <message>, send the CTCP message to nick, common messages are VERSION and USERINFO")},
	{"CYCLE", cmd_cycle, 1, 1, 1,
	 N_("CYCLE [<channel>], parts the current or given channel and immediately rejoins")},
	{"DCC", cmd_dcc, 0, 0, 1,
	 N_("\n"
	 "DCC GET <nick>                      - accept an offered file\n"
	 "DCC SEND [-maxcps=#] <nick> [file]  - send a file to someone\n"
	 "DCC PSEND [-maxcps=#] <nick> [file] - send a file using passive mode\n"
	 "DCC LIST                            - show DCC list\n"
	 "DCC CHAT <nick>                     - offer DCC CHAT to someone\n"
	 "DCC PCHAT <nick>                    - offer DCC CHAT using passive mode\n"
	 "DCC CLOSE <type> <nick> <file>         example:\n"
	 "         /dcc close send johnsmith file.tar.gz")},
	{"DEBUG", cmd_debug, 0, 0, 1, 0},

	{"DEHOP", cmd_dehop, 1, 1, 1,
	 N_("DEHOP <nick>, removes chanhalf-op status from the nick on the current channel (needs chanop)")},
	{"DELBUTTON", cmd_delbutton, 0, 0, 1,
	 N_("DELBUTTON <name>, deletes a button from under the user-list")},
	{"DEOP", cmd_deop, 1, 1, 1,
	 N_("DEOP <nick>, removes chanop status from the nick on the current channel (needs chanop)")},
	{"DEVOICE", cmd_devoice, 1, 1, 1,
	 N_("DEVOICE <nick>, removes voice status from the nick on the current channel (needs chanop)")},
	{"DISCON", cmd_discon, 0, 0, 1, N_("DISCON, Disconnects from server")},
	{"DNS", cmd_dns, 0, 0, 1, N_("DNS <nick|host|ip>, Resolves an IP or hostname")},
	{"ECHO", cmd_echo, 0, 0, 1, N_("ECHO <text>, Prints text locally")},
#ifndef WIN32
	{"EXEC", cmd_exec, 0, 0, 1,
	 N_("EXEC [-o] <command>, runs the command. If -o flag is used then output is sent to current channel, else is printed to current text box")},
#ifndef __EMX__
	{"EXECCONT", cmd_execc, 0, 0, 1, N_("EXECCONT, sends the process SIGCONT")},
#endif
	{"EXECKILL", cmd_execk, 0, 0, 1,
	 N_("EXECKILL [-9], kills a running exec in the current session. If -9 is given the process is SIGKILL'ed")},
#ifndef __EMX__
	{"EXECSTOP", cmd_execs, 0, 0, 1, N_("EXECSTOP, sends the process SIGSTOP")},
	{"EXECWRITE", cmd_execw, 0, 0, 1, N_("EXECWRITE, sends data to the processes stdin")},
#endif
#endif
#if 0
	{"EXPORTCONF", cmd_exportconf, 0, 0, 1, N_("EXPORTCONF, exports HexChat settings")},
#endif
	{"FLUSHQ", cmd_flushq, 0, 0, 1,
	 N_("FLUSHQ, flushes the current server's send queue")},
	{"GATE", cmd_gate, 0, 0, 1,
	 N_("GATE <host> [<port>], proxies through a host, port defaults to 23")},
	{"GETBOOL", cmd_getbool, 0, 0, 1, "GETBOOL <command> <title> <text>"},
	{"GETFILE", cmd_getfile, 0, 0, 1, "GETFILE [-folder] [-multi] [-save] <command> <title> [<initial>]"},
	{"GETINT", cmd_getint, 0, 0, 1, "GETINT <default> <command> <prompt>"},
	{"GETSTR", cmd_getstr, 0, 0, 1, "GETSTR <default> <command> <prompt>"},
	{"GHOST", cmd_ghost, 1, 0, 1, N_("GHOST <nick> [password], Kills a ghosted nickname")},
	{"GUI", cmd_gui, 0, 0, 1, "GUI [APPLY|ATTACH|DETACH|SHOW|HIDE|FOCUS|FLASH|ICONIFY|COLOR <n>]\n"
									  "       GUI [MSGBOX <text>|MENU TOGGLE]"},
	{"HELP", cmd_help, 0, 0, 1, 0},
	{"HOP", cmd_hop, 1, 1, 1,
	 N_("HOP <nick>, gives chanhalf-op status to the nick (needs chanop)")},
	{"ID", cmd_id, 1, 0, 1, N_("ID <password>, identifies yourself to nickserv")},
	{"IGNORE", cmd_ignore, 0, 0, 1,
	 N_("IGNORE <mask> <types..> <options..>\n"
	 "    mask - host mask to ignore, eg: *!*@*.aol.com\n"
	 "    types - types of data to ignore, one or all of:\n"
	 "            PRIV, CHAN, NOTI, CTCP, DCC, INVI, ALL\n"
	 "    options - NOSAVE, QUIET")},

	{"INVITE", cmd_invite, 1, 0, 1,
	 N_("INVITE <nick> [<channel>], invites someone to a channel, by default the current channel (needs chanop)")},
	{"JOIN", cmd_join, 1, 0, 0, N_("JOIN <channel>, joins the channel")},
	{"KICK", cmd_kick, 1, 1, 1,
	 N_("KICK <nick> [reason], kicks the nick from the current channel (needs chanop)")},
	{"KICKBAN", cmd_kickban, 1, 1, 1,
	 N_("KICKBAN <nick> [reason], bans then kicks the nick from the current channel (needs chanop)")},
	{"KILLALL", cmd_killall, 0, 0, 1, "KILLALL, immediately exit"},
	{"LAGCHECK", cmd_lagcheck, 0, 0, 1,
	 N_("LAGCHECK, forces a new lag check")},
	{"LASTLOG", cmd_lastlog, 0, 0, 1,
	 N_("LASTLOG [-h] [-m] [-r] [--] <string>, searches for a string in the buffer\n"
	 "    Use -h to highlight the found string(s)\n"
	 "    Use -m to match case\n"
	 "    Use -r when string is a Regular Expression\n"
	 "    Use -- (double hyphen) to end options when searching for, say, the string '-r'")},
	{"LIST", cmd_list, 1, 0, 1, 0},
	{"LOAD", cmd_load, 0, 0, 1, N_("LOAD [-e] <file>, loads a plugin or script")},

	{"MDEHOP", cmd_mdehop, 1, 1, 1,
	 N_("MDEHOP, Mass deop's all chanhalf-ops in the current channel (needs chanop)")},
	{"MDEOP", cmd_mdeop, 1, 1, 1,
	 N_("MDEOP, Mass deop's all chanops in the current channel (needs chanop)")},
	{"ME", cmd_me, 0, 0, 1,
	 N_("ME <action>, sends the action to the current channel (actions are written in the 3rd person, like /me jumps)")},
	{"MENU", cmd_menu, 0, 0, 1, "MENU [-eX] [-i<ICONFILE>] [-k<mod>,<key>] [-m] [-pX] [-r<X,group>] [-tX] {ADD|DEL} <path> [command] [unselect command]\n"
										 "       See http://hexchat.readthedocs.org/en/latest/plugins.html#controlling-the-gui for more details."},
	{"MKICK", cmd_mkick, 1, 1, 1,
	 N_("MKICK, Mass kicks everyone except you in the current channel (needs chanop)")},
	{"MODE", cmd_mode, 1, 0, 1, 0},
	{"MOP", cmd_mop, 1, 1, 1,
	 N_("MOP, Mass op's all users in the current channel (needs chanop)")},
	{"MSG", cmd_msg, 0, 0, 1, N_("MSG <nick> <message>, sends a private message, message \".\" to send to last nick or prefix with \"=\" for dcc chat")},

	{"NAMES", cmd_names, 1, 0, 1,
	 N_("NAMES [channel], Lists the nicks on the channel")},
	{"NCTCP", cmd_nctcp, 1, 0, 1,
	 N_("NCTCP <nick> <message>, Sends a CTCP notice")},
	{"NEWSERVER", cmd_newserver, 0, 0, 1, N_("NEWSERVER [-noconnect] <hostname> [<port>]")},
	{"NICK", cmd_nick, 0, 0, 1, N_("NICK <nickname>, sets your nick")},

	{"NOTICE", cmd_notice, 1, 0, 1,
	 N_("NOTICE <nick/channel> <message>, sends a notice")},
	{"NOTIFY", cmd_notify, 0, 0, 1,
	 N_("NOTIFY [-n network1[,network2,...]] [<nick>], displays your notify list or adds someone to it")},
	{"OP", cmd_op, 1, 1, 1,
	 N_("OP <nick>, gives chanop status to the nick (needs chanop)")},
	{"PART", cmd_part, 1, 1, 0,
	 N_("PART [<channel>] [<reason>], leaves the channel, by default the current one")},
	{"PING", cmd_ping, 1, 0, 1,
	 N_("PING <nick | channel>, CTCP pings nick or channel")},
	{"QUERY", cmd_query, 0, 0, 1,
	 N_("QUERY [-nofocus] <nick> [message], opens up a new privmsg window to someone and optionally sends a message")},
	{"QUIET", cmd_quiet, 1, 1, 1,
	 N_("QUIET <mask> [<quiettype>], quiet everyone matching the mask in the current channel if supported by the server.")},
	{"QUIT", cmd_quit, 0, 0, 1,
	 N_("QUIT [<reason>], disconnects from the current server")},
	{"QUOTE", cmd_quote, 1, 0, 1,
	 N_("QUOTE <text>, sends the text in raw form to the server")},
#ifdef USE_OPENSSL
	{"RECONNECT", cmd_reconnect, 0, 0, 1,
	 N_("RECONNECT [-ssl] [<host>] [<port>] [<password>], Can be called just as /RECONNECT to reconnect to the current server or with /RECONNECT ALL to reconnect to all the open servers")},
#else
	{"RECONNECT", cmd_reconnect, 0, 0, 1,
	 N_("RECONNECT [<host>] [<port>] [<password>], Can be called just as /RECONNECT to reconnect to the current server or with /RECONNECT ALL to reconnect to all the open servers")},
#endif
	{"RECV", cmd_recv, 1, 0, 1, N_("RECV <text>, send raw data to HexChat, as if it was received from the IRC server")},
	{"RELOAD", cmd_reload, 0, 0, 1, N_("RELOAD <name>, reloads a plugin or script")},
	{"SAY", cmd_say, 0, 0, 1,
	 N_("SAY <text>, sends the text to the object in the current window")},
	{"SEND", cmd_send, 0, 0, 1, N_("SEND <nick> [<file>]")},
#ifdef USE_OPENSSL
	{"SERVCHAN", cmd_servchan, 0, 0, 1,
	 N_("SERVCHAN [-ssl] <host> <port> <channel>, connects and joins a channel")},
#else
	{"SERVCHAN", cmd_servchan, 0, 0, 1,
	 N_("SERVCHAN <host> <port> <channel>, connects and joins a channel")},
#endif
#ifdef USE_OPENSSL
	{"SERVER", cmd_server, 0, 0, 1,
	 N_("SERVER [-ssl] <host> [<port>] [<password>], connects to a server, the default port is 6667 for normal connections, and 6697 for ssl connections")},
#else
	{"SERVER", cmd_server, 0, 0, 1,
	 N_("SERVER <host> [<port>] [<password>], connects to a server, the default port is 6667")},
#endif
	{"SET", cmd_set, 0, 0, 1, N_("SET [-e] [-off|-on] [-quiet] <variable> [<value>]")},
	{"SETCURSOR", cmd_setcursor, 0, 0, 1, N_("SETCURSOR [-|+]<position>, reposition the cursor in the inputbox")},
	{"SETTAB", cmd_settab, 0, 0, 1, N_("SETTAB <new name>, change a tab's name, tab_trunc limit still applies")},
	{"SETTEXT", cmd_settext, 0, 0, 1, N_("SETTEXT <new text>, replace the text in the input box")},
	{"SPLAY", cmd_splay, 0, 0, 1, "SPLAY <soundfile>"},
	{"TOPIC", cmd_topic, 1, 1, 1,
	 N_("TOPIC [<topic>], sets the topic if one is given, else shows the current topic")},
	{"TRAY", cmd_tray, 0, 0, 1,
	 N_("\nTRAY -f <timeout> <file1> [<file2>] Blink tray between two icons.\n"
		   "TRAY -f <filename>                  Set tray to a fixed icon.\n"
			"TRAY -i <number>                    Blink tray with an internal icon.\n"
			"TRAY -t <text>                      Set the tray tooltip.\n"
			"TRAY -b <title> <text>              Set the tray balloon."
			)},
	{"UNBAN", cmd_unban, 1, 1, 1,
	 N_("UNBAN <mask> [<mask>...], unbans the specified masks.")},
	{"UNIGNORE", cmd_unignore, 0, 0, 1, N_("UNIGNORE <mask> [QUIET]")},
	{"UNLOAD", cmd_unload, 0, 0, 1, N_("UNLOAD <name>, unloads a plugin or script")},
	{"UNQUIET", cmd_unquiet, 1, 1, 1,
	 N_("UNQUIET <mask> [<mask>...], unquiets the specified masks if supported by the server.")},
	{"URL", cmd_url, 0, 0, 1, N_("URL <url>, opens a URL in your browser")},
	{"USELECT", cmd_uselect, 0, 1, 0,
	 N_("USELECT [-a] [-s] <nick1> <nick2> etc, highlights nick(s) in channel userlist")},
	{"USERLIST", cmd_userlist, 1, 1, 1, 0},
	{"VOICE", cmd_voice, 1, 1, 1,
	 N_("VOICE <nick>, gives voice status to someone (needs chanop)")},
	{"WALLCHAN", cmd_wallchan, 1, 1, 1,
	 N_("WALLCHAN <message>, writes the message to all channels")},
	{"WALLCHOP", cmd_wallchop, 1, 1, 1,
	 N_("WALLCHOP <message>, sends the message to all chanops on the current channel")},
	{0, 0, 0, 0, 0, 0}
};

static const commands * find_internal_command (const char *name)
{
	const commands * cmd = std::find_if(
		std::begin(xc_cmds),
		std::end(xc_cmds),
		[name](const commands& c){
		if (!c.name) return false;
		return g_ascii_strcasecmp(name, c.name) == 0;
	});
	return cmd != std::end(xc_cmds) ? cmd : nullptr;
}

static gboolean
usercommand_show_help (session *sess, const char *name)
{
	bool found = false;
	char buf[1024];

	std::locale locale;
	for (const auto & pop : command_list)
	{
		if (boost::iequals(pop.name, name, locale))
		{
			snprintf (buf, sizeof(buf), _("User Command for: %s\n"), pop.cmd.c_str());
			PrintText (sess, buf);

			found = true;
		}
	}

	return found;
}

static void
help (session *sess, char *tbuf, const char *helpcmd, bool quiet)
{
	if (plugin_show_help (sess, helpcmd))
		return;

	if (usercommand_show_help (sess, helpcmd))
		return;

	auto cmd = find_internal_command (helpcmd);
	if (cmd)
	{
		if (cmd->help)
		{
			snprintf (tbuf, TBUFSIZE, _("Usage: %s\n"), _(cmd->help));
			PrintText (sess, tbuf);
		} else
		{
			if (!quiet)
				PrintText (sess, _("\nNo help available on that command.\n"));
		}
		return;
	}

	if (!quiet)
		PrintText (sess, _("No such command.\n"));
}

/* inserts %a, %c, %d etc into buffer. Also handles &x %x for word/word_eol. *
 *   returns 2 on buffer overflow
 *   returns 1 on success                                                    *
 *   returns 0 on bad-args-for-user-command                                  *
 * - word/word_eol args might be nullptr                                        *
 * - this beast is used for UserCommands, UserlistButtons and CTCP replies   */

int
auto_insert (char *dest, int destlen, const unsigned char *src, const char * const word[],
				 const char * const word_eol[], const char *a, const char *c, const char *d, const char *e, const char *h,
				 const char *n, const char *s, const char *u)
{
	int num;
	char buf[32];
	time_t now;
	struct tm *tm_ptr;
	const char *utf;
	gsize utf_len;
	char *orig = dest;

	destlen--;
	std::locale locale;
	while (src[0])
	{
		if (src[0] == '%' || src[0] == '&')
		{
			if (std::isdigit<char>(src[1], locale))
			{
				if (std::isdigit<char>(src[2], locale) && std::isdigit<char>(src[3], locale))
				{
					buf[0] = src[1];
					buf[1] = src[2];
					buf[2] = src[3];
					buf[3] = 0;
					dest[0] = atoi (buf);
					glib_string utf(g_locale_to_utf8(dest, 1, 0, &utf_len, 0));
					if (utf)
					{
						if ((dest - orig) + utf_len >= destlen)
						{
							return 2;
						}

						std::copy_n(utf.get(), utf_len, dest);
						dest += utf_len;
					}
					src += 3;
				} else
				{
					if (word)
					{
						src++;
						num = src[0] - '0';	/* ascii to decimal */
						if (*word[num] == 0)
							return 0;

						if (src[-1] == '%')
							utf = word[num];
						else
							utf = word_eol[num];

						/* avoid recusive usercommand overflow */
						if ((dest - orig) + strlen (utf) >= destlen)
							return 2;

						strcpy (dest, utf);
						dest += strlen (dest);
					}
				}
			} else
			{
				if (src[0] == '&')
					goto lamecode;
				src++;
				utf = nullptr;
				switch (src[0])
				{
				case '%':
					if ((dest - orig) + 2u >= destlen)
						return 2;
					dest[0] = '%';
					dest[1] = 0;
					dest++;
					break;
				case 'a':
					utf = a; break;
				case 'c':
					utf = c; break;
				case 'd':
					utf = d; break;
				case 'e':
					utf = e; break;
				case 'h':
					utf = h; break;
				case 'm':
					utf = get_sys_str (true); break;
				case 'n':
					utf = n; break;
				case 's':
					utf = s; break;
				case 't':
					now = time (0);
					utf = ctime (&now);
					//utf[19] = 0;
					break;
				case 'u':
					utf = u; break;
				case 'v':
					utf = PACKAGE_VERSION; break;
					break;
				case 'y':
					now = time (0);
					tm_ptr = localtime (&now);
					snprintf (buf, sizeof (buf), "%4d%02d%02d", 1900 +
								 tm_ptr->tm_year, 1 + tm_ptr->tm_mon, tm_ptr->tm_mday);
					utf = buf;
					break;
				default:
					src--;
					goto lamecode;
				}

				if (utf)
				{
					if ((dest - orig) + strlen (utf) >= destlen)
						return 2;
					strcpy (dest, utf);
					dest += strlen (dest);
				}

			}
			src++;
		} else
		{
			utf_len = g_utf8_skip[src[0]];

			if ((dest - orig) + utf_len >= destlen)
				return 2;

			if (utf_len == 1)
			{
		 lamecode:
				dest[0] = src[0];
				dest++;
				src++;
			} else
			{
				std::copy_n(src, utf_len, dest);
				dest += utf_len;
				src += utf_len;
			}
		}
	}

	dest[0] = 0;

	return 1;
}

void check_special_chars (char *cmd, bool do_ascii) /* check for %X */
{
	auto result = check_special_chars(boost::string_ref{ cmd }, do_ascii);
	std::copy(result.cbegin(), result.cend(), cmd);
}

std::string check_special_chars(const boost::string_ref & cmd, bool do_ascii) /* check for %X */
{
	if (cmd.empty())
		return std::string{};

	int occur = 0;
	auto len = cmd.size();
	size_t i = 0, j = 0;

	std::string buf(len + 1, '\0');
	std::locale locale;
	while (cmd[j])
	{
		switch (cmd[j])
		{
		case '%':
			occur++;
			if (do_ascii &&
				j + 3 < len &&
				(std::isdigit(cmd[j + 1], locale) && std::isdigit(cmd[j + 2], locale) &&
				std::isdigit(cmd[j + 3], locale)))
			{
				char tbuf[4];
				tbuf[0] = cmd[j + 1];
				tbuf[1] = cmd[j + 2];
				tbuf[2] = cmd[j + 3];
				tbuf[3] = 0;
				buf[i] = atoi(tbuf);
				gsize utf_len;
				glib_string utf(g_locale_to_utf8(&buf[0] + i, 1, 0, &utf_len, 0));
				if (utf)
				{
					std::copy_n(utf.get(), utf_len, buf.begin() + i);
					i += (utf_len - 1);
				}
				j += 3;
			}
			else
			{
				switch (cmd[j + 1])
				{
				case 'R':
					buf[i] = '\026';
					break;
				case 'U':
					buf[i] = '\037';
					break;
				case 'B':
					buf[i] = '\002';
					break;
				case 'I':
					buf[i] = '\035';
					break;
				case 'C':
					buf[i] = '\003';
					break;
				case 'O':
					buf[i] = '\017';
					break;
				case 'H':	/* CL: invisible text code */
					buf[i] = HIDDEN_CHAR;
					break;
				case '%':
					buf[i] = '%';
					break;
				default:
					buf[i] = '%';
					j--;
					break;
				}
				j++;
				break;
		default:
			buf[i] = cmd[j];
			}
		}
		j++;
		i++;
	}
	buf[i] = 0;
	if (occur)
	{
		auto zero = buf.find_first_of('\0');
		if (zero != std::string::npos)
		{
			buf.erase(zero);
		}

		return buf;
	}
	return cmd.to_string();
}

static void
perform_nick_completion (struct session *sess, char *cmd, char *tbuf)
{
	char *space = strchr (cmd, ' ');
	if (space && space != cmd)
	{
		if (space[-1] == prefs.hex_completion_suffix[0] && space - 1 != cmd)
		{
			auto len = space - cmd - 1;
			if (len < NICKLEN)
			{
				char nick[NICKLEN];

				std::copy_n(cmd, len, std::begin(nick));
				nick[len] = 0;

				int bestlen = std::numeric_limits<int>::max();
				User * best = nullptr;
				for (auto & user : sess->usertree)
				{
					int lenu;

					if (!rfc_ncasecmp(user->nick.c_str(), nick, len))
					{
						lenu = user->nick.size();
						if (lenu == len)
						{
							snprintf(tbuf, TBUFSIZE, "%s%s", user->nick.c_str(), space - 1);
							len = -1;
							break;
						}
						else if (lenu < bestlen)
						{
							bestlen = lenu;
							best = user.get();
						}
					}
				}

				if (len == -1)
					return;

				if (best)
				{
					snprintf (tbuf, TBUFSIZE, "%s%s", best->nick.c_str(), space - 1);
					return;
				}
			}
		}
	}

	strcpy (tbuf, cmd);
}

static void
user_command (session * sess, char *, const std::string & cmd, char *word[],
				  char *word_eol[])
{
	char buf[2048] = { 0 };
	if (!auto_insert (buf, 2048, (const unsigned char*)cmd.c_str(), word, word_eol, "", sess->channel, "",
		sess->server->get_network(true).data(), "",
							sess->server->nick, "", ""))
	{
		PrintText (sess, _("Bad arguments for user command.\n"));
		return;
	}
	if (buf[2047])
		throw std::runtime_error("buffer overflow");
	handle_command (sess, buf, true);
}

/* handle text entered without a hex_input_command_char prefix */

static void
handle_say (session *sess, char *text, int check_spch)
{
	dcc::DCC *dcc;
	char *word[PDIWORDS+1];
	char *word_eol[PDIWORDS+1];
	message_tags_data no_tags = message_tags_data();

	if (strcmp (sess->channel, "(lastlog)") == 0)
	{
		lastlog (sess->lastlog_sess, text, sess->lastlog_flags);
		return;
	}

	auto len = strlen(text);
	std::string pdibuf(len + 1, '\0');

	std::string newcmd(len + NICKLEN + 1, '\0');

	if (check_spch && prefs.hex_input_perc_color)
		check_special_chars (text, !!prefs.hex_input_perc_ascii);

	/* Python relies on this */
	word[PDIWORDS] = nullptr;
	word_eol[PDIWORDS] = nullptr;

	/* split the text into words and word_eol */
	process_data_init (&pdibuf[0], text, word, word_eol, true, false);

	/* a command of "" can be hooked for non-commands */
	if (plugin_emit_command(sess, "", word, word_eol))
		return;

	/* incase a plugin did /close */
	if (!is_session(sess))
		return;

	if (!sess->channel[0] || sess->type == session::SESS_SERVER || sess->type == session::SESS_NOTICES || sess->type == session::SESS_SNOTICES)
	{
		notj_msg (sess);
		return;
	}

	if (prefs.hex_completion_auto)
		perform_nick_completion (sess, text, &newcmd[0]);
	else
		safe_strcpy (&newcmd[0], text, newcmd.size());


	if (sess->type == session::SESS_DIALOG)
	{
		/* try it via dcc, if possible */
		dcc = dcc::dcc_write_chat (sess->channel, &newcmd[0]);
		if (dcc)
		{
			inbound_chanmsg (*sess->server, nullptr, sess->channel,
				sess->server->nick, &newcmd[0], true, false, &no_tags);
			set_topic (sess, net_ip (dcc->addr), net_ip (dcc->addr));
			return;
		}
	}

	if (sess->server->connected)
	{
		char *split_text = nullptr;
		int cmd_length = 13; /* " PRIVMSG ", " ", :, \r, \n */
		size_t offset = 0;

		while ((split_text = split_up_text(sess, &newcmd[0] + offset, cmd_length, split_text)))
		{
			inbound_chanmsg (*sess->server, sess, sess->channel, sess->server->nick,
								  split_text, true, false, &no_tags);
			sess->server->p_message (sess->channel, split_text);
			
			if (*split_text)
				offset += std::strlen(split_text);
			
			g_free(split_text);
		}

		inbound_chanmsg (*sess->server, sess, sess->channel, sess->server->nick,
			&newcmd[0] + offset, true, false, &no_tags);
		sess->server->p_message(sess->channel, &newcmd[0] + offset);
	} else
	{
		notc_msg (sess);
	}
}

namespace
{
	class replace_formatter
	{
		const ircnet * net;
	public:
		replace_formatter(ircnet * net)
			:net(net){}

		template<typename FindResultT>
		std::string operator()(const FindResultT& Match)
		{
			const std::string & temp = Match.str();
			switch (temp.back()){
			case 'n':
				if (net->nick)
				{
					return net->nick.get();
				}
				else
				{
					return prefs.hex_irc_nick1;
				}
			case 'p':
				if (net->pass)
				{
					return net->pass;
				}
			case 'r':
				if (net->real)
				{
					return net->real;
				}
				else
				{
					return prefs.hex_irc_real_name;
				}
			case 'u':
				if (net->user)
				{
					return net->user;
				}
				else
				{
					return prefs.hex_irc_user_name;
				}
			default:
				return temp;
			}
		}
	};
}// end anonymous namespace

std::string command_insert_vars (session *sess,  const std::string& cmd)
{
	ircnet *mynet = sess->server->network;
	static const boost::regex replacevals("%(n|p|r|u)");

	if (!mynet)										/* shouldn't really happen */
	{
		return cmd;
	}

	std::ostringstream expanded;
	std::ostream_iterator<char> out(expanded);
	boost::regex_replace(out, cmd.cbegin(), cmd.cend(), replacevals, replace_formatter(mynet));
	return expanded.str();
}

namespace
{
	class command_counter
	{
		int & _command_level;
		command_counter(const command_counter&) = delete;
		command_counter& operator=(const command_counter&) = delete;
	public:
		command_counter(int & command_count)
			:_command_level(command_count){};
		~command_counter()
		{
			if (_command_level > 0)
				--_command_level;
		}
	};
}
/* handle a command, without the '/' prefix */

bool
handle_command (session *sess, char *cmd, bool check_spch)
{
	static int command_level = 0;

	if (command_level > 99)
	{
		fe_message (_("Too many recursive usercommands, aborting."), FE_MSG_ERROR);
		return true;
	}
	command_level++;
	command_counter counter(command_level);
	// do not put any new logic above this line

	bool user_cmd = false;
	char *word[PDIWORDS + 1];
	char *word_eol[PDIWORDS + 1];

	auto len = strlen (cmd);
	// TODO .. figure out another way to do this... ideally we should let commands do their own buffers
	auto pdilen = std::max<size_t>(len + 1, TBUFSIZE);
	std::string pdibuf(pdilen, '\0');
	auto tbuflen = std::max<size_t>((len * 2) + 1, TBUFSIZE);
	std::string tbuf(tbuflen, '\0');

	/* split the text into words and word_eol */
	process_data_init (&pdibuf[0], cmd, word, word_eol, true, true);

	/* ensure an empty string at index 32 for cmd_deop etc */
	/* (internal use only, plugins can still only read 1-31). */
	word[PDIWORDS] = "\000\000";
	word_eol[PDIWORDS] = "\000\000";

	auto int_cmd = find_internal_command (word[1]);
	/* redo it without quotes processing, for some commands like /JOIN */
	if (int_cmd && !int_cmd->handle_quotes)
	{
		process_data_init (&pdibuf[0], cmd, word, word_eol, false, false);
	}

	if (check_spch && prefs.hex_input_perc_color)
	{
		check_special_chars (cmd, !!prefs.hex_input_perc_ascii);
	}

	if (plugin_emit_command (sess, word[1], word, word_eol))
	{
		return true;
	}

	/* incase a plugin did /close */
	if (!is_session (sess))
	{
		return true;
	}

	/* first see if it's a userCommand */
	std::locale locale;
	for (const auto & pop : command_list)
	{
		if (boost::iequals(pop.name, word[1], locale))
		{
			user_command (sess, &tbuf[0], pop.cmd, word, word_eol);
			user_cmd = true;
		}
	}

	if (user_cmd)
	{
		return true;
	}

	/* now check internal commands */
	int_cmd = find_internal_command (word[1]);

	if (int_cmd)
	{
		if (int_cmd->needserver && !sess->server->connected)
		{
			notc_msg (sess);
		}
		else if (int_cmd->needchannel && !sess->channel[0])
		{
			notj_msg (sess);
		}
		else
		{
			switch (int_cmd->callback (sess, &tbuf[0], word, word_eol))
			{
				case false:
					help(sess, &tbuf[0], int_cmd->name, true);
					break;
				case 2:
					return false;
				default:
					break;
			}
		}
	}
	else
	{
		/* unknown command, just send it to the server and hope */
		if (!sess->server->connected)
		{
			PrintText (sess, _("Unknown Command. Try /help\n"));
		}
		else
		{
			sess->server->p_raw (cmd);
		}
	}

	return true;
}

/* handle one line entered into the input box */

static bool
handle_user_input (session *sess, char *text, int history, int nocommand)
{
	if (*text == '\0')
		return true;

	if (history)
		sess->hist.add(text);

	/* is it NOT a command, just text? */
	if (nocommand || text[0] != prefs.hex_input_command_char[0])
	{
		handle_say (sess, text, true);
		return true;
	}

	/* check for // */
	if (text[0] == prefs.hex_input_command_char[0] && text[1] == prefs.hex_input_command_char[0])
	{
		handle_say (sess, text + 1, true);
		return true;
	}

#if 0 /* Who would remember all this? */
	if (prefs.hex_input_command_char[0] == '/')
	{
		int i;
		const char *unix_dirs [] = {
			"/bin/",
			"/boot/",
			"/dev/",
			"/etc/",
			"/home/",
			"/lib/",
			"/lost+found/",
			"/mnt/",
			"/opt/",
			"/proc/",
			"/root/",
			"/sbin/",
			"/tmp/",
			"/usr/",
			"/var/",
			"/gnome/",
			nullptr
		};
		for (i = 0; unix_dirs[i] != nullptr; i++)
			if (strncmp (text, unix_dirs[i], strlen (unix_dirs[i]))==0)
			{
				handle_say (sess, text, true);
				return 1;
			}
	}
#endif

	return handle_command (sess, text + 1, true);
}

/* changed by Steve Green. Macs sometimes paste with imbedded \r */
void
handle_multiline (session *sess, char *cmd, int history, int nocommand)
{
	while (*cmd)
	{
		char *cr = cmd + strcspn (cmd, "\n\r");
		int end_of_string = *cr == 0;
		*cr = 0;
		if (!handle_user_input (sess, cmd, history, nocommand))
			return;
		if (end_of_string)
			break;
		cmd = cr + 1;
	}
}

/*void
handle_multiline (session *sess, char *cmd, int history, int nocommand)
{
	char *cr;

	cr = strchr (cmd, '\n');
	if (cr)
	{
		while (1)
		{
			if (cr)
				*cr = 0;
			if (!handle_user_input (sess, cmd, history, nocommand))
				return;
			if (!cr)
				break;
			cmd = cr + 1;
			if (*cmd == 0)
				break;
			cr = strchr (cmd, '\n');
		}
	} else
	{
		handle_user_input (sess, cmd, history, nocommand);
	}
}*/
