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
#include <locale>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <boost/utility/string_ref.hpp>
#include <gsl.h>
#include <boost/algorithm/string/predicate.hpp>

#include "hexchat.hpp"
#include "hexchatc.hpp"
#include "modes.hpp"
#include "server.hpp"
#include "text.hpp"
#include "fe.hpp"
#include "util.hpp"
#include "inbound.hpp"
#include "userlist.hpp"
#include "session.hpp"
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif

#include <glib/gprintf.h>

struct mode_run
{
	server *serv;
	std::string op;
	std::string deop;
	std::string voice;
	std::string devoice;
};

static int is_prefix_char (const server * serv, char c);
static void record_chan_mode (session &sess, char sign, char mode, char *arg);
static void handle_single_mode (mode_run &mr, char sign, char mode, char *nick,
										  char *chan, char *arg, bool quiet, bool is_324,
										  const message_tags_data *tags_data);
static bool mode_has_arg(const server & serv, char sign, char mode);
static void mode_print_grouped (session *sess, char *nick, mode_run &mr,
										  const message_tags_data *tags_data);
static int mode_chanmode_type(const server & serv, char mode);


/* word[] - list of nicks.
   wpos   - index into word[]. Where nicks really start.
   end    - index into word[]. Last entry plus one.
   sign   - a char, e.g. '+' or '-'
   mode   - a mode, e.g. 'o' or 'v'	*/
void
send_channel_modes (session *sess, const std::vector<std::string> &word, int wpos,
						  int end, char sign, char mode, int modes_per_line)
{
	server *serv = sess->server;

	/* sanity check. IRC RFC says three per line. */
	if (serv->modes_per_line < 3)
		serv->modes_per_line = 3;
	if (modes_per_line < 1)
		modes_per_line = serv->modes_per_line;

	/* RFC max, minus length of "MODE %s " and "\r\n" and 1 +/- sign */
	/* 512 - 6 - 2 - 1 - strlen(chan) */
	std::string channel(sess->channel);
	auto max = 503 - channel.size();

	while (wpos < end)
	{
		std::ostringstream buf;
		std::ostream_iterator<char> tbuf(buf);

		/* we'll need this many modechars too */
		int len = modes_per_line;

		/* how many can we fit? */
		int i;
		for (i = 0; i < modes_per_line; i++)
		{
			/* no more nicks left? */
			if (wpos + i >= end)
				break;
			auto wlen = word.at(wpos + i).size() + 1;
			if (wlen + len > max)
				break;
			len += wlen; /* length of our whole string so far */
		}
		if (i < 1)
			return;
		int usable_modes = i;	/* this is how many we'll send on this line */

		/* add the +/-modemodemodemode */
		*tbuf++ = sign;
		for (int i = 0; i < usable_modes; i++)
		{
			*tbuf++ = mode;
		}

		/* add all the nicknames */
		for (i = 0; i < usable_modes; i++)
		{
			*tbuf++ = ' ';
			buf << word[wpos + i];
		}
		serv->p_mode (channel, buf.str());

		wpos += usable_modes;
	}
}

/* does 'chan' have a valid prefix? e.g. # or & */
bool
server::is_channel_name(const boost::string_ref & chan) const
{
	return this->chantypes.find_first_of(chan[0]) != std::string::npos;
}

/* is the given char a valid nick mode char? e.g. @ or + */

static int
is_prefix_char (const server * serv, char c)
{
	auto pos = serv->nick_prefixes.find_first_of(c);
	if (pos != std::string::npos)
		return pos;

	if (serv->bad_prefix)
	{
		if (serv->bad_nick_prefixes.find_first_of(c) != std::string::npos)
		/* valid prefix char, but mode unknown */
			return -2;
	}

	return -1;
}

/* returns '@' for ops etc... */

char
get_nick_prefix (const server * serv, unsigned int access)
{
	for (int pos = 0; pos < USERACCESS_SIZE; pos++)
	{
		char c = serv->nick_prefixes[pos];
		if (c == 0)
			break;
		if (access & (1 << pos))
			return c;
	}

	return 0;
}

/* returns the access bitfield for a nickname. E.g.
	@nick would return 000010 in binary
	%nick would return 000100 in binary
	+nick would return 001000 in binary */

unsigned int
nick_access (const server * serv, const char *nick, int &modechars)
{
	int i;
	unsigned int access = 0;
	const char *orig = nick;

	while (*nick)
	{
		i = is_prefix_char (serv, *nick);
		if (i == -1)
			break;

		/* -2 == valid prefix char, but mode unknown */
		if (i != -2)
			access |= (1 << i);

		nick++;
	}

	modechars = nick - orig;

	return access;
}

/* returns the access number for a particular mode. e.g.
	mode 'a' returns 0
	mode 'o' returns 1
	mode 'h' returns 2
	mode 'v' returns 3
	Also puts the nick-prefix-char in 'prefix' */

int
mode_access (const server * serv, char mode, char *prefix)
{
	int pos = 0;

	while (serv->nick_modes[pos])
	{
		if (serv->nick_modes[pos] == mode)
		{
			*prefix = serv->nick_prefixes[pos];
			return pos;
		}
		pos++;
	}

	*prefix = 0;

	return -1;
}

static void
record_chan_mode (session &sess, char sign, char mode, char *arg)
{
	/* Somebody needed to acutally update sess->current_modes, needed to
		play nice with bouncers, and less mode calls. Also keeps modes up
		to date for scripts */
	if (!sess.server)
		throw std::runtime_error("Invalid Server Reference");
	server &serv = *sess.server;
	// deliberate copy for exception safety
	std::string current(sess.current_modes);
	gint mode_pos = -1;
	auto current_char = current.begin();
	std::string::size_type modes_length;
	gint argument_num = 0;
	gint argument_offset = 0;
	gint argument_length = 0;
	int i = 0;
	//gchar *arguments_start;

	/* find out if the mode currently exists */
	auto arguments_start = current.find_first_of(' ');
	if (arguments_start != std::string::npos) {
		modes_length = arguments_start;
	}
	else {
		modes_length = current.size();
		/* set this to the end of the modes */
		arguments_start = current.size();
	}

	while (mode_pos == -1 && i < modes_length)
	{
		if (*current_char == mode)
		{
			mode_pos = i;
		}
		else
		{
			i++;
			current_char++;
		}
	}

	/* if the mode currently exists and has an arg, need to know where
	 * (including leading space) */
	if (mode_pos != -1 && mode_has_arg(serv, '+', mode))
	{
		current_char = current.begin();

		i = 0;
		while (i <= mode_pos)
		{
			if (mode_has_arg(serv, '+', *current_char))
				argument_num++;
			current_char++;
			i++;
		}

		/* check through arguments for where to start */
		current_char = current.begin() + arguments_start;
		i = 0;
		while (i < argument_num && *current_char != '\0')
		{
			if (*current_char == ' ')
				i++;
			if (i != argument_num)
				current_char++;
		}
		argument_offset = current_char - current.begin();

		/* how long the existing argument is for this key
		 * important for malloc and strncpy */
		if (i == argument_num)
		{
			argument_length++;
			current_char++;
			while (*current_char != '\0' && *current_char != ' ')
			{
				argument_length++;
				current_char++;
			}
		}
	}

	/* two cases, adding and removing a mode, handled differently */
	if (sign == '+')
	{
		if (mode_pos != -1)
		{
			/* if it already exists, only need to do something (change)
			 * if there should be a param */
			if (mode_has_arg(serv, sign, mode))
			{
				/* leave the old space there */
				current.erase(argument_offset + 1, argument_length - 1);
				current.insert(argument_offset + 1, arg);

				sess.current_modes = std::move(current);
			}
		}
		/* mode wasn't there before */
		else
		{
			/* insert the new mode character */
			current.insert(modes_length, 1, mode);

			/* add the argument, with space if there is one */
			if (mode_has_arg(serv, sign, mode))
			{
				current.push_back(' ');
				current.append(arg);
			}

			sess.current_modes = std::move(current);
		}
	}
	else if (sign == '-' && mode_pos != -1)
	{
		/* remove the argument first if it has one*/
		if (mode_has_arg(serv, '+', mode))
			current.erase(argument_offset, argument_length);

		/* remove the mode character */
		current.erase(mode_pos, 1);

		sess.current_modes = std::move(current);
	}
}

static std::string
mode_cat(const std::string& str, const std::string& addition)
{
	if (!str.empty())
	{
		return str + " " + addition;
	}
	return addition;
}

/* handle one mode, e.g.
   handle_single_mode (mr,'+','b',"elite","#warez","banneduser",) */

static void
handle_single_mode (mode_run &mr, char sign, char mode, char *nick,
						  char *chan, char *arg, bool quiet, bool is_324,
						  const message_tags_data *tags_data)
{
	session *sess;
	if (!mr.serv)
		throw std::runtime_error("Invalid server reference");
	server &serv = *(mr.serv);
	char outbuf[4];
	bool supportsq = false;

	outbuf[0] = sign;
	outbuf[1] = 0;
	outbuf[2] = mode;
	outbuf[3] = 0;

	sess = find_channel (serv, chan);
	if (!sess || !serv.is_channel_name (chan))
	{
		/* got modes for a chan we're not in! probably nickmode +isw etc */
		sess = serv.front_session;
		goto genmode;
	}

	/* is this a nick mode? */
	if (serv.nick_modes.find_first_of(mode) != std::string::npos)
	{
		/* update the user in the userlist */
		userlist_update_mode (sess, /*nickname */ arg, mode, sign);
	} else
	{
		if (!is_324 && !sess->ignore_mode && mode_chanmode_type(serv, mode) >= 1)
			record_chan_mode (*sess, sign, mode, arg);
	}

	/* Is q a chanmode on this server? */
	for (char cm : serv.chanmodes)
	{
		if (cm == ',')
			break;
		if (cm == 'q')
			supportsq = true;
	}

	switch (sign)
	{
	case '+':
		switch (mode)
		{
		case 'k':
			safe_strcpy (sess->channelkey, arg, sizeof (sess->channelkey));
			fe_update_channel_key (sess);
			fe_update_mode_buttons (sess, mode, sign);
			if (!quiet)
				EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANSETKEY, sess, nick, arg, NULL,
											  NULL, 0, tags_data->timestamp);
			return;
		case 'l':
			sess->limit = atoi (arg);
			fe_update_channel_limit (sess);
			fe_update_mode_buttons (sess, mode, sign);
			if (!quiet)
				EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANSETLIMIT, sess, nick, arg, NULL,
											  NULL, 0, tags_data->timestamp);
			return;
		case 'o':
			if (!quiet)
				mr.op = mode_cat (mr.op, arg);
			return;
		case 'h':
			if (!quiet)
				EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANHOP, sess, nick, arg, NULL, NULL,
											  0, tags_data->timestamp);
			return;
		case 'v':
			if (!quiet)
				mr.voice = mode_cat (mr.voice, arg);
			return;
		case 'b':
			if (!quiet)
				EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANBAN, sess, nick, arg, NULL, NULL,
											  0, tags_data->timestamp);
			return;
		case 'e':
			if (!quiet)
				EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANEXEMPT, sess, nick, arg, NULL,
											  NULL, 0, tags_data->timestamp);
			return;
		case 'I':
			if (!quiet)
				EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANINVITE, sess, nick, arg, NULL, NULL,
											  0, tags_data->timestamp);
			return;
		case 'q':
			if (!supportsq)
				break; /* +q is owner on this server */
			if (!quiet)
				EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANQUIET, sess, nick, arg, NULL, NULL, 0,
								 tags_data->timestamp);
			return;
		}
		break;
	case '-':
		switch (mode)
		{
		case 'k':
			sess->channelkey[0] = 0;
			fe_update_channel_key (sess);
			fe_update_mode_buttons (sess, mode, sign);
			if (!quiet)
				EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANRMKEY, sess, nick, NULL, NULL,
											  NULL, 0, tags_data->timestamp);
			return;
		case 'l':
			sess->limit = 0;
			fe_update_channel_limit (sess);
			fe_update_mode_buttons (sess, mode, sign);
			if (!quiet)
				EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANRMLIMIT, sess, nick, NULL, NULL,
											  NULL, 0, tags_data->timestamp);
			return;
		case 'o':
			if (!quiet)
				mr.deop = mode_cat (mr.deop, arg);
			return;
		case 'h':
			if (!quiet)
				EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANDEHOP, sess, nick, arg, NULL,
											  NULL, 0, tags_data->timestamp);
			return;
		case 'v':
			if (!quiet)
				mr.devoice = mode_cat (mr.devoice, arg);
			return;
		case 'b':
			if (!quiet)
				EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANUNBAN, sess, nick, arg, NULL, NULL,
											  0, tags_data->timestamp);
			return;
		case 'e':
			if (!quiet)
				EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANRMEXEMPT, sess, nick, arg, NULL,
											  NULL, 0, tags_data->timestamp);
			return;
		case 'I':
			if (!quiet)
				EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANRMINVITE, sess, nick, arg, NULL,
											  NULL, 0, tags_data->timestamp);
			return;
		case 'q':
			if (!supportsq)
				break; /* -q is owner on this server */
			if (!quiet)
				EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANUNQUIET, sess, nick, arg, NULL,
											  NULL, 0, tags_data->timestamp);
			return;
		}
	}

	fe_update_mode_buttons (sess, mode, sign);

 genmode:
	/* Received umode +e. If we're waiting to send JOIN then send now! */
	if (mode == 'e' && sign == '+' && !serv.p_cmp (chan, serv.nick))
		inbound_identified (serv);

	if (!quiet)
	{
		if (*arg)
		{
			std::string buf(strlen(chan) + strlen(arg) + 2, '\0');
			sprintf(&buf[0], "%s %s", chan, arg);
			EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANMODEGEN, sess, nick, outbuf,
										  outbuf + 2, &buf[0], 0, tags_data->timestamp);
		} else
			EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANMODEGEN, sess, nick, outbuf,
										  outbuf + 2, chan, 0, tags_data->timestamp);
	}
}

/* does this mode have an arg? like +b +l +o */

static bool
mode_has_arg (const server & serv, char sign, char mode)
{
	int type;

	/* if it's a nickmode, it must have an arg */
	if (serv.nick_modes.find_first_of(mode) != std::string::npos)
		return true;

	type = mode_chanmode_type (serv, mode);
	switch (type)
	{
	case 0:					  /* type A */
	case 1:					  /* type B */
		return true;
	case 2:					  /* type C */
		if (sign == '+')
			return true;
	case 3:					  /* type D */
		return false;
	default:
		return false;
	}

}

/* what type of chanmode is it? -1 for not in chanmode */
static int
mode_chanmode_type (const server & serv, char mode)
{
	/* see what numeric 005 CHANMODES=xxx said */
	int type = 0;
	bool found = false;

	for(auto &cm : serv.chanmodes)
	{
		if (cm == ',')
		{
			type++;
		} else if (cm == mode)
		{
			found = true;
			break;
		}
	}
	if (found)
		return type;
	/* not found? -1 */
	else
		return -1;
}

static void
mode_print_grouped (session *sess, char *nick, mode_run &mr,
						  const message_tags_data *tags_data)
{
	/* print all the grouped Op/Deops */
	if (!mr.op.empty())
	{
		mr.op.push_back(0);
		EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANOP, sess, nick, &mr.op[0], NULL, NULL, 0,
									  tags_data->timestamp);
		mr.op.clear();
	}

	if (!mr.deop.empty())
	{
		mr.deop.push_back(0);
		EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANDEOP, sess, nick, &mr.deop[0], NULL, NULL,
									  0, tags_data->timestamp);
		mr.deop.clear();
	}

	if (!mr.voice.empty())
	{
		mr.voice.push_back(0);
		EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANVOICE, sess, nick, &mr.voice[0], NULL, NULL,
									  0, tags_data->timestamp);
		mr.voice.clear();
	}

	if (!mr.devoice.empty())
	{
		mr.devoice.push_back(0);
		EMIT_SIGNAL_TIMESTAMP (XP_TE_CHANDEVOICE, sess, nick, &mr.devoice[0], NULL,
									  NULL, 0, tags_data->timestamp);
		mr.devoice.clear();
	}
}


/* handle a MODE or numeric 324 from server */

void
handle_mode (server & serv, char *word[], char *word_eol[],
				 char *nick, bool numeric_324, const message_tags_data *tags_data)
{
	session *sess;
	char *chan;
	char *modes;
	char *argstr;
	char sign;
	int len;
	int arg;
	int i, num_args;
	int num_modes;
	int offset = 3;
	bool all_modes_have_args = false;
	bool using_front_tab = false;
	mode_run mr = mode_run();

	mr.serv = &serv;

	/* numeric 324 has everything 1 word later (as opposed to MODE) */
	if (numeric_324)
		offset++;

	chan = word[offset];
	modes = word[offset + 1];
	if (*modes == ':')
		modes++;

	if (*modes == 0)
		return;	/* beyondirc's blank modes */

	sess = find_channel (serv, chan);
	if (!sess)
	{
		sess = serv.front_session;
		using_front_tab = true;
	}
	/* remove trailing space */
	len = strlen (word_eol[offset]) - 1;
	if (word_eol[offset][len] == ' ')
		word_eol[offset][len] = 0;

	if (prefs.hex_irc_raw_modes && !numeric_324)
		EMIT_SIGNAL_TIMESTAMP (XP_TE_RAWMODES, sess, nick, word_eol[offset], 0, 0, 0,
									  tags_data->timestamp);

	if (numeric_324 && !using_front_tab)
	{
		sess->current_modes = word_eol[offset+1];
	}

	sign = *modes;
	modes++;
	arg = 1;

	/* count the number of arguments (e.g. after the -o+v) */
	num_args = 0;
	i = 1;
	while ((i + offset + 1) < PDIWORDS)
	{
		i++;
		if (!(*word[i + offset]))
			break;
		num_args++;
	}

	/* count the number of modes (without the -/+ chars */
	num_modes = 0;
	i = 0;
	while (i < strlen (modes))
	{
		if (modes[i] != '+' && modes[i] != '-')
			num_modes++;
		i++;
	}

	if (num_args == num_modes)
		all_modes_have_args = true;

	while (*modes)
	{
		switch (*modes)
		{
		case '-':
		case '+':
			/* print all the grouped Op/Deops */
			mode_print_grouped (sess, nick, mr, tags_data);
			sign = *modes;
			break;
		default:
			argstr = "";
			if ((all_modes_have_args || mode_has_arg (serv, sign, *modes)) && arg < (num_args+1))
			{
				arg++;
				argstr = word[arg + offset];
			}
			handle_single_mode (mr, sign, *modes, nick, chan,
									  argstr, numeric_324 || prefs.hex_irc_raw_modes,
									  numeric_324, tags_data);
		}

		modes++;
	}

	/* update the title at the end, now that the mode update is internal now */
	if (!using_front_tab)
		fe_set_title (*sess);

	/* print all the grouped Op/Deops */
	mode_print_grouped (sess, nick, mr, tags_data);
}

namespace
{
	struct ascii_strcasecmp : public std::collate < char >
	{
	protected:
		int do_compare(const char * low1, const char * high1,
			const char * low2, const char* high2) const
		{
			return g_ascii_strcasecmp(low1, low2);
		}
	};
}

/* handle the 005 numeric */

void
inbound_005 (server & serv, gsl::span<char*> word)
{
	auto it = word.cbegin();
	std::advance(it, 4);
	std::for_each(it, word.cend(), 
		[&serv](auto & wordVal) {
		auto span = gsl::cstring_span<>(wordVal, std::strlen(wordVal));
		if (boost::starts_with(span, "MODES="))
		{
			serv.modes_per_line = std::stoi(gsl::to_string(span.subspan(6)));
		}
		else if (boost::starts_with(span, "CHANTYPES="))
		{
			serv.chantypes.clear();
			serv.chantypes = gsl::to_string(span.subspan(10));
		}
		else if (boost::starts_with(span, "CHANMODES="))
		{
			serv.chanmodes = gsl::to_string(span.subspan(10));
		}
		else if (boost::starts_with(span, "PREFIX="))
		{
			auto prefixSpan = span.subspan(7);
			auto parenLoc = std::find(prefixSpan.cbegin(), prefixSpan.cend(), ')');
			if (parenLoc != prefixSpan.cend())
			{
				const auto pre = std::distance(prefixSpan.cbegin(), parenLoc);
				serv.nick_prefixes = gsl::to_string(prefixSpan.subspan(pre + 1));
				serv.nick_modes = gsl::to_string(span.subspan(8, pre - 1));
			}
			else
			{
				/* bad! some ircds don't give us the modes. */
				/* in this case, we use it only to strip /NAMES */
				serv.bad_prefix = true;
				serv.bad_nick_prefixes = gsl::to_string(span.subspan(7));
			}
		}
		else if (boost::starts_with(span, "WATCH="))
		{
			serv.supports_watch = true;
		}
		else if (boost::starts_with(span, "MONITOR="))
		{
			serv.supports_monitor = true;
		}
		else if (boost::starts_with(span, "NETWORK="))
		{
			/*			if (serv.networkname)
			free (serv.networkname);
			serv.networkname = strdup (span + 8);*/

			if (serv.server_session->type == session::SESS_SERVER)
			{
				safe_strcpy(serv.server_session->channel, span.subspan(8).data(), CHANLEN);
				fe_set_channel(serv.server_session);
			}

		}
		else if (boost::starts_with(span, "CASEMAPPING="))
		{
			if (span.subspan(12) == "ascii")	/* bahamut */
			{
				serv.p_cmp = (int(*)(const char*, const char*))g_ascii_strcasecmp;
				serv.imbue(std::locale(std::locale(), new ascii_strcasecmp));
			}
		}
		else if (boost::starts_with(span, "CHARSET="))
		{
			if (g_ascii_strncasecmp(span.subspan(8).data(), "UTF-8", 5) == 0)
			{
				serv.set_encoding("UTF-8");
			}
		}
		else if (span == "NAMESX")
		{
			/* 12345678901234567 */
			tcp_send(serv, "PROTOCTL NAMESX\r\n");
		}
		else if (span == "WHOX")
		{
			serv.have_whox = true;
		}
		else if (span == "EXCEPTS")
		{
			serv.have_except = true;
		}
		else if (span == "INVEX")
		{
			/* supports mode letter +I, default channel invite */
			serv.have_invite = true;
		}
		else if (span == "ELIST=")
		{
			auto elistSpan = span.subspan(6);
			/* supports LIST >< min/max user counts? */
			if (boost::icontains(elistSpan, "U"))
				serv.use_listargs = true;
		}
	});
}
