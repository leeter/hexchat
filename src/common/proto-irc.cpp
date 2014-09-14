/* X-Chat
 * Copyright (C) 2002 Peter Zelezny.
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

/* IRC RFC1459(+commonly used extensions) protocol implementation */

#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstdarg>

#ifndef WIN32
#include <unistd.h>
#endif

#include "hexchat.hpp"
#include "proto-irc.hpp"
#include "ctcp.hpp"
#include "fe.hpp"
#include "ignore.hpp"
#include "inbound.hpp"
#include "modes.hpp"
#include "notify.hpp"
#include "plugin.h"
#include "server.hpp"
#include "text.hpp"
#include "outbound.hpp"
#include "util.hpp"
#include "hexchatc.hpp"
#include "url.hpp"
#include "servlist.hpp"


void
server::p_login(const std::string& user, const std::string& realname)
{
	tcp_sendf (this, "CAP LS\r\n");		/* start with CAP LS as Charybdis sasl.txt suggests */
	this->sent_capend = FALSE;	/* track if we have finished */

	if (this->password[0] && this->loginmethod == LOGIN_PASS)
	{
		tcp_sendf (this, "PASS %s\r\n", this->password);
	}

	tcp_sendf (this,
				  "NICK %s\r\n"
				  "USER %s %s %s :%s\r\n",
                  this->nick, user.c_str(), user.c_str(), this->servername, realname.c_str());
}

static void
irc_nickserv(server *serv, const std::string& cmd, const std::string& arg1, const std::string& arg2, const std::string& arg3)
{
	/* are all ircd authors idiots? */
	switch (serv->loginmethod)
	{
		case LOGIN_MSG_NICKSERV:
            tcp_sendf(serv, "PRIVMSG NICKSERV :%s %s%s%s\r\n", cmd.c_str(), arg1.c_str(), arg2.c_str(), arg3.c_str());
			break;
		case LOGIN_NICKSERV:
            tcp_sendf(serv, "NICKSERV %s %s%s%s\r\n", cmd.c_str(), arg1.c_str(), arg2.c_str(), arg3.c_str());
			break;
		default: /* This may not work but at least it tries something when using /id or /ghost cmd */
            tcp_sendf(serv, "NICKSERV %s %s%s%s\r\n", cmd.c_str(), arg1.c_str(), arg2.c_str(), arg3.c_str());
			break;
#if 0
		case LOGIN_MSG_NS:
            tcp_sendf (serv, "PRIVMSG NS :%s %s%s%s\r\n", cmd.c_str(), arg1.c_str(), arg2.c_str(), arg3.c_str());
			break;
		case LOGIN_NS:
            tcp_sendf (serv, "NS %s %s%s%s\r\n", cmd.c_str(), arg1.c_str(), arg2.c_str(), arg3.c_str());
			break;
		case LOGIN_AUTH:
			/* why couldn't QuakeNet implement one of the existing ones? */
            tcp_sendf (serv, "AUTH %s %s\r\n", arg1.c_str(), arg2.c_str());
			break;
#endif
	}
}

void
server::p_ns_identify(const std::string &pass)
{
	switch (this->loginmethod)
	{
		case LOGIN_CHALLENGEAUTH:
			tcp_sendf (this, "PRIVMSG %s :CHALLENGE\r\n", CHALLENGEAUTH_NICK);	/* request a challenge from Q */
			break;
#if 0
		case LOGIN_AUTH:
			irc_nickserv (this, "", this->nick, pass, "");
			break;
#endif
		default:
			irc_nickserv (this, "IDENTIFY", pass, "", "");
	}
}

void
server::p_ns_ghost(const std::string& usname, const std::string& pass)
{
	if (this->loginmethod != LOGIN_CHALLENGEAUTH)
	{
		irc_nickserv (this, "GHOST", usname, " ", pass);
	}
}

void
server::p_join(const std::string& channel, const std::string& key)
{
	if (!key.empty())
		tcp_sendf (this, "JOIN %s %s\r\n", channel.c_str(), key.c_str());
	else
		tcp_sendf (this, "JOIN %s\r\n", channel.c_str());
}

static void
irc_join_list_flush (server *serv, const std::string & channels, const std::string & keys, bool send_keys)
{
	if (send_keys)
	{
		tcp_sendf (serv, "JOIN %s %s\r\n", channels.c_str(), keys.c_str());	/* send the actual command */
	}
	else
	{
        tcp_sendf(serv, "JOIN %s\r\n", channels.c_str());	/* send the actual command */
	}
}

/* Join a whole list of channels & keys, split to multiple lines
 * to get around the 512 limit.
 */

void
server::p_join_list (GSList *favorites)
{
	bool first_item = true;										/* determine whether we add commas or not */
	bool send_keys = false;										/* if none of our channels have keys, we can omit the 'x' fillers altogether */
	int len = 9;											/* JOIN<space>channels<space>keys\r\n\0 */
	favchannel *fav;
    std::string chanlist;
    std::string keylist;
	GSList *favlist;

	favlist = favorites;

	while (favlist)
	{
		fav = static_cast<favchannel*>(favlist->data);

		len += fav->name.size();
		if (fav->key)
		{
            len += fav->key->size();
		}

		if (len >= 512)										/* command length exceeds the IRC hard limit, flush it and start from scratch */
		{
			irc_join_list_flush (this, chanlist, keylist, send_keys);

            chanlist.clear();
            keylist.clear();
			len = 9;
			first_item = true;									/* list dumped, omit commas once again */
			send_keys = false;									/* also omit keys until we actually find one */
		}

		if (!first_item)
		{
			/* This should be done before the length check, but channel names
			 * are already at least 2 characters long so it would trigger the
			 * flush anyway.
			 */
			len += 2;

			/* add separators but only if it's not the 1st element */
			chanlist.push_back(',');
			keylist.push_back(',');
		}

        chanlist += fav->name;

		if (fav->key)
		{
            keylist += *(fav->key);
			send_keys = true;
		}
		else
		{
			keylist.push_back('x');				/* 'x' filler for keyless channels so that our JOIN command is always well-formatted */
		}

		first_item = false;
		favlist = favlist->next;
	}

	irc_join_list_flush (this, chanlist, keylist, send_keys);
	g_slist_free (favlist);
}

void
server::p_join_list(const std::vector<favchannel> &favorites)
{
    bool first_item = true;										/* determine whether we add commas or not */
    bool send_keys = false;										/* if none of our channels have keys, we can omit the 'x' fillers altogether */
    int len = 9;											/* JOIN<space>channels<space>keys\r\n\0 */
    std::string chanlist;
    std::string keylist;

    for (const auto & fav : favorites)
    {
        len += fav.name.size();
        if (fav.key)
        {
            len += fav.key->size();
        }

        if (len >= 512)										/* command length exceeds the IRC hard limit, flush it and start from scratch */
        {
            irc_join_list_flush(this, chanlist, keylist, send_keys);

            chanlist.clear();
            keylist.clear();
            len = 9;
            first_item = true;									/* list dumped, omit commas once again */
            send_keys = false;									/* also omit keys until we actually find one */
        }

        if (!first_item)
        {
            /* This should be done before the length check, but channel names
            * are already at least 2 characters long so it would trigger the
            * flush anyway.
            */
            len += 2;

            /* add separators but only if it's not the 1st element */
            chanlist.push_back(',');
            keylist.push_back(',');
        }

        chanlist += fav.name;

        if (fav.key)
        {
            keylist += *(fav.key);
            send_keys = true;
        }
        else
        {
            keylist.push_back('x');				/* 'x' filler for keyless channels so that our JOIN command is always well-formatted */
        }

        first_item = false;
    }

    irc_join_list_flush(this, chanlist, keylist, send_keys);
}

void
server::p_part(const std::string& channel, const std::string & reason)
{
	if (!reason.empty())
		tcp_sendf (this, "PART %s :%s\r\n", channel.c_str(), reason.c_str());
	else
		tcp_sendf (this, "PART %s\r\n", channel.c_str());
}

void
server::p_quit (const std::string & reason)
{
	if (!reason.empty())
		tcp_sendf (this, "QUIT :%s\r\n", reason.c_str());
	else
		tcp_send_len (this, "QUIT\r\n", 6);
}

void
server::p_set_back ()
{
	tcp_send_len (this, "AWAY\r\n", 6);
}

void
server::p_set_away (const std::string & reason)
{
	tcp_sendf (this, "AWAY :%s\r\n", reason.empty()? " " : reason.c_str());
}

// TODO: split appropriately
void
server::p_ctcp(const std::string & to, const std::string & msg)
{
	tcp_sendf (this, "PRIVMSG %s :\001%s\001\r\n", to.c_str(), msg.c_str());
}

void
server::p_nctcp(const std::string & to, const std::string & msg)
{
	tcp_sendf (this, "NOTICE %s :\001%s\001\r\n", to.c_str(), msg.c_str());
}

void
server::p_cycle(const std::string& channel, const std::string& key)
{
    tcp_sendf(this, "PART %s\r\nJOIN %s %s\r\n", channel.c_str(), channel.c_str(), key.c_str());
}

void
server::p_kick(const std::string& channel, const std::string &nick, const std::string & reason)
{
	if (!reason.empty())
		tcp_sendf (this, "KICK %s %s :%s\r\n", channel.c_str(), nick.c_str(), reason.c_str());
	else
		tcp_sendf (this, "KICK %s %s\r\n", channel.c_str(), nick.c_str());
}

void
server::p_invite (const std::string & channel, const std::string & nick)
{
	tcp_sendf (this, "INVITE %s %s\r\n", nick.c_str(), channel.c_str());
}

void
server::p_mode(const std::string & target, const std::string & mode)
{
	tcp_sendf (this, "MODE %s %s\r\n", target.c_str(), mode.c_str());
}

/* find channel info when joined */

void
server::p_join_info (const std::string& channel)
{
	tcp_sendf (this, "MODE %s\r\n", channel.c_str());
}

/* initiate userlist retreival */

void
server::p_user_list(const std::string & channel)
{
    if (this->have_whox)
        tcp_sendf(this, "WHO %s %%chtsunfra,152\r\n", channel.c_str());
    else
        tcp_sendf(this, "WHO %s\r\n", channel.c_str());
}

/* userhost */
//irc_userhost
void
server::p_get_ip_uh(const std::string & nick)
{
	tcp_sendf (this, "USERHOST %s\r\n", nick.c_str());
}

// TODO: get rid of?
//void
//server::p_away_status(const std::string & channel)
//{
//	if (this->have_whox)
//		tcp_sendf (this, "WHO %s %%chtsunfra,152\r\n", channel.c_str());
//	else
//        tcp_sendf(this, "WHO %s\r\n", channel.c_str());
//}

/*static void
irc_get_ip (server *serv, char *nick)
{
	tcp_sendf (serv, "WHO %s\r\n", nick);
}*/


/*
 *  Command: WHOIS
 *     Parameters: [<server>] <nickmask>[,<nickmask>[,...]]
 */
void
server::p_whois (const std::string& nicks)
{
	tcp_sendf (this, "WHOIS %s\r\n", nicks.c_str());
}

void
server::p_message(const std::string & channel, const std::string & text)
{
	tcp_sendf (this, "PRIVMSG %s :%s\r\n", channel.c_str(), text.c_str());
}

// TODO: handle splitting it
void
server::p_action(const std::string & channel, const std::string & act)
{
	tcp_sendf (this, "PRIVMSG %s :\001ACTION %s\001\r\n", channel.c_str(), act.c_str());
}

void
server::p_notice(const std::string & channel, const std::string & text)
{
	tcp_sendf (this, "NOTICE %s :%s\r\n", channel.c_str(), text.c_str());
}

void
server::p_topic(const std::string & channel, const char *topic)
{
    if (topic)
        tcp_sendf(this, "TOPIC %s :\r\n", channel.c_str());
	else if (topic[0])
		tcp_sendf (this, "TOPIC %s :%s\r\n", channel.c_str(), topic);
	else
		tcp_sendf (this, "TOPIC %s\r\n", channel.c_str());
}

void
server::p_list_channels(const std::string & arg, int min_users)
{
	if (!arg.empty())
	{
		tcp_sendf (this, "LIST %s\r\n", arg.c_str());
		return;
	}

	if (this->use_listargs)
		tcp_sendf (this, "LIST >%d,<10000\r\n", min_users - 1);
	else
		tcp_send_len (this, "LIST\r\n", 6);
}

void
server::p_names(const std::string & channel)
{
	tcp_sendf (this, "NAMES %s\r\n", channel.c_str());
}

void
server::p_change_nick(const std::string & new_nick)
{
	tcp_sendf (this, "NICK %s\r\n", new_nick.c_str());
}

void
server::p_ping(const std::string & to, const std::string & timestring)
{
	if (!to.empty())
		tcp_sendf (this, "PRIVMSG %s :\001PING %s\001\r\n", to.c_str(), timestring.c_str());
	else
		tcp_sendf (this, "PING %s\r\n", timestring.c_str());
}

bool
server::p_raw(const std::string &raw)
{
	char tbuf[4096];
	if (!raw.empty())
	{
		if (raw.size() < sizeof (tbuf) - 3)
		{
            auto len = snprintf(tbuf, sizeof(tbuf), "%s\r\n", raw.c_str());
			tcp_send_len (this, tbuf, len);
		} else
		{
			tcp_send_len (this, raw.c_str(), raw.size());
			tcp_send_len (this, "\r\n", 2);
		}
		return true;
	}
	return false;
}

/* ============================================================== */
/* ======================= IRC INPUT ============================ */
/* ============================================================== */


static void
channel_date (session *sess, char *chan, char *timestr,
				  const message_tags_data *tags_data)
{
	time_t timestamp = (time_t) atol (timestr);
	char *tim = ctime (&timestamp);
	tim[24] = 0;	/* get rid of the \n */
	EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANDATE, sess, chan, tim, NULL, NULL, 0,
								  tags_data->timestamp);
}

static void
process_numeric (session * sess, int n,
					  char *word[], char *word_eol[], char *text,
					  const message_tags_data *tags_data)
{
	server *serv = sess->server;
	/* show whois is the server tab */
	session *whois_sess = serv->server_session;
	
	/* unless this setting is on */
	if (prefs.hex_irc_whois_front)
		whois_sess = serv->front_session;

	switch (n)
	{
	case 1:
		inbound_login_start (sess, word[3], word[1], tags_data);
		/* if network is PTnet then you must get your IP address
			from "001" server message */
		if ((strncmp(word[7], "PTnet", 5) == 0) &&
			(strncmp(word[8], "IRC", 3) == 0) &&
			(strncmp(word[9], "Network", 7) == 0) &&
			(strrchr(word[10], '@') != NULL))
		{
			serv->use_who = FALSE;
			if (prefs.hex_dcc_ip_from_server)
				inbound_foundip (sess, strrchr(word[10], '@')+1, tags_data);
		}

		goto def;

	case 4:	/* check the ircd type */
		serv->use_listargs = FALSE;
		serv->modes_per_line = 3;		/* default to IRC RFC */
		if (strncmp (word[5], "bahamut", 7) == 0)				/* DALNet */
		{
			serv->use_listargs = TRUE;		/* use the /list args */
		} else if (strncmp (word[5], "u2.10.", 6) == 0)		/* Undernet */
		{
			serv->use_listargs = TRUE;		/* use the /list args */
			serv->modes_per_line = 6;		/* allow 6 modes per line */
		} else if (strncmp (word[5], "glx2", 4) == 0)
		{
			serv->use_listargs = TRUE;		/* use the /list args */
		}
		goto def;

	case 5:
		inbound_005 (serv, word, tags_data);
		goto def;

	case 263:	/*Server load is temporarily too heavy */
		if (fe_is_chanwindow (sess->server))
		{
			fe_chan_list_end (sess->server);
			fe_message (word_eol[4], FE_MSG_ERROR);
		}
		goto def;

	case 301:
		inbound_away (serv, word[4],
						  (word_eol[5][0] == ':') ? word_eol[5] + 1 : word_eol[5],
						  tags_data);
		break;

	case 302:
		if (serv->skip_next_userhost)
		{
			char *eq = strchr (word[4], '=');
			if (eq)
			{
				*eq = 0;
				if (!serv->p_cmp (word[4] + 1, serv->nick))
				{
					char *at = strrchr (eq + 1, '@');
					if (at)
						inbound_foundip (sess, at + 1, tags_data);
				}
			}

			serv->skip_next_userhost = FALSE;
			break;
		}
		else goto def;

	case 303:
		word[4]++;
		notify_markonline (serv, word, tags_data);
		break;

	case 305:
		inbound_uback (serv, tags_data);
		goto def;

	case 306:
		inbound_uaway (serv, tags_data);
		goto def;

	case 312:
		if (!serv->skip_next_whois)
			EMIT_SIGNAL_TIMESTAMP (XP_TE_WHOIS3, whois_sess, word[4], word_eol[5],
										  NULL, NULL, 0, tags_data->timestamp);
		else
			inbound_user_info (sess, NULL, NULL, NULL, word[5], word[4], NULL, NULL,
									 0xff, tags_data);
		break;

	case 311:	/* WHOIS 1st line */
		serv->inside_whois = 1;
		inbound_user_info_start (sess, word[4], tags_data);
		if (!serv->skip_next_whois)
			EMIT_SIGNAL_TIMESTAMP (XP_TE_WHOIS1, whois_sess, word[4], word[5],
										  word[6], word_eol[8] + 1, 0, tags_data->timestamp);
		else
			inbound_user_info (sess, NULL, word[5], word[6], NULL, word[4],
									 word_eol[8][0] == ':' ? word_eol[8] + 1 : word_eol[8],
									 NULL, 0xff, tags_data);
		break;

	case 314:	/* WHOWAS */
		inbound_user_info_start (sess, word[4], tags_data);
		EMIT_SIGNAL_TIMESTAMP (XP_TE_WHOIS1, whois_sess, word[4], word[5],
									  word[6], word_eol[8] + 1, 0, tags_data->timestamp);
		break;

	case 317:
		if (!serv->skip_next_whois)
		{
			time_t timestamp = (time_t) atol (word[6]);
			long idle = atol (word[5]);
			char *tim;
			char outbuf[64];

			snprintf (outbuf, sizeof (outbuf),
						"%02ld:%02ld:%02ld", idle / 3600, (idle / 60) % 60,
						idle % 60);
			if (timestamp == 0)
				EMIT_SIGNAL_TIMESTAMP (XP_TE_WHOIS4, whois_sess, word[4],
											  outbuf, NULL, NULL, 0, tags_data->timestamp);
			else
			{
				tim = ctime (&timestamp);
				tim[19] = 0; 	/* get rid of the \n */
				EMIT_SIGNAL_TIMESTAMP (XP_TE_WHOIS4T, whois_sess, word[4],
											  outbuf, tim, NULL, 0, tags_data->timestamp);
			}
		}
		break;

	case 318:	/* END OF WHOIS */
		if (!serv->skip_next_whois)
			EMIT_SIGNAL_TIMESTAMP (XP_TE_WHOIS6, whois_sess, word[4], NULL,
										  NULL, NULL, 0, tags_data->timestamp);
		serv->skip_next_whois = 0;
		serv->inside_whois = 0;
		break;

	case 313:
	case 319:
		if (!serv->skip_next_whois)
			EMIT_SIGNAL_TIMESTAMP (XP_TE_WHOIS2, whois_sess, word[4],
										  word_eol[5] + 1, NULL, NULL, 0,
										  tags_data->timestamp);
		break;

	case 307:	/* dalnet version */
	case 320:	/* :is an identified user */
		if (!serv->skip_next_whois)
			EMIT_SIGNAL_TIMESTAMP (XP_TE_WHOIS_ID, whois_sess, word[4],
										  word_eol[5] + 1, NULL, NULL, 0,
										  tags_data->timestamp);
		break;

	case 321:
		if (!fe_is_chanwindow (sess->server))
			EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANLISTHEAD, serv->server_session, NULL,
										  NULL, NULL, NULL, 0, tags_data->timestamp);
		break;

	case 322:
		if (fe_is_chanwindow (sess->server))
		{
			fe_add_chan_list (sess->server, word[4], word[5], word_eol[6] + 1);
		} else
		{
			PrintTextTimeStampf (serv->server_session, tags_data->timestamp,
										"%-16s %-7d %s\017\n", word[4], atoi (word[5]),
										word_eol[6] + 1);
		}
		break;

	case 323:
		if (!fe_is_chanwindow (sess->server))
			EMIT_SIGNAL_TIMESTAMP (XP_TE_SERVTEXT, serv->server_session, text, 
										  word[1], word[2], NULL, 0, tags_data->timestamp);
		else
			fe_chan_list_end (sess->server);
		break;

	case 324:
		sess = find_channel (serv, word[4]);
		if (!sess)
			sess = serv->server_session;
		if (sess->ignore_mode)
			sess->ignore_mode = FALSE;
		else
			EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANMODES, sess, word[4], word_eol[5],
										  NULL, NULL, 0, tags_data->timestamp);
		fe_update_mode_buttons (sess, 'c', '-');
		fe_update_mode_buttons (sess, 'r', '-');
		fe_update_mode_buttons (sess, 't', '-');
		fe_update_mode_buttons (sess, 'n', '-');
		fe_update_mode_buttons (sess, 'i', '-');
		fe_update_mode_buttons (sess, 'm', '-');
		fe_update_mode_buttons (sess, 'l', '-');
		fe_update_mode_buttons (sess, 'k', '-');
		handle_mode (serv, word, word_eol, "", TRUE, tags_data);
		break;

	case 328: /* channel url */
		sess = find_channel (serv, word[4]);
		if (sess)
		{
			EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANURL, sess, word[4], word[5] + 1,
									NULL, NULL, 0, tags_data->timestamp); 
		}
		break;

	case 329:
		sess = find_channel (serv, word[4]);
		if (sess)
		{
			if (sess->ignore_date)
				sess->ignore_date = FALSE;
			else
				channel_date (sess, word[4], word[5], tags_data);
		}
		break;

	case 330:
		if (!serv->skip_next_whois)
			EMIT_SIGNAL_TIMESTAMP (XP_TE_WHOIS_AUTH, whois_sess, word[4],
										  word_eol[6] + 1, word[5], NULL, 0,
										  tags_data->timestamp);
		inbound_user_info (sess, NULL, NULL, NULL, NULL, word[4], NULL, word[5],
								 0xff, tags_data);
		break;

	case 332:
		inbound_topic (serv, word[4],
							(word_eol[5][0] == ':') ? word_eol[5] + 1 : word_eol[5],
							tags_data);
		break;

	case 333:
		inbound_topictime (serv, word[4], word[5], atol (word[6]), tags_data);
		break;

#if 0
	case 338:  /* Undernet Real user@host, Real IP */
		EMIT_SIGNAL_TIMESTAMP (XP_TE_WHOIS_REALHOST, sess, word[4], word[5], word[6], 
									  (word_eol[7][0]==':') ? word_eol[7]+1 : word_eol[7],
									  0, tags_data->timestamp);
		break;
#endif

	case 341:						  /* INVITE ACK */
		EMIT_SIGNAL_TIMESTAMP (XP_TE_UINVITE, sess, word[4], word[5],
									  serv->servername, NULL, 0, tags_data->timestamp);
		break;

	case 352:						  /* WHO */
		{
			unsigned int away = 0;
			session *who_sess = find_channel (serv, word[4]);

			if (*word[9] == 'G')
				away = 1;

			inbound_user_info (sess, word[4], word[5], word[6], word[7],
									 word[8], word_eol[11], NULL, away,
									 tags_data);

			/* try to show only user initiated whos */
			if (!who_sess || !who_sess->doing_who)
				EMIT_SIGNAL_TIMESTAMP (XP_TE_SERVTEXT, serv->server_session, text, word[1],
											  word[2], NULL, 0, tags_data->timestamp);
		}
		break;

	case 354:	/* undernet WHOX: used as a reply for irc_away_status */
		{
			unsigned int away = 0;
			session *who_sess;

			/* irc_away_status and irc_user_list sends out a "152" */
			if (!strcmp (word[4], "152"))
			{
				who_sess = find_channel (serv, word[5]);

				if (*word[10] == 'G')
					away = 1;

				/* :server 354 yournick 152 #channel ~ident host servname nick H account :realname */
				inbound_user_info (sess, word[5], word[6], word[7], word[8],
										 word[9], word_eol[12]+1, word[11], away,
										 tags_data);

				/* try to show only user initiated whos */
				if (!who_sess || !who_sess->doing_who)
					EMIT_SIGNAL_TIMESTAMP (XP_TE_SERVTEXT, serv->server_session, text,
												  word[1], word[2], NULL, 0,
												  tags_data->timestamp);
			} else
				goto def;
		}
		break;

	case 315:						  /* END OF WHO */
		{
			session *who_sess;
			who_sess = find_channel (serv, word[4]);
			if (who_sess)
			{
				if (!who_sess->doing_who)
					EMIT_SIGNAL_TIMESTAMP (XP_TE_SERVTEXT, serv->server_session, text,
												  word[1], word[2], NULL, 0,
												  tags_data->timestamp);
				who_sess->doing_who = FALSE;
			} else
			{
				if (!serv->doing_dns)
					EMIT_SIGNAL_TIMESTAMP (XP_TE_SERVTEXT, serv->server_session, text,
												  word[1], word[2], NULL, 0, tags_data->timestamp);
				serv->doing_dns = FALSE;
			}
		}
		break;

	case 346:	/* +I-list entry */
		if (!inbound_banlist (sess, atol (word[7]), word[4], word[5], word[6], 346,
									 tags_data))
			goto def;
		break;

	case 347:	/* end of invite list */
		if (!fe_ban_list_end (sess, 347))
			goto def;
		break;

	case 348:	/* +e-list entry */
		if (!inbound_banlist (sess, atol (word[7]), word[4], word[5], word[6], 348,
									 tags_data))
			goto def;
		break;

	case 349:	/* end of exemption list */
		sess = find_channel (serv, word[4]);
		if (!sess)
		{
			sess = serv->front_session;
			goto def;
		}
		if (!fe_ban_list_end (sess, 349))
			goto def;
		break;

	case 353:						  /* NAMES */
		inbound_nameslist (serv, word[5],
								 (word_eol[6][0] == ':') ? word_eol[6] + 1 : word_eol[6],
								 tags_data);
		break;

	case 366:
		if (!inbound_nameslist_end (serv, word[4], tags_data))
			goto def;
		break;

	case 367: /* banlist entry */
		if (!inbound_banlist (sess, atol (word[7]), word[4], word[5], word[6], 367,
									 tags_data))
			goto def;
		break;

	case 368:
		sess = find_channel (serv, word[4]);
		if (!sess)
		{
			sess = serv->front_session;
			goto def;
		}
		if (!fe_ban_list_end (sess, 368))
			goto def;
		break;

	case 369:	/* WHOWAS end */
	case 406:	/* WHOWAS error */
		EMIT_SIGNAL_TIMESTAMP (XP_TE_SERVTEXT, whois_sess, text, word[1], word[2],
									  NULL, 0, tags_data->timestamp);
		serv->inside_whois = 0;
		break;

	case 372:	/* motd text */
	case 375:	/* motd start */
		if (!prefs.hex_irc_skip_motd || serv->motd_skipped)
			EMIT_SIGNAL_TIMESTAMP (XP_TE_MOTD, serv->server_session, text, NULL,
										  NULL, NULL, 0, tags_data->timestamp);
		break;

	case 376:	/* end of motd */
	case 422:	/* motd file is missing */
		inbound_login_end (sess, text, tags_data);
		break;

	case 432:	/* erroneous nickname */
		if (serv->end_of_motd)
		{
			goto def;
		}
		inbound_next_nick (sess,  word[4], 1, tags_data);
		break;

	case 433:	/* nickname in use */
		if (serv->end_of_motd)
		{
			goto def;
		}
		inbound_next_nick (sess,  word[4], 0, tags_data);
		break;

	case 437:
        if (serv->end_of_motd || sess->server->is_channel_name(word[4]))
			goto def;
		inbound_next_nick (sess, word[4], 0, tags_data);
		break;

	case 471:
		EMIT_SIGNAL_TIMESTAMP (XP_TE_USERLIMIT, sess, word[4], NULL, NULL, NULL, 0,
									  tags_data->timestamp);
		break;

	case 473:
		EMIT_SIGNAL_TIMESTAMP (XP_TE_INVITE, sess, word[4], NULL, NULL, NULL, 0,
									  tags_data->timestamp);
		break;

	case 474:
		EMIT_SIGNAL_TIMESTAMP (XP_TE_BANNED, sess, word[4], NULL, NULL, NULL, 0,
									  tags_data->timestamp);
		break;

	case 475:
		EMIT_SIGNAL_TIMESTAMP (XP_TE_KEYWORD, sess, word[4], NULL, NULL, NULL, 0,
									  tags_data->timestamp);
		break;

	case 601:
		notify_set_offline (serv, word[4], FALSE, tags_data);
		break;

	case 605:
		notify_set_offline (serv, word[4], TRUE, tags_data);
		break;

	case 600:
	case 604:
		notify_set_online (serv, word[4], tags_data);
		break;

	case 728:	/* +q-list entry */
		/* NOTE:  FREENODE returns these results inconsistent with e.g. +b */
		/* Who else has imlemented MODE_QUIET, I wonder? */
		if (!inbound_banlist (sess, atol (word[8]), word[4], word[6], word[7], 728,
									 tags_data))
			goto def;
		break;

	case 729:	/* end of quiet list */
		if (!fe_ban_list_end (sess, 729))
			goto def;
		break;

	case 730: /* RPL_MONONLINE */
		notify_set_online_list (serv, word[4] + 1, tags_data);
		break;

	case 731: /* RPL_MONOFFLINE */
		notify_set_offline_list (serv, word[4] + 1, FALSE, tags_data);
		break;

	case 900:	/* successful SASL 'logged in as ' */
		EMIT_SIGNAL_TIMESTAMP (XP_TE_SERVTEXT, serv->server_session, 
									  word_eol[6]+1, word[1], word[2], NULL, 0,
									  tags_data->timestamp);
		break;
	case 903:	/* successful SASL auth */
	case 904:	/* failed SASL auth */
		if (inbound_sasl_error (serv))
			break; /* might retry */
	case 905:	/* failed SASL auth */
	case 906:	/* aborted */
	case 907:	/* attempting to re-auth after a successful auth */
		EMIT_SIGNAL_TIMESTAMP (XP_TE_SASLRESPONSE, serv->server_session, word[1],
									  word[2], word[3], ++word_eol[4], 0,
									  tags_data->timestamp);
		if (!serv->sent_capend)
		{
			serv->sent_capend = TRUE;
			tcp_send_len (serv, "CAP END\r\n", 9);
		}
		break;
	case 908:	/* Supported SASL Mechs */
		inbound_sasl_supportedmechs (serv, word[4]);
		break;

	default:

		if (serv->inside_whois && word[4][0])
		{
			/* some unknown WHOIS reply, ircd coders make them up weekly */
			if (!serv->skip_next_whois)
				EMIT_SIGNAL_TIMESTAMP (XP_TE_WHOIS_SPECIAL, whois_sess, word[4],
											  (word_eol[5][0] == ':') ? word_eol[5] + 1 : word_eol[5],
											  word[2], NULL, 0, tags_data->timestamp);
			return;
		}

	def:
		{
			session *sess;
		
			if (serv->is_channel_name (word[4]))
			{
				sess = find_channel (serv, word[4]);
				if (!sess)
					sess = serv->server_session;
			}
			else if ((sess=find_dialog (serv,word[4]))) /* user with an open dialog */
				;
			else
				sess=serv->server_session;
			
			EMIT_SIGNAL_TIMESTAMP (XP_TE_SERVTEXT, sess, text, word[1], word[2],
										  NULL, 0, tags_data->timestamp);
		}
	}
}

/* handle named messages that starts with a ':' */

static void
process_named_msg (session *sess, char *type, char *word[], char *word_eol[],
						 const message_tags_data *tags_data)
{
	server *serv = sess->server;
	char ip[128], nick[NICKLEN];
	char *text, *ex;
	int len = strlen (type);

	/* fill in the "ip" and "nick" buffers */
	ex = strchr (word[1], '!');
	if (!ex)							  /* no '!', must be a server message */
	{
		safe_strcpy (ip, word[1], sizeof (ip));
		safe_strcpy (nick, word[1], sizeof (nick));
	} else
	{
		safe_strcpy (ip, ex + 1, sizeof (ip));
		ex[0] = 0;
		safe_strcpy (nick, word[1], sizeof (nick));
		ex[0] = '!';
	}

	if (len == 4)
	{
		guint32 t;

		t = WORDL((guint8)type[0], (guint8)type[1], (guint8)type[2], (guint8)type[3]); 	
		/* this should compile to a bunch of: CMP.L, JE ... nice & fast */
		switch (t)
		{
		case WORDL('J','O','I','N'):
			{
				char *chan = word[3];
				char *account = word[4];
				char *realname = word_eol[5];

				if (account && strcmp (account, "*") == 0)
					account = NULL;
				if (realname && *realname == ':')
					realname++;
				if (*chan == ':')
					chan++;
				if (!serv->p_cmp (nick, serv->nick))
					inbound_ujoin (serv, chan, nick, ip, tags_data);
				else
					inbound_join (serv, chan, nick, ip, account, realname,
									  tags_data);
			}
			return;

		case WORDL('K','I','C','K'):
			{
				char *kicked = word[4];
				char *reason = word_eol[5];
				if (*kicked)
				{
					if (*reason == ':')
						reason++;
					if (!strcmp (kicked, serv->nick))
	 					inbound_ukick (serv, word[3], nick, reason, tags_data);
					else
						inbound_kick (serv, word[3], kicked, nick, reason, tags_data);
				}
			}
			return;

		case WORDL('K','I','L','L'):
			{
				char *reason = word_eol[4];
				if (*reason == ':')
					reason++;

				EMIT_SIGNAL_TIMESTAMP (XP_TE_KILL, sess, nick, reason, NULL, NULL,
											  0, tags_data->timestamp);
			}
			return;

		case WORDL('M','O','D','E'):
			handle_mode (serv, word, word_eol, nick, FALSE, tags_data);	/* modes.c */
			return;

		case WORDL('N','I','C','K'):
			inbound_newnick (serv, nick, 
								  (word_eol[3][0] == ':') ? word_eol[3] + 1 : word_eol[3],
								  FALSE, tags_data);
			return;

		case WORDL('P','A','R','T'):
			{
				char *chan = word[3];
				char *reason = word_eol[4];

				if (*chan == ':')
					chan++;
				if (*reason == ':')
					reason++;
				if (!strcmp (nick, serv->nick))
					inbound_upart (serv, chan, ip, reason, tags_data);
				else
					inbound_part (serv, chan, nick, ip, reason, tags_data);
			}
			return;

		case WORDL('P','O','N','G'):
			inbound_ping_reply (serv->server_session,
									  (word[4][0] == ':') ? word[4] + 1 : word[4],
									  word[3], tags_data);
			return;

		case WORDL('Q','U','I','T'):
			inbound_quit (serv, nick, ip,
							  (word_eol[3][0] == ':') ? word_eol[3] + 1 : word_eol[3],
							  tags_data);
			return;

		case WORDL('A','W','A','Y'):
			inbound_away_notify (serv, nick,
										(word_eol[3][0] == ':') ? word_eol[3] + 1 : NULL,
										tags_data);
			return;
		}

		goto garbage;
	}

	else if (len >= 5)
	{
		guint32 t;

		t = WORDL((guint8)type[0], (guint8)type[1], (guint8)type[2], (guint8)type[3]); 	
		/* this should compile to a bunch of: CMP.L, JE ... nice & fast */
		switch (t)
		{

		case WORDL('A','C','C','O'):
			inbound_account (serv, nick, word[3], tags_data);
			return;
			
		case WORDL('I','N','V','I'):
            if (ignore_check(word[1], ignore::IG_INVI))
				return;
			
			if (word[4][0] == ':')
				EMIT_SIGNAL_TIMESTAMP (XP_TE_INVITED, sess, word[4] + 1, nick,
											  serv->servername, NULL, 0,
											  tags_data->timestamp);
			else
				EMIT_SIGNAL_TIMESTAMP (XP_TE_INVITED, sess, word[4], nick,
											  serv->servername, NULL, 0,
											  tags_data->timestamp);
				
			return;

		case WORDL('N','O','T','I'):
			{
				int id = FALSE;								/* identified */

				text = word_eol[4];
				if (*text == ':')
				{
					text++;
				}

#ifdef USE_OPENSSL
				if (!strncmp (text, "CHALLENGE ", 10))		/* QuakeNet CHALLENGE upon our request */
				{
					char *response = challengeauth_response (serv->network->user ? serv->network->user : prefs.hex_irc_user_name, serv->password, word[5]);

					tcp_sendf (serv, "PRIVMSG %s :CHALLENGEAUTH %s %s %s\r\n",
						CHALLENGEAUTH_NICK,
						serv->network->user ? serv->network->user : prefs.hex_irc_user_name,
						response,
						CHALLENGEAUTH_ALGO);

					g_free (response);
					return;									/* omit the CHALLENGE <hash> ALGOS message */
				}
#endif

				if (serv->have_idmsg)
				{
					if (*text == '+')
					{
						id = TRUE;
						text++;
					} else if (*text == '-')
						text++;
				}

                if (!ignore_check(word[1], ignore::IG_NOTI))
					inbound_notice (serv, word[3], nick, text, ip, id, tags_data);
			}
			return;

		case WORDL('P','R','I','V'):
			{
				char *to = word[3];
				int len;
				int id = FALSE;	/* identified */
				if (*to)
				{
					/* Handle limited channel messages, for now no special event */
					if (serv->chantypes.find_first_of(to[0]) == std::string::npos
						&& serv->nick_prefixes.find_first_of(to[0]) != std::string::npos)
						to++;
						
					text = word_eol[4];
					if (*text == ':')
						text++;
					if (serv->have_idmsg)
					{
						if (*text == '+')
						{
							id = TRUE;
							text++;
						} else if (*text == '-')
							text++;
					}
					len = strlen (text);
					if (text[0] == 1 && text[len - 1] == 1)	/* ctcp */
					{
						text[len - 1] = 0;
						text++;
						if (g_ascii_strncasecmp (text, "ACTION", 6) != 0)
							flood_check(nick, ip, serv, sess, flood_check_type::CTCP);
						if (g_ascii_strncasecmp (text, "DCC ", 4) == 0)
							/* redo this with handle_quotes TRUE */
							process_data_init (word[1], word_eol[1], word, word_eol, TRUE, FALSE);
						ctcp_handle (sess, to, nick, ip, text, word, word_eol, id,
										 tags_data);
					} else
					{
						if (serv->is_channel_name (to))
						{
                            if (ignore_check(word[1], ignore::IG_CHAN))
								return;
							inbound_chanmsg (serv, NULL, to, nick, text, FALSE, id,
												  tags_data);
						} else
						{
                            if (ignore_check(word[1], ignore::IG_PRIV))
								return;
							inbound_privmsg (serv, nick, ip, text, id, tags_data);
						}
					}
				}
			}
			return;

		case WORDL('T','O','P','I'):
			inbound_topicnew (serv, nick, word[3],
									(word_eol[4][0] == ':') ? word_eol[4] + 1 : word_eol[4],
									tags_data);
			return;

		case WORDL('W','A','L','L'):
			text = word_eol[3];
			if (*text == ':')
				text++;
			EMIT_SIGNAL_TIMESTAMP (XP_TE_WALLOPS, sess, nick, text, NULL, NULL, 0,
										  tags_data->timestamp);
			return;
		}
	}

	else if (len == 3)
	{
		guint32 t;

		t = WORDL((guint8)type[0], (guint8)type[1], (guint8)type[2], (guint8)type[3]);
		switch (t)
		{
			case WORDL('C','A','P','\0'):
				if (strncasecmp (word[4], "ACK", 3) == 0)
				{
					inbound_cap_ack (serv, word[1], 
										  word[5][0] == ':' ? word_eol[5] + 1 : word_eol[5],
										  tags_data);
				}
				else if (strncasecmp (word[4], "LS", 2) == 0)
				{
					inbound_cap_ls (serv, word[1], 
										 word[5][0] == ':' ? word_eol[5] + 1 : word_eol[5],
										 tags_data);
				}
				else if (strncasecmp (word[4], "NAK", 3) == 0)
				{
					inbound_cap_nak (serv, tags_data);
				}
				else if (strncasecmp (word[4], "LIST", 4) == 0)	
				{
					inbound_cap_list (serv, word[1], 
											word[5][0] == ':' ? word_eol[5] + 1 : word_eol[5],
											tags_data);
				}

				return;
		}
	}

garbage:
	/* unknown message */
	PrintTextTimeStampf (sess, tags_data->timestamp, "GARBAGE: %s\n", word_eol[1]);
}

/* handle named messages that DON'T start with a ':' */

static void
process_named_servermsg (session *sess, char *buf, char *rawname, char *word_eol[],
								 const message_tags_data *tags_data)
{
	sess = sess->server->server_session;

	if (!strncmp (buf, "PING ", 5))
	{
		tcp_sendf (sess->server, "PONG %s\r\n", buf + 5);
		return;
	}
	if (!strncmp (buf, "ERROR", 5))
	{
		EMIT_SIGNAL_TIMESTAMP (XP_TE_SERVERERROR, sess, buf + 7, NULL, NULL, NULL,
									  0, tags_data->timestamp);
		return;
	}
	if (!strncmp (buf, "NOTICE ", 7))
	{
		buf = word_eol[3];
		if (*buf == ':')
			buf++;
		EMIT_SIGNAL_TIMESTAMP (XP_TE_SERVNOTICE, sess, buf, 
									  sess->server->servername, NULL, NULL, 0,
									  tags_data->timestamp);
		return;
	}
	if (!strncmp (buf, "AUTHENTICATE", 12))
	{
		inbound_sasl_authenticate (sess->server, word_eol[2]);
		return;
	}

	EMIT_SIGNAL_TIMESTAMP (XP_TE_SERVTEXT, sess, buf, sess->server->servername,
								  rawname, NULL, 0, tags_data->timestamp);
}

/* Returns the timezone offset. This should be the same as the variable
 * "timezone" in time.h, but *BSD doesn't have it.
 */
static int
get_timezone(void)
{
	struct tm tm_utc, tm_local;
	time_t t, time_utc, time_local;

	time (&t);

	/* gmtime() and localtime() are thread-safe on windows.
	 * on other systems we should use {gmtime,localtime}_r().
	 */
#if WIN32
	tm_utc = *gmtime (&t);
	tm_local = *localtime (&t);
#else
	gmtime_r (&t, &tm_utc);
	localtime_r (&t, &tm_local);
#endif

	time_utc = mktime (&tm_utc);
	time_local = mktime (&tm_local);

	return time_utc - time_local;
}

/* Handle time-server tags.
 * 
 * Sets tags_data->timestamp to the correct time (in unix time). 
 * This received time is always in UTC.
 *
 * See http://ircv3.atheme.org/extensions/server-time-3.2
 */
static void
handle_message_tag_time (const char *time, message_tags_data *tags_data)
{
	/* The time format defined in the ircv3.2 specification is
	 *       YYYY-MM-DDThh:mm:ss.sssZ
	 * but znc simply sends a unix time (with 3 decimal places for miliseconds)
	 * so we might as well support both.
	 */
	if (!*time)
		return;
	
	if (time[strlen (time) - 1] == 'Z')
	{
		/* as defined in the specification */
		struct tm t;
		int z;

		/* we ignore the milisecond part */
		z = sscanf (time, "%d-%d-%dT%d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday,
					&t.tm_hour, &t.tm_min, &t.tm_sec);

		if (z != 6)
			return;

		t.tm_year -= 1900;
		t.tm_mon -= 1;
		t.tm_isdst = 0; /* day light saving time */

		tags_data->timestamp = mktime (&t);

		if (tags_data->timestamp < 0)
		{
			tags_data->timestamp = 0;
			return;
		}

		/* get rid of the local time (mktime() receives a local calendar time) */
		tags_data->timestamp -= get_timezone();
	}
	else
	{
		/* znc */
		long long int t;

		/* we ignore the milisecond part */
		if (sscanf (time, "%lld", &t) != 1)
			return;

		tags_data->timestamp = (time_t) t;
	}
}

/* Handle message tags.
 *
 * See http://ircv3.atheme.org/specification/message-tags-3.2 
 */
static void
handle_message_tags (server *serv, const char *tags_str,
							message_tags_data *tags_data)
{
	char **tags;
	int i;

	/* FIXME We might want to avoid the allocation overhead here since 
	 * this might be called for every message from the server.
	 */
	tags = g_strsplit (tags_str, ";", 0);

	for (i=0; tags[i]; i++)
	{
		char *key = tags[i];
		char *value = strchr (tags[i], '=');

		if (!value)
			continue;

		*value = '\0';
		value++;

		if (serv->have_server_time && !strcmp (key, "time"))
			handle_message_tag_time (value, tags_data);
	}
	
	g_strfreev (tags);
}

/* irc_inline() - 1 single line received from serv */
void
server::p_inline (char *buf, int len)
{
	session *sess;
	char *type, *text;
	char *word[PDIWORDS+1];
	char *word_eol[PDIWORDS+1];
	char pdibuf_static[522]; /* 1 line can potentially be 512*6 in utf8 */
	char *pdibuf = pdibuf_static;
	message_tags_data tags_data = MESSAGE_TAGS_DATA_INIT;

	/* need more than 522? fall back to malloc */
	if (len >= sizeof (pdibuf_static))
		pdibuf = static_cast<char*>(malloc (len + 1));

	sess = this->front_session;

	/* Python relies on this */
	word[PDIWORDS] = NULL;
	word_eol[PDIWORDS] = NULL;

	if (*buf == '@')
	{
		char *tags = buf + 1; /* skip the '@' */
		char *sep = strchr (buf, ' ');

		if (!sep)
			goto xit;
		
		*sep = '\0';
		buf = sep + 1;

		handle_message_tags(this, tags, &tags_data);
	}

	url_check_line (buf, len);

	/* split line into words and words_to_end_of_line */
	process_data_init (pdibuf, buf, word, word_eol, FALSE, FALSE);

	if (buf[0] == ':')
	{
		/* find a context for this message */
		if (this->is_channel_name (word[3]))
		{
			auto tmp = find_channel (word[3]);
			if (tmp)
				sess = &(*tmp);
		}

		/* for server messages, the 2nd word is the "message type" */
		type = word[2];

		word[0] = type;
		word_eol[1] = buf;	/* keep the ":" for plugins */

		if (plugin_emit_server (sess, type, word, word_eol,
								tags_data.timestamp))
			goto xit;

		word[1]++;
		word_eol[1] = buf + 1;	/* but not for HexChat internally */

	} else
	{
		word[0] = type = word[1];

		if (plugin_emit_server (sess, type, word, word_eol,
								tags_data.timestamp))
			goto xit;
	}

	if (buf[0] != ':')
	{
		process_named_servermsg (sess, buf, word[0], word_eol, &tags_data);
		goto xit;
	}

	/* see if the second word is a numeric */
	if (isdigit ((unsigned char) word[2][0]))
	{
		text = word_eol[4];
		if (*text == ':')
			text++;

		process_numeric (sess, atoi (word[2]), word, word_eol, text, &tags_data);
	} else
	{
		process_named_msg (sess, type, word, word_eol, &tags_data);
	}

xit:
	if (pdibuf != pdibuf_static)
		free (pdibuf);
}
