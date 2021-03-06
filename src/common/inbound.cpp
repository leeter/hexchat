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
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/types.h>
#include <boost/utility/string_ref.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#define WANTARPA
#define WANTDNS
#include "inet.hpp"

#include <gio/gio.h>

#include "hexchat.hpp"
#include "util.hpp"
#include "ignore.hpp"
#include "fe.hpp"
#include "modes.hpp"
#include "notify.hpp"
#include "outbound.hpp"
#include "inbound.hpp"
#include "server.hpp"
#include "servlist.hpp"
#include "text.hpp"
#include "hexchatc.hpp"
#include "chanopt.hpp"
#include "dcc.hpp"
#include "sasl.hpp"
#include "userlist.hpp"
#include "session.hpp"
#include "session_logging.hpp"

namespace dcc = hexchat::dcc;

void
clear_channel (session &sess)
{
	if (sess.channel[0])
		strcpy (sess.waitchannel, sess.channel);
	sess.channel[0] = 0;
	sess.doing_who = false;
	sess.done_away_check = false;

	//log_close (sess);

	sess.current_modes.erase();

	if (sess.mode_timeout_tag)
	{
		fe_timeout_remove (sess.mode_timeout_tag);
		sess.mode_timeout_tag = 0;
	}

	fe_clear_channel (sess);
	userlist_clear (&sess);
	fe_set_nonchannel (&sess, false);
	fe_set_title (sess);
}

void
set_topic (session *sess, const std::string & topic, const std::string & stripped_topic)
{
	sess->topic = stripped_topic;
	fe_set_topic (sess, topic, stripped_topic);
}

static session *
find_session_from_nick (const char *nick, server &serv)
{
	auto sess = find_dialog (serv, nick);
	if (sess)
		return sess;

	if (serv.front_session)
	{
		if (userlist_find (serv.front_session, nick))
			return serv.front_session;
	}

	if (current_sess && current_sess->server == &serv)
	{
		if (userlist_find (current_sess, nick))
			return current_sess;
	}

	for (auto list = sess_list; list; list = g_slist_next(list))
	{
		sess = static_cast<session*>(list->data);
		if (sess->server == &serv)
		{
			if (userlist_find (sess, nick))
				return sess;
		}
	}
	return 0;
}

static session *
inbound_open_dialog (server &serv, const char *from,
							const message_tags_data *tags_data)
{
	session *sess;

	sess = new_ircwindow(&serv, from, session::SESS_DIALOG, false);
	/* for playing sounds */
	EMIT_SIGNAL_TIMESTAMP (XP_TE_OPENDIALOG, sess, nullptr, nullptr, nullptr, nullptr, 0,
								  tags_data->timestamp);

	return sess;
}

static void
inbound_make_idtext (server &serv, char *idtext, int max, bool id)
{
	idtext[0] = 0;
	if (serv.have_idmsg || serv.have_accnotify)
	{
		if (id)
		{
			safe_strcpy (idtext, prefs.hex_irc_id_ytext, max);
		} else
		{
			safe_strcpy (idtext, prefs.hex_irc_id_ntext, max);
		}
		/* convert codes like %C,%U to the proper ones */
		check_special_chars (idtext, true);
	}
}

void
inbound_privmsg (server &serv, char *from, char *ip, char *text, bool id,
					  const message_tags_data *tags_data)
{
	session *sess;
	struct User *user;
	char idtext[64];
	bool nodiag = false;

	sess = find_dialog (serv, from);

	if (sess || prefs.hex_gui_autoopen_dialog)
	{
		/*0=ctcp  1=priv will set hex_gui_autoopen_dialog=0 here is flud detected */
		if (!sess)
		{
			if (flood_check(from, ip, serv, current_sess, flood_check_type::PRIV))
				/* Create a dialog session */
				sess = inbound_open_dialog (serv, from, tags_data);
			else
				sess = serv.server_session;
			if (!sess)
				return; /* ?? */
		}

		if (ip && ip[0])
		{
			if (prefs.hex_irc_logging &&
				(sess->topic.empty() || sess->topic != ip))
			{
				std::ostringstream buf;
				buf << boost::format("[%s has address %s]\n") % from % ip;
				sess->log.write(buf.str(), 0);
				/*char tbuf[1024];
				snprintf (tbuf, sizeof (tbuf), "[%s has address %s]\n", from, ip);
				write (sess->logfd, tbuf, strlen (tbuf));*/
			}
			set_topic (sess, ip, ip);
		}
		inbound_chanmsg (serv, nullptr, nullptr, from, text, false, id, tags_data);
		return;
	}

	sess = find_session_from_nick (from, serv);
	if (!sess)
	{
		sess = serv.front_session;
		nodiag = true; /* We don't want it to look like a normal message in front sess */
	}

	user = userlist_find (sess, from);
	if (user)
	{
		user->lasttalk = time (0);
		if (user->account)
			id = true;
	}
	
	inbound_make_idtext (serv, idtext, sizeof (idtext), id);

	if (sess->type == session::SESS_DIALOG && !nodiag)
		EMIT_SIGNAL_TIMESTAMP (XP_TE_DPRIVMSG, sess, from, text, idtext, nullptr, 0,
									  tags_data->timestamp);
	else
		EMIT_SIGNAL_TIMESTAMP (XP_TE_PRIVMSG, sess, from, text, idtext, nullptr, 0, 
									  tags_data->timestamp);
}

/* used for Alerts section. Masks can be separated by commas and spaces. */

bool alert_match_word (const char *word, const char *masks)
{
	if (!masks || masks[0] == 0)
		return false;

	std::unique_ptr<char[]> mutable_masks(new_strdup(masks));
	char *p = mutable_masks.get();

	for (;;)
	{
		/* if it's a 0, space or comma, the word has ended. */
		if (*p == 0 || *p == ' ' || *p == ',')
		{
			auto endchar = *p;
			*p = 0;
			bool res = match (mutable_masks.get(), word);
			*p = endchar;

			if (res)
				return true;	/* yes, matched! */

			masks = p + 1;
			if (*p == 0)
				return false;
		}
		p++;
	}
}

bool alert_match_text (const char text[], const char masks[])
{
	if (!masks || masks[0] == 0)
		return false;

	std::unique_ptr<char[]> mutable_text(new_strdup(text));
	unsigned char *p = reinterpret_cast<unsigned char*>(mutable_text.get());
	char * i_text = mutable_text.get();

	for (;;)
	{
		if (*p >= '0' && *p <= '9')
		{
			p++;
			continue;
		}

		/* if it's RFC1459 <special>, it can be inside a word */
		switch (*p)
		{
		case '-': case '[': case ']': case '\\':
		case '`': case '^': case '{': case '}':
		case '_': case '|':
			p++;
			continue;
		}

		/* if it's a 0, space or comma, the word has ended. */
		if (*p == 0 || *p == ' ' || *p == ',' ||
			/* if it's anything BUT a letter, the word has ended. */
			 (!g_unichar_isalpha (g_utf8_get_char ((const gchar*)p))))
		{
			auto endchar = *p;
			*p = 0;
			bool res = alert_match_word(i_text, masks);
			*p = endchar;

			if (res)
				return true;	/* yes, matched! */

			i_text = (char*)(p + g_utf8_skip [p[0]]);
			if (*p == 0)
				return false;
		}

		p += g_utf8_skip [p[0]];
	}
}

static bool
is_hilight (const char from[], const char text[], session *sess, server &serv)
{
	if (alert_match_word (from, prefs.hex_irc_no_hilight))
		return false;

	auto temp = strip_color (text, STRIP_ALL);

	if (alert_match_text(temp.c_str(), serv.nick) ||
		alert_match_text(temp.c_str(), prefs.hex_irc_extra_hilight) ||
		alert_match_word(temp.c_str(), prefs.hex_irc_nick_hilight))
	{
		if (sess != current_tab)
		{
			sess->nick_said = true;
			lastact_update (sess);
		}
		fe_set_hilight (sess);
		return true;
	}
	return false;
}

void
inbound_action (session *sess, const std::string& chan, char *from, char *ip, char *text,
					 bool fromme, bool id, const message_tags_data *tags_data)
{
	session *def = sess;
	if (!sess->server)
		throw std::runtime_error("invalid server reference");
	server &serv = *(sess->server);
	struct User *user;
	char nickchar[2] = "\000";
	char idtext[64];
	bool privaction = false;

	if (!fromme)
	{
		if (serv.is_channel_name (chan))
		{
			sess = find_channel (serv, chan);
		} else
		{
			/* it's a private action! */
			privaction = true;
			/* find a dialog tab for it */
			sess = find_dialog (serv, from);
			/* if non found, open a new one */
			if (!sess && prefs.hex_gui_autoopen_dialog)
			{
				/* but only if it wouldn't flood */
				if (flood_check(from, ip, serv, current_sess, flood_check_type::PRIV))
					sess = inbound_open_dialog (serv, from, tags_data);
				else
					sess = serv.server_session;
			}
			if (!sess)
			{
				sess = find_session_from_nick (from, serv);
				/* still not good? */
				if (!sess)
					sess = serv.front_session;
			}
		}
	}

	if (!sess)
		sess = def;

	if (sess != current_tab)
	{
		if (fromme)
		{
			sess->msg_said = false;
			sess->new_data = true;
		} else
		{
			sess->msg_said = true;
			sess->new_data = false;
		}
		lastact_update (sess);
	}

	user = userlist_find (sess, from);
	if (user)
	{
		nickchar[0] = user->prefix[0];
		user->lasttalk = std::time (nullptr);
		if (user->account)
			id = true;
		if (user->me)
			fromme = true;
	}

	inbound_make_idtext (serv, idtext, sizeof (idtext), id);

	if (!fromme && !privaction)
	{
		if (is_hilight (from, text, sess, serv))
		{
			EMIT_SIGNAL_TIMESTAMP (XP_TE_HCHANACTION, sess, from, text, nickchar,
										  idtext, 0, tags_data->timestamp);
			return;
		}
	}

	if (fromme)
		EMIT_SIGNAL_TIMESTAMP (XP_TE_UACTION, sess, from, text, nickchar, idtext,
									  0, tags_data->timestamp);
	else if (!privaction)
		EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANACTION, sess, from, text, nickchar,
									  idtext, 0, tags_data->timestamp);
	else if (sess->type == session::SESS_DIALOG)
		EMIT_SIGNAL_TIMESTAMP (XP_TE_DPRIVACTION, sess, from, text, idtext, nullptr,
									  0, tags_data->timestamp);
	else
		EMIT_SIGNAL_TIMESTAMP (XP_TE_PRIVACTION, sess, from, text, idtext, nullptr, 0,
									  tags_data->timestamp);
}

void
inbound_chanmsg (server &serv, session *sess, char *chan, char *from, 
					  char *text, bool fromme, bool id, 
					  const message_tags_data *tags_data)
{
	if (!sess)
	{
		if (chan)
		{
			sess = find_channel (serv, chan);
			if (!sess && !serv.is_channel_name (chan))
				sess = find_dialog (serv, chan);
		} else
		{
			sess = find_dialog (serv, from);
		}
		if (!sess)
			return;
	}

	if (sess != current_tab)
	{
		sess->msg_said = true;
		sess->new_data = false;
		lastact_update (sess);
	}

	auto user = userlist_find (sess, from);
	char nickchar[2] = "\000";
	if (user)
	{
		if (user->account)
			id = true;
		nickchar[0] = user->prefix[0];
		user->lasttalk = std::time (nullptr);
		if (user->me)
			fromme = true;
	}

	if (fromme)
	{
		if (prefs.hex_away_auto_unmark && serv.is_away && !tags_data->timestamp)
			sess->server->p_set_back ();
		EMIT_SIGNAL_TIMESTAMP (XP_TE_UCHANMSG, sess, from, text, nickchar, nullptr,
									  0, tags_data->timestamp);
		return;
	}

	char idtext[64];
	inbound_make_idtext (serv, idtext, sizeof (idtext), id);

	if (sess->type == session::SESS_DIALOG)
		EMIT_SIGNAL_TIMESTAMP (XP_TE_DPRIVMSG, sess, from, text, idtext, nullptr, 0,
									  tags_data->timestamp);
	else if (is_hilight(from, text, sess, serv))
		EMIT_SIGNAL_TIMESTAMP (XP_TE_HCHANMSG, sess, from, text, nickchar, idtext,
									  0, tags_data->timestamp);
	else
		EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANMSG, sess, from, text, nickchar, idtext,
									  0, tags_data->timestamp);
}

void
inbound_newnick (server &serv, char *nick, char *newnick, int quiet,
					  const message_tags_data *tags_data)
{
	bool me = false;
	if (!serv.p_cmp (nick, serv.nick))
	{
		me = true;
		safe_strcpy (serv.nick, newnick, NICKLEN);
	}

	for (auto list = sess_list; list; list = g_slist_next(list))
	{
		auto sess = static_cast<session*>(list->data);
		if (sess->server == &serv)
		{
			if (userlist_change(sess, nick, newnick) || (me && sess->type == session::SESS_SERVER))
			{
				if (!quiet)
				{
					if (me)
						EMIT_SIGNAL_TIMESTAMP (XP_TE_UCHANGENICK, sess, nick, 
													  newnick, nullptr, nullptr, 0,
													  tags_data->timestamp);
					else
						EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANGENICK, sess, nick,
													  newnick, nullptr, nullptr, 0, tags_data->timestamp);
				}
			}
			if (sess->type == session::SESS_DIALOG && !serv.p_cmp(sess->channel, nick))
			{
				safe_strcpy (sess->channel, newnick, CHANLEN);
				fe_set_channel (sess);
			}
			fe_set_title (*sess);
		}
	}

	dcc::dcc_change_nick (serv, nick, newnick);

	if (me)
		fe_set_nick (serv, newnick);
}

/* find a "<none>" tab */
static session *
find_unused_session (const server &serv)
{
	for (auto list = sess_list; list; list = g_slist_next(list))
	{
		auto sess = static_cast<session *>(list->data);
		if (sess->type == session::SESS_CHANNEL && sess->channel[0] == 0 &&
			 sess->server == &serv)
		{
			if (sess->waitchannel[0] == 0)
				return sess;
		}
	}
	return nullptr;
}

static session *
find_session_from_waitchannel (const char *chan, const server &serv)
{
	for (auto list = sess_list; list; list = g_slist_next(list))
	{
		auto sess = static_cast<session *>(list->data);
		if (sess->server == &serv && sess->channel[0] == 0 && sess->type == session::SESS_CHANNEL)
		{
			if (!serv.p_cmp (chan, sess->waitchannel))
				return sess;
		}
	}
	return nullptr;
}

void
inbound_ujoin (server &serv, char *chan, char *nick, char *ip,
					const message_tags_data *tags_data)
{
	session *sess;
	bool found_unused = false;

	/* already joined? probably a bnc */
	sess = find_channel (serv, chan);
	if (!sess)
	{
		/* see if a window is waiting to join this channel */
		sess = find_session_from_waitchannel (chan, serv);
		if (!sess)
		{
			/* find a "<none>" tab and use that */
			sess = find_unused_session (serv);
			found_unused = sess != nullptr;
			if (!sess)
				/* last resort, open a new tab/window */
				sess = new_ircwindow(&serv, chan, session::SESS_CHANNEL, true);
		}
	}

	safe_strcpy (sess->channel, chan, CHANLEN);
	if (found_unused)
	{
		chanopt_load (sess);
		scrollback_load (*sess);
		if (sess->scrollwritten && sess->scrollback_replay_marklast)
			sess->scrollback_replay_marklast (sess);
	}

	fe_set_channel (sess);
	fe_set_title (*sess);
	fe_set_nonchannel (sess, true);
	userlist_clear (sess);

	//log_open_or_close (sess);

	sess->waitchannel[0] = 0;
	sess->ignore_date = true;
	sess->ignore_mode = true;
	sess->ignore_names = true;
	sess->end_of_names = false;

	/* sends a MODE */
	serv.p_join_info (chan);

	EMIT_SIGNAL_TIMESTAMP (XP_TE_UJOIN, sess, nick, chan, ip, nullptr, 0,
								  tags_data->timestamp);

	if (prefs.hex_irc_who_join)
	{
		/* sends WHO #channel */
		serv.p_user_list (chan);
		sess->doing_who = true;
	}
}

void
inbound_ukick (server &serv, char *chan, char *kicker, char *reason,
					const message_tags_data *tags_data)
{
	session *sess = find_channel (serv, chan);
	if (sess)
	{
		EMIT_SIGNAL_TIMESTAMP (XP_TE_UKICK, sess, serv.nick, chan, kicker, 
									  reason, 0, tags_data->timestamp);
		clear_channel (*sess);
		if (prefs.hex_irc_auto_rejoin)
		{
			serv.p_join (chan, sess->channelkey);
			safe_strcpy (sess->waitchannel, chan, CHANLEN);
		}
	}
}

void
inbound_upart (server &serv, char *chan, char *ip, char *reason,
					const message_tags_data *tags_data)
{
	session *sess = find_channel (serv, chan);
	if (sess)
	{
		if (*reason)
			EMIT_SIGNAL_TIMESTAMP (XP_TE_UPARTREASON, sess, serv.nick, ip, chan,
										  reason, 0, tags_data->timestamp);
		else
			EMIT_SIGNAL_TIMESTAMP (XP_TE_UPART, sess, serv.nick, ip, chan, nullptr,
										  0, tags_data->timestamp);
		clear_channel (*sess);
	}
}

void
inbound_nameslist (server &serv, char *chan, char *names,
						 const message_tags_data *tags_data)
{
	session *sess = find_channel (serv, chan);
	if (!sess)
	{
		EMIT_SIGNAL_TIMESTAMP (XP_TE_USERSONCHAN, serv.server_session, chan,
									  names, nullptr, nullptr, 0, tags_data->timestamp);
		return;
	}
	if (!sess->ignore_names)
		EMIT_SIGNAL_TIMESTAMP (XP_TE_USERSONCHAN, sess, chan, names, nullptr, nullptr,
									  0, tags_data->timestamp);

	if (sess->end_of_names)
	{
		sess->end_of_names = false;
		userlist_clear (sess);
	}

	std::istringstream namesbuff{ names };
	for (std::string token; std::getline(namesbuff, token, ' ');)
	{
		if (token.empty())
			continue;

		auto offset = size_t{ NICKLEN };
		const char* host = nullptr;

		if (serv.have_uhnames)
		{
			offset = 0;

			auto bang_loc = token.find_last_of('!');
			if (bang_loc != std::string::npos)
			{
				offset += bang_loc;
				if (offset++ < token.size())
					host = token.c_str() + offset;
			}
		}
		auto name = token.substr(0, std::min({ offset - 1, size_t{ NICKLEN - 1 }, token.size() }));

		userlist_add (sess, name.c_str(), host, nullptr, nullptr, tags_data);
	}
}

void
inbound_topic (server &serv, char *chan, char *topic_text,
					const message_tags_data *tags_data)
{
	session *sess = find_channel (serv, chan);

	if (sess)
	{
		auto stripped_topic = strip_color (topic_text, STRIP_ALL);
		set_topic (sess, topic_text, stripped_topic);
	} else
		sess = serv.server_session;

	EMIT_SIGNAL_TIMESTAMP (XP_TE_TOPIC, sess, chan, topic_text, nullptr, nullptr, 0,
								  tags_data->timestamp);
}

void
inbound_topicnew (const server &serv, char *nick, char *chan, char *topic,
						const message_tags_data *tags_data)
{
	session *sess;

	sess = find_channel (serv, chan);
	if (sess)
	{
		EMIT_SIGNAL_TIMESTAMP (XP_TE_NEWTOPIC, sess, nick, topic, chan, nullptr, 0,
									  tags_data->timestamp);
		auto stripped_topic = strip_color (topic, STRIP_ALL);
		set_topic (sess, topic, stripped_topic);
	}
}

void
inbound_join (const server &serv, char *chan, char *user, char *ip, char *account,
				  char *realname, const message_tags_data *tags_data)
{
	session *sess = find_channel (serv, chan);
	if (sess)
	{
		EMIT_SIGNAL_TIMESTAMP (XP_TE_JOIN, sess, user, chan, ip, account, 0,
									  tags_data->timestamp);
		userlist_add (sess, user, ip, account, realname, tags_data);
	}
}

void
inbound_kick (const server &serv, char *chan, char *user, char *kicker, char *reason,
				  const message_tags_data *tags_data)
{
	session *sess = find_channel (serv, chan);
	if (sess)
	{
		EMIT_SIGNAL_TIMESTAMP (XP_TE_KICK, sess, kicker, user, chan, reason, 0,
									  tags_data->timestamp);
		userlist_remove (sess, user);
	}
}

void
inbound_part (const server &serv, char *chan, char *user, char *ip, char *reason,
				  const message_tags_data *tags_data)
{
	session *sess = find_channel (serv, chan);
	if (sess)
	{
		if (*reason)
			EMIT_SIGNAL_TIMESTAMP (XP_TE_PARTREASON, sess, user, ip, chan, reason,
										  0, tags_data->timestamp);
		else
			EMIT_SIGNAL_TIMESTAMP (XP_TE_PART, sess, user, ip, chan, nullptr, 0,
										  tags_data->timestamp);
		userlist_remove (sess, user);
	}
}

void
inbound_topictime (server &serv, char *chan, char *nick, time_t stamp,
						 const message_tags_data *tags_data)
{
	char *tim = ctime (&stamp);
	session *sess = find_channel (serv, chan);

	if (!sess)
		sess = serv.server_session;

	tim[24] = 0;	/* get rid of the \n */
	EMIT_SIGNAL_TIMESTAMP (XP_TE_TOPICDATE, sess, chan, nick, tim, nullptr, 0,
								  tags_data->timestamp);
}

void
inbound_quit (server &serv, char *nick, char *ip, char *reason,
				  const message_tags_data *tags_data)
{
	bool was_on_front_session = false;

	for(auto list = sess_list; list; list = g_slist_next(list))
	{
		auto sess = static_cast<session *>(list->data);
		if (sess->server == &serv)
		{
			if (sess == current_sess)
				was_on_front_session = true;
			struct User *user;
			if ((user = userlist_find (sess, nick)))
			{
				EMIT_SIGNAL_TIMESTAMP (XP_TE_QUIT, sess, nick, reason, ip, nullptr, 0,
											  tags_data->timestamp);
				userlist_remove_user (sess, user);
			}
			else if (sess->type == session::SESS_DIALOG && !serv.p_cmp(sess->channel, nick))
			{
				EMIT_SIGNAL_TIMESTAMP (XP_TE_QUIT, sess, nick, reason, ip, nullptr, 0,
											  tags_data->timestamp);
			}
		}
	}

	notify_set_offline (serv, nick, was_on_front_session, tags_data);
}

void
inbound_account (const server &serv, const char *nick, const char *account,
					  const message_tags_data *tags_data)
{
	for (auto list = sess_list; list; list = g_slist_next(list))
	{
		auto sess = static_cast<session*>(list->data);
		if (sess->server == &serv)
			userlist_set_account (sess, nick, account);
	}
}

void
inbound_ping_reply (session *sess, char *timestring, char *from,
						  const message_tags_data *tags_data)
{
	unsigned long tim, nowtim, dif;
	int lag = 0;
	char outbuf[64];

	if (strncmp (timestring, "LAG", 3) == 0)
	{
		timestring += 3;
		lag = 1;
	}

	tim = strtoul (timestring, nullptr, 10);
	nowtim = make_ping_time ();
	dif = nowtim - tim;

	sess->server->ping_recv = boost::chrono::steady_clock::now();

	if (lag)
	{
		sess->server->lag_sent = 0;
		sess->server->lag = dif;
		fe_set_lag (*sess->server, dif);
		return;
	}

	if (atol (timestring) == 0)
	{
		if (sess->server->lag_sent)
			sess->server->lag_sent = 0;
		else
			EMIT_SIGNAL_TIMESTAMP (XP_TE_PINGREP, sess, from, "?", nullptr, nullptr, 0,
										  tags_data->timestamp);
	} else
	{
		snprintf (outbuf, sizeof (outbuf), "%ld.%03ld", dif / 1000, dif % 1000);
		EMIT_SIGNAL_TIMESTAMP (XP_TE_PINGREP, sess, from, outbuf, nullptr, nullptr, 0,
									  tags_data->timestamp);
	}
}

static session *
find_session_from_type (int type, server &serv)
{
	for (auto list = sess_list; list; list = g_slist_next(list))
	{
		auto sess = static_cast<session*>(list->data);
		if (sess->type == type && sess->server == &serv)
			return sess;
	}
	return nullptr;
}

void
inbound_notice (server &serv, char *to, char *nick, char *msg, char *ip, int id,
					 const message_tags_data *tags_data)
{
	char *po,*ptr=to;
	session *sess = 0;
	bool server_notice = false;

	if (serv.is_channel_name (ptr))
		sess = find_channel (serv, ptr);

	/* /notice [mode-prefix]#channel should end up in that channel */
	if (!sess && serv.nick_prefixes.find_first_of(ptr[0]) != std::string::npos)
	{
		ptr++;
		sess = find_channel (serv, ptr);
	}

	if (strcmp (nick, ip) == 0)
		server_notice = true;

	if (!sess)
	{
		ptr = 0;
		if (prefs.hex_irc_notice_pos == 0)
		{
											/* paranoia check */
			if (msg[0] == '[' && (!serv.have_idmsg || id))
			{
				/* guess where chanserv meant to post this -sigh- */
				if (!g_ascii_strcasecmp (nick, "ChanServ") && !find_dialog (serv, nick))
				{
					std::string dest(msg + 1);
					auto end = dest.find_first_of(']');
					if (end != std::string::npos)
					{
						dest.erase(dest.begin() + end, dest.end());
						sess = find_channel (serv, dest);
					}
				}
			}
			if (!sess)
				sess = find_session_from_nick (nick, serv);
		} else if (prefs.hex_irc_notice_pos == 1)
		{
			int stype = server_notice ? session::SESS_SNOTICES : session::SESS_NOTICES;
			sess = find_session_from_type (stype, serv);
			if (!sess)
			{
				if (stype == session::SESS_NOTICES)
					sess = new_ircwindow(&serv, "(notices)", session::SESS_NOTICES, false);
				else
					sess = new_ircwindow(&serv, "(snotices)", session::SESS_SNOTICES, false);
				fe_set_channel (sess);
				fe_set_title (*sess);
				fe_set_nonchannel (sess, false);
				userlist_clear (sess);
				//log_open_or_close (sess);
			}
			/* Avoid redundancy with some Undernet notices */
			if (!strncmp (msg, "*** Notice -- ", 14))
				msg += 14;
		} else
		{
			sess = serv.front_session;
		}

		if (!sess)
		{
			if (server_notice)	
				sess = serv.server_session;
			else
				sess = serv.front_session;
		}
	}

	if (msg[0] == 1)
	{
		msg++;
		if (!strncmp (msg, "PING", 4))
		{
			inbound_ping_reply (sess, msg + 5, nick, tags_data);
			return;
		}
	}
	po = strchr (msg, '\001');
	if (po)
		po[0] = 0;

	if (server_notice)
		EMIT_SIGNAL_TIMESTAMP (XP_TE_SERVNOTICE, sess, msg, nick, nullptr, nullptr, 0,
									  tags_data->timestamp);
	else if (ptr)
		EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANNOTICE, sess, nick, to, msg, nullptr, 0,
									  tags_data->timestamp);
	else
		EMIT_SIGNAL_TIMESTAMP (XP_TE_NOTICE, sess, nick, msg, nullptr, nullptr, 0,
									  tags_data->timestamp);
}

void
inbound_away (server &serv, char *nick, char *msg,
				  const message_tags_data *tags_data)
{
	auto away_msg = serv.get_away_message(nick);
	if (away_msg && away_msg->second == msg)	/* Seen the msg before? */
	{
		if (prefs.hex_away_show_once && !serv.inside_whois)
			return;
	} else
	{
		serv.save_away_message(nick, boost::make_optional<std::string>(msg != nullptr, msg));
	}

	session *sess = nullptr;
	if (prefs.hex_irc_whois_front)
		sess = serv.front_session;
	else
	{
		if (!serv.inside_whois)
			sess = find_session_from_nick (nick, serv);
		if (!sess)
			sess = serv.server_session;
	}

	/* possibly hide the output */
	if (!serv.inside_whois || !serv.skip_next_whois)
		EMIT_SIGNAL_TIMESTAMP (XP_TE_WHOIS5, sess, nick, msg, nullptr, nullptr, 0,
									  tags_data->timestamp);

	for (auto list = sess_list; list; list = g_slist_next(list))
	{
		auto sess = static_cast<session*>(list->data);
		if (sess->server == &serv)
			userlist_set_away (sess, nick, true);
	}
}

void
inbound_away_notify (const server &serv, char *nick, char *reason,
							const message_tags_data *tags_data)
{
	for (auto list = sess_list; list; list = g_slist_next(list))
	{
		auto sess = static_cast<session*>(list->data);
		if (sess->server == &serv)
		{
			userlist_set_away (sess, nick, reason ? true : false);
			if (sess == serv.front_session && notify_is_in_list (serv, nick))
			{
				if (reason)
					EMIT_SIGNAL_TIMESTAMP (XP_TE_NOTIFYAWAY, sess, nick, reason, nullptr,
												  nullptr, 0, tags_data->timestamp);
				else
					EMIT_SIGNAL_TIMESTAMP (XP_TE_NOTIFYBACK, sess, nick, nullptr, nullptr, 
												  nullptr, 0, tags_data->timestamp);
			}
		}
	}
}

bool
inbound_nameslist_end (const server &serv, const std::string & chan,
							  const message_tags_data *tags_data)
{
	if (chan == "*")
	{
		for (auto list = sess_list; list; list = g_slist_next(list))
		{
			auto sess = static_cast<session*>(list->data);
			if (sess->server == &serv)
			{
				sess->end_of_names = true;
				sess->ignore_names = false;
			}
		}
		return true;
	}
	auto sess = find_channel (serv, chan);
	if (sess)
	{
		sess->end_of_names = true;
		sess->ignore_names = false;
		return true;
	}
	return false;
}

static bool
check_autojoin_channels (server &serv)
{
	/* shouldn't really happen, the io tag is destroyed in server.c */
	if (!is_server (&serv))
	{
		return false;
	}

	std::vector<favchannel> sess_channels;      /* joined channels that are not in the favorites list */
	int i = 0;
	/* If there's a session (i.e. this is a reconnect), autojoin to everything that was open previously. */
	for (auto list = sess_list; list; list = g_slist_next(list))
	{
		auto sess = static_cast<session*>(list->data);

		if (sess->server == &serv)
		{
			if (sess->willjoinchannel[0] != 0)
			{
				strcpy (sess->waitchannel, sess->willjoinchannel);
				sess->willjoinchannel[0] = 0;

				auto fav = servlist_favchan_find (serv.network, sess->waitchannel, nullptr);	/* Is this channel in our favorites? */

				/* session->channelkey is initially unset for channels joined from the favorites. You have to fill them up manually from favorites settings. */
				if (fav)
				{
					/* session->channelkey is set if there was a key change during the session. In that case, use the session key, not the one from favorites. */
					if (fav->key && !strlen (sess->channelkey))
					{
						safe_strcpy (sess->channelkey, fav->key->c_str(), sizeof (sess->channelkey));
					}
				}

				/* for easier checks, ensure that favchannel->key is just nullptr when session->channelkey is empty i.e. '' */
				if (strlen (sess->channelkey))
				{
					sess_channels.push_back(favchannel{ sess->waitchannel, boost::make_optional<std::string>(sess->channelkey) });
				}
				else
				{
					sess_channels.push_back(favchannel{ sess->waitchannel, boost::none });
				}
				i++;
			}
		}
	}

	if (!sess_channels.empty())
	{
		serv.p_join_list (sess_channels);
	}
	else
	{
		/* If there's no session, just autojoin to favorites. */
		if (serv.favlist)
		{
			serv.p_join_list (serv.favlist);
			i++;

			/* FIXME this is not going to work and is not needed either. server_free() does the job already. */
			/* g_slist_free_full (serv.favlist, (GDestroyNotify) servlist_favchan_free); */
		}
	}

	serv.joindelay_tag = 0;
	fe_server_event(&serv, fe_serverevents::LOGGEDIN, i);
	return false;
}

void
inbound_next_nick (session *sess, char *nick, int error,
						 const message_tags_data *tags_data)
{
	char *newnick;
	server *serv = sess->server;
	ircnet *net;
	glib_string newnick_ptr;
	serv->nickcount++;

	switch (serv->nickcount)
	{
	case 2:
		newnick = prefs.hex_irc_nick2;
		net = serv->network;
		/* use network specific "Second choice"? */
		if (net && !(net->flags & FLAG_USE_GLOBAL) && net->nick2)
		{
			newnick_ptr.reset(g_strdup(net->nick2->c_str()));
			newnick = newnick_ptr.get();
		}
		serv->p_change_nick (newnick);
		if (error)
		{
			EMIT_SIGNAL_TIMESTAMP (XP_TE_NICKERROR, sess, nick, newnick, nullptr, nullptr,
										  0, tags_data->timestamp);
		}
		else
		{
			EMIT_SIGNAL_TIMESTAMP (XP_TE_NICKCLASH, sess, nick, newnick, nullptr, nullptr,
										  0, tags_data->timestamp);
		}
		break;

	case 3:
		serv->p_change_nick (prefs.hex_irc_nick3);
		if (error)
		{
			EMIT_SIGNAL_TIMESTAMP (XP_TE_NICKERROR, sess, nick, prefs.hex_irc_nick3,
										  nullptr, nullptr, 0, tags_data->timestamp);
		}
		else
		{
			EMIT_SIGNAL_TIMESTAMP (XP_TE_NICKCLASH, sess, nick, prefs.hex_irc_nick3,
										  nullptr, nullptr, 0, tags_data->timestamp);
		}
		break;

	default:
		EMIT_SIGNAL_TIMESTAMP (XP_TE_NICKFAIL, sess, nullptr, nullptr, nullptr, nullptr, 0,
									  tags_data->timestamp);
	}
}


static void
dns_addr_callback (GObject *obj, GAsyncResult *result, gpointer user_data)
{
	GResolver *resolver = G_RESOLVER(obj);
	session *sess = (session*)user_data;
	gchar *addr;

	g_return_if_fail (is_session(sess));

	addr = g_resolver_lookup_by_address_finish (resolver, result, nullptr);
	if (addr)
		PrintTextf (sess, _("Resolved to %s"), addr);
	else
		PrintText (sess, _("Not found"));
}

static void
dns_name_callback (GObject *obj, GAsyncResult *result, gpointer user_data)
{
	GResolver *resolver = G_RESOLVER(obj);
	session *sess = (session*)user_data;
	GList* addrs;
	gchar* addr;
	GList* list;

	g_return_if_fail (is_session (sess));

	addrs = g_resolver_lookup_by_name_finish (resolver, result, nullptr);
	if (addrs)
	{
		PrintText (sess, _("Resolved to:"));

		for (list = g_list_first (addrs); list; list = g_list_next (list))
		{
			addr = g_inet_address_to_string (static_cast<GInetAddress*>(list->data));
			PrintTextf (sess, "    %s", addr);
		}

		g_resolver_free_addresses (addrs);
	}
	else
		PrintText (sess, _("Not found"));
}

void
do_dns (session *sess, const char *nick, const char *host,
		const message_tags_data *tags_data)
{
	GResolver *res = g_resolver_get_default ();
	GInetAddress *addr;
	const char *po;

	po = strrchr (host, '@');
	if (po)
		host = po + 1;

	if (nick)
	{
		std::string mutable_nick(nick);
		std::string mutable_host(host);
		EMIT_SIGNAL_TIMESTAMP(XP_TE_RESOLVINGUSER, sess, &mutable_nick[0], &mutable_host[0], nullptr, nullptr, 0,
			tags_data->timestamp);
	}

	PrintTextf (sess, _("Looking up %s..."), host);

	addr = g_inet_address_new_from_string (host);
	if (addr)
		g_resolver_lookup_by_address_async (res, addr, nullptr, dns_addr_callback, sess);
	else
		g_resolver_lookup_by_name_async (res, host, nullptr, dns_name_callback, sess);
}

static void
set_default_modes (server &serv)
{
	char modes[8];

	modes[0] = '+';
	modes[1] = '\0';

	if (prefs.hex_irc_wallops)
		strcat (modes, "w");
	if (prefs.hex_irc_servernotice)
		strcat (modes, "s");
	if (prefs.hex_irc_invisible)
		strcat (modes, "i");
	if (prefs.hex_irc_hidehost)
		strcat (modes, "x");

	if (modes[1] != '\0')
	{
		serv.p_mode (serv.nick, modes);
	}
}

void
inbound_login_start (session *sess, char *nick, char *servname,
							const message_tags_data *tags_data)
{
	if (!sess->server)
		throw std::runtime_error("Invalid server reference");
	inbound_newnick (*(sess->server), sess->server->nick, nick, true, tags_data);
	sess->server->set_name(servname);
	/*if (sess->type == session::SESS_SERVER)
		log_open_or_close (sess);*/
	/* reset our away status */
	if (sess->server->reconnect_away)
	{
		handle_command (sess->server->server_session, "away", false);
		sess->server->reconnect_away = false;
	}
}

static void
inbound_set_all_away_status (const server &serv, const char *nick, bool away)
{
	for (auto list = sess_list; list; list = g_slist_next(list))
	{
		auto sess = static_cast<session*>(list->data);
		if (sess->server == &serv)
			userlist_set_away (sess, nick, away);
	}
}

void
inbound_uaway (server &serv, const message_tags_data *tags_data)
{
	serv.is_away = true;
	serv.away_time = time (nullptr);
	fe_set_away (serv);

	inbound_set_all_away_status (serv, serv.nick, true);
}

void
inbound_uback (server &serv, const message_tags_data *tags_data)
{
	serv.is_away = false;
	serv.reconnect_away = false;
	fe_set_away (serv);

	inbound_set_all_away_status (serv, serv.nick, false);
}

void
inbound_foundip (session *sess, char *ip, const message_tags_data *tags_data)
{
	struct hostent *HostAddr;

	HostAddr = gethostbyname (ip);
	if (HostAddr)
	{
		prefs.dcc_ip = ((struct in_addr *) HostAddr->h_addr)->s_addr;
		EMIT_SIGNAL_TIMESTAMP (XP_TE_FOUNDIP, sess->server->server_session,
									  inet_ntoa (*((struct in_addr *) HostAddr->h_addr)),
									  nullptr, nullptr, nullptr, 0, tags_data->timestamp);
	}
}

void
inbound_user_info_start (session *sess, const char *nick,
								 const message_tags_data *tags_data)
{
	/* set away to false now, 301 may turn it back on */
	inbound_set_all_away_status (*(sess->server), nick, false);
}

/* reporting new information found about this user. chan may be nullptr.
 * away may be 0xff to indicate UNKNOWN. */

void
inbound_user_info (session *sess, char *chan, char *user, char *host,
						 char *servname, char *nick, char *realname,
						 char *account, unsigned int away,
						 const message_tags_data *tags_data)
{
	server *serv = sess->server;
	glib_string uhost;

	if (user && host)
	{
		uhost.reset(static_cast<char*>(g_malloc (strlen (user) + strlen (host) + 2)));
		sprintf (uhost.get(), "%s@%s", user, host);
	}

	if (chan)
	{
		auto who_sess = find_channel (*serv, chan);
		if (who_sess)
			userlist_add_hostname (who_sess, nick, uhost.get(), realname, servname, account, away);
		else
		{
			if (serv->doing_dns && nick && host)
				do_dns (sess, nick, host, tags_data);
		}
	}
	else
	{
		/* came from WHOIS, not channel specific */
		for (auto list = sess_list; list; list = g_slist_next(list))
		{
			auto sess = static_cast<session*>(list->data);
			if (sess->type == session::SESS_CHANNEL && sess->server == serv)
			{
				userlist_add_hostname (sess, nick, uhost.get(), realname, servname, account, away);
			}
		}
	}
}

bool inbound_banlist (session *sess, time_t stamp, char *chan, char *mask, 
					  char *banner, int rplcode, const message_tags_data *tags_data)
{
	char *time_str = ctime (&stamp);
	server *serv = sess->server;
	char *nl;

	if (stamp <= 0)
	{
		time_str = "";
	}
	else
	{
		if ((nl = strchr (time_str, '\n')))
			*nl = 0;
	}

	sess = find_channel (*serv, chan);
	if (!sess)
	{
		sess = serv->front_session;
		goto nowindow;
	}

	if (!fe_add_ban_list (sess, mask, banner, time_str, rplcode))
	{
nowindow:

		EMIT_SIGNAL_TIMESTAMP (XP_TE_BANLIST, sess, chan, mask, banner, time_str,
									  0, tags_data->timestamp);
		return true;
	}

	return true;
}

/* execute 1 end-of-motd command */

static bool
inbound_exec_eom_cmd (const std::string & str, session *sess)
{
	auto cmd = command_insert_vars (sess, (str[0] == '/') ? str.substr(1) : str);
	handle_command (sess, &cmd[0], true);

	return true;
}

static bool
inbound_nickserv_login (const server &serv)
{
	/* this could grow ugly, but let's hope there won't be new NickServ types */
	switch (serv.loginmethod)
	{
		case LOGIN_MSG_NICKSERV:
		case LOGIN_NICKSERV:
		case LOGIN_CHALLENGEAUTH:
#if 0
		case LOGIN_NS:
		case LOGIN_MSG_NS:
		case LOGIN_AUTH:
#endif
			return true;
		default:
			return false;
	}
}

void
inbound_login_end (session *sess, char *text, const message_tags_data *tags_data)
{
	if (!sess->server)
		throw std::runtime_error("Invalid server reference");
	server &serv = *(sess->server);

	if (!serv.end_of_motd)
	{
		if (prefs.hex_dcc_ip_from_server && serv.use_who)
		{
			serv.skip_next_userhost = true;
			serv.p_get_ip_uh (serv.nick);	/* sends USERHOST mynick */
		}
		set_default_modes (serv);

		if (serv.network)
		{
			/* there may be more than 1, separated by \n */
			for(auto cmdlist = serv.network->commandlist; cmdlist; cmdlist = g_slist_next(cmdlist))
			{
				auto cmd = static_cast<commandentry*>(cmdlist->data);
				inbound_exec_eom_cmd (cmd->command, sess);
			}

			/* send nickserv password */
			if (serv.network->pass && inbound_nickserv_login (serv))
			{
				serv.p_ns_identify (serv.network->pass);
			}
		}

		/* wait for join if command or nickserv set */
		if (serv.network && prefs.hex_irc_join_delay
			&& ((serv.network->pass && inbound_nickserv_login (serv))
				|| serv.network->commandlist))
		{
			serv.joindelay_tag = fe_timeout_add(prefs.hex_irc_join_delay * 1000, (GSourceFunc)check_autojoin_channels, &serv);
		}
		else
		{
			check_autojoin_channels (serv);
		}

		if (serv.supports_watch || serv.supports_monitor)
		{
			notify_send_watches (serv);
		}

		serv.end_of_motd = true;
	}

	if (prefs.hex_irc_skip_motd && !serv.motd_skipped)
	{
		serv.motd_skipped = true;
		EMIT_SIGNAL_TIMESTAMP (XP_TE_MOTDSKIP, serv.server_session, nullptr, nullptr,
									  nullptr, nullptr, 0, tags_data->timestamp);
		return;
	}

	EMIT_SIGNAL_TIMESTAMP (XP_TE_MOTD, serv.server_session, text, nullptr, nullptr,
								  nullptr, 0, tags_data->timestamp);
}

void
inbound_identified (server &serv)	/* 'MODE +e MYSELF' on freenode */
{
	if (serv.joindelay_tag)
	{
		/* stop waiting, just auto JOIN now */
		fe_timeout_remove (serv.joindelay_tag);
		serv.joindelay_tag = 0;
		check_autojoin_channels (serv);
	}
}

void
inbound_cap_ack (server &serv, char *nick, char *extensions,
					  const message_tags_data *tags_data)
{
	EMIT_SIGNAL_TIMESTAMP (XP_TE_CAPACK, serv.server_session, nick, extensions,
								  nullptr, nullptr, 0, tags_data->timestamp);

	if (strstr (extensions, "identify-msg") != nullptr)
	{
		serv.have_idmsg = true;
	}

	if (strstr (extensions, "multi-prefix") != nullptr)
	{
		serv.have_namesx = true;
	}

	if (strstr (extensions, "away-notify") != nullptr)
	{
		serv.have_awaynotify = true;
	}

	if (strstr (extensions, "account-notify") != nullptr)
	{
		serv.have_accnotify = true;
	}
					
	if (strstr (extensions, "extended-join") != nullptr)
	{
		serv.have_extjoin = true;
	}

	if (strstr (extensions, "userhost-in-names") != nullptr)
	{
		serv.have_uhnames = true;
	}

	if (strstr (extensions, "server-time") != nullptr)
	{
		serv.have_server_time = true;
	}

	if (strstr (extensions, "sasl") != nullptr)
	{
		serv.have_sasl = true;
		serv.sent_saslauth = false;

#ifdef USE_OPENSSL
		if (serv.loginmethod == LOGIN_SASLEXTERNAL)
		{
			serv.sasl_mech = MECH_EXTERNAL;
			tcp_send (serv, "AUTHENTICATE EXTERNAL\r\n");
		}
		else
		{
			/* default to most secure, it will fallback if not supported */
			serv.sasl_mech = MECH_AES;
			tcp_send (serv, "AUTHENTICATE DH-AES\r\n");
		}
#else
		serv.sasl_mech = MECH_PLAIN;
		tcp_send (serv, "AUTHENTICATE PLAIN\r\n");
#endif
	}
}

void
inbound_cap_ls (server &serv, char *nick, char *extensions_str,
					 const message_tags_data *tags_data)
{
	char buffer[256];	/* buffer for requesting capabilities and emitting the signal */
	bool want_cap = false; /* format the CAP REQ string based on previous capabilities being requested or not */
	bool want_sasl = false; /* CAP END shouldn't be sent when SASL is requested, it needs further responses */
	char **extensions;
	int i;

	EMIT_SIGNAL_TIMESTAMP (XP_TE_CAPLIST, serv.server_session, nick,
								  extensions_str, nullptr, nullptr, 0, tags_data->timestamp);

	extensions = g_strsplit (extensions_str, " ", 0);

	strcpy (buffer, "CAP REQ :");

	for (i=0; extensions[i]; i++)
	{
		const char *extension = extensions[i];

		if (!strcmp (extension, "identify-msg"))
		{
			strcat (buffer, "identify-msg ");
			want_cap = true;
		}
		if (!strcmp (extension, "multi-prefix"))
		{
			strcat (buffer, "multi-prefix ");
			want_cap = true;
		}
		if (!strcmp (extension, "away-notify"))
		{
			strcat (buffer, "away-notify ");
			want_cap = true;
		}
		if (!strcmp (extension, "account-notify"))
		{
			strcat (buffer, "account-notify ");
			want_cap = true;
		}
		if (!strcmp (extension, "extended-join"))
		{
			strcat (buffer, "extended-join ");
			want_cap = true;
		}
		if (!strcmp (extension, "userhost-in-names"))
		{
			strcat (buffer, "userhost-in-names ");
			want_cap = true;
		}

		/* bouncers can prefix a name space to the extension so we should use.
		 * znc <= 1.0 uses "znc.in/server-time" and newer use "znc.in/server-time-iso".
		 */
		if (!strcmp (extension, "znc.in/server-time-iso"))
		{
			strcat (buffer, "znc.in/server-time-iso ");
			want_cap = true;
		}
		if (!strcmp (extension, "znc.in/server-time"))
		{
			strcat (buffer, "znc.in/server-time ");
			want_cap = true;
		}
		if (prefs.hex_irc_cap_server_time
			 && !strcmp (extension, "server-time"))
		{
			strcat (buffer, "server-time ");
			want_cap = true;
		}
		
		/* if the SASL password is set AND auth mode is set to SASL, request SASL auth */
		if (!strcmp (extension, "sasl")
			&& ((serv.loginmethod == LOGIN_SASL && strlen (serv.password) != 0)
			|| (serv.loginmethod == LOGIN_SASLEXTERNAL && serv.have_cert)))
		{
			strcat (buffer, "sasl ");
			want_cap = true;
			want_sasl = true;
		}
	}

	g_strfreev (extensions);

	if (want_cap)
	{
		/* buffer + 9 = emit buffer without "CAP REQ :" */
		EMIT_SIGNAL_TIMESTAMP (XP_TE_CAPREQ, serv.server_session,
									  buffer + 9, nullptr, nullptr, nullptr, 0,
									  tags_data->timestamp);
		tcp_sendf (serv, "%s\r\n", g_strchomp (buffer));
	}
	if (!want_sasl)
	{
		/* if we use SASL, CAP END is dealt via raw numerics */
		serv.sent_capend = true;
		tcp_send(serv, "CAP END\r\n");
	}
}

void
inbound_cap_nak (server &serv, const message_tags_data *tags_data)
{
	serv.sent_capend = true;
	tcp_send (serv, "CAP END\r\n");
}

void
inbound_cap_list (server &serv, char *nick, char *extensions,
						const message_tags_data *tags_data)
{
	EMIT_SIGNAL_TIMESTAMP (XP_TE_CAPACK, serv.server_session, nick, extensions,
								  nullptr, nullptr, 0, tags_data->timestamp);
}

static const char *sasl_mechanisms[] =
{
	"PLAIN",
	"DH-BLOWFISH",
	"DH-AES",
	"EXTERNAL"
};

void
inbound_sasl_supportedmechs (server &serv, char *list)
{
	if (serv.sasl_mech != MECH_EXTERNAL)
	{
		/* Use most secure one supported */
		for (int i = MECH_AES; i >= MECH_PLAIN; i--)
		{
			if (strstr (list, sasl_mechanisms[i]) != nullptr)
			{
				serv.sasl_mech = i;
				serv.retry_sasl = true;
				tcp_sendf (serv, "AUTHENTICATE %s\r\n", sasl_mechanisms[i]);
				return;
			}
		}
	}

	/* Abort, none supported */
	serv.sent_saslauth = true;
	tcp_send(serv, "AUTHENTICATE *\r\n");
	return;
}

void
inbound_sasl_authenticate (server &serv, char *data)
{
		ircnet *net = serv.network;
		char *user;
		const char *mech = sasl_mechanisms[serv.sasl_mech];

		/* Got a list of supported mechanisms from inspircd */
		if (strchr (data, ',') != nullptr)
		{
			inbound_sasl_supportedmechs (serv, data);
			return;
		}

		if (net->user && !(net->flags & FLAG_USE_GLOBAL))
			user = net->user;
		else
			user = prefs.hex_irc_user_name;

		std::string pass;
		switch (serv.sasl_mech)
		{
		case MECH_PLAIN:
			pass = auth::sasl::encode_sasl_pass_plain (user, serv.password);
			break;
#ifdef USE_OPENSSL
		case MECH_BLOWFISH:
			pass = auth::sasl::encode_sasl_pass_blowfish (user, serv.password, data);
			break;
		case MECH_AES:
			pass = auth::sasl::encode_sasl_pass_aes (user, serv.password, data);
			break;
		case MECH_EXTERNAL:
			pass = "+";
			break;
#endif
		}

		if (pass.empty())
		{
			/* something went wrong abort */
			serv.sent_saslauth = true; /* prevent trying PLAIN */
			tcp_send (serv, "AUTHENTICATE *\r\n");
			return;
		}

		serv.sent_saslauth = true;
		tcp_sendf (serv, "AUTHENTICATE %s\r\n", pass.c_str());

		
		EMIT_SIGNAL_TIMESTAMP (XP_TE_SASLAUTH, serv.server_session, user, (char*)mech,
								nullptr,	nullptr,	0,	0);
}

bool inbound_sasl_error (server &serv)
{
	if (serv.retry_sasl && !serv.sent_saslauth)
		return true;

	/* If server sent 904 before we sent password,
		* mech not support so fallback to next mech */
	if (!serv.sent_saslauth && serv.sasl_mech != MECH_EXTERNAL && serv.sasl_mech != MECH_PLAIN)
	{
		serv.sasl_mech -= 1;
		tcp_sendf (serv, "AUTHENTICATE %s\r\n", sasl_mechanisms[serv.sasl_mech]);
		return true;
	}
	return false;
}
