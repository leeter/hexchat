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

#include <sstream>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <boost/format.hpp>
#include <boost/optional.hpp>
#include <boost/utility/string_ref.hpp>

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
#include "hexchat.hpp"
#include "ignore.hpp"
#include "cfgfiles.hpp"
#include "fe.hpp"
#include "text.hpp"
#include "util.hpp"
#include "hexchatc.hpp"
#include "server.hpp"
#include "typedef.h"

ignore::ignore()
	:type()
{}

int ignored_ctcp = 0;			  /* keep a count of all we ignore */
int ignored_priv = 0;
int ignored_chan = 0;
int ignored_noti = 0;
int ignored_invi = 0;

static std::vector<ignore> ignores;
static int ignored_total = 0;
/* ignore_exists ():
 * returns: struct ig, if this mask is in the ignore list already
 *          NULL, otherwise
 */
boost::optional<ignore &>
ignore_exists (const boost::string_ref& mask)
{
	auto res = std::find_if(ignores.begin(), ignores.end(), [&mask](const ignore & ig){
		return !rfc_casecmp(ig.mask.c_str(), mask.data());
	});
	return res != ignores.end() ? boost::make_optional<ignore&>(*res) : boost::none;
}

/* ignore_add(...)

 * returns:
 *            0 fail
 *            1 success
 *            2 success (old ignore has been changed)
 */

int
ignore_add(const std::string& mask, int type, bool overwrite)
{
	bool change_only = false;

	/* first check if it's already ignored */
	auto ig = ignore_exists (mask);
	if (ig)
		change_only = true;

	ignore new_ig;
	if (!change_only)
		ig = new_ig;

	if (!ig)
		return 0;

	ig->mask = mask;

	if (!overwrite && change_only)
		ig->type |= type;
	else
		ig->type = type;

	if (!change_only)
		ignores.push_back(ig.get());
	fe_ignore_update (1);

	if (change_only)
		return 2;

	return 1;
}

void
ignore_showlist (session *sess)
{
	//char tbuf[256];

	EMIT_SIGNAL (XP_TE_IGNOREHEADER, sess, 0, 0, 0, 0, 0);

	const std::string yes_str(_("YES  "));
	const std::string no_str(_("NO   "));

	for (const auto & ig : ignores)
	{
		std::ostringstream buf;
		buf << boost::format(" %-25s ") % ig.mask;
		if (ig.type & ignore::IG_PRIV)
			buf << yes_str;
		else
			buf << no_str;
		if (ig.type & ignore::IG_NOTI)
			buf << yes_str;
		else
			buf << no_str;
		if (ig.type & ignore::IG_CHAN)
			buf << yes_str;
		else
			buf << no_str;
		if (ig.type & ignore::IG_CTCP)
			buf << yes_str;
		else
			buf << no_str;
		if (ig.type & ignore::IG_DCC)
			buf << yes_str;
		else
			buf << no_str;
		if (ig.type & ignore::IG_INVI)
			buf << yes_str;
		else
			buf << no_str;
		if (ig.type & ignore::IG_UNIG)
			buf << yes_str;
		else
			buf << no_str;
		buf << "\n";
		PrintText (sess, buf.str());
		/*EMIT_SIGNAL (XP_TE_IGNORELIST, sess, ig->mask, 0, 0, 0, 0); */
		/* use this later, when TE's support 7 args */
	}

	if (ignores.empty())
		EMIT_SIGNAL (XP_TE_IGNOREEMPTY, sess, 0, 0, 0, 0, 0);

	EMIT_SIGNAL (XP_TE_IGNOREFOOTER, sess, 0, 0, 0, 0, 0);
}

/* ignore_del()

 * one of the args must be NULL, use mask OR *ig, not both
 *
 */

bool
ignore_del(const std::string& mask)
{
	auto old_size = ignores.size();
	auto res = ignores.erase(
		std::remove_if(ignores.begin(), ignores.end(), [&mask](const ignore & ig){
			return !rfc_casecmp(ig.mask.c_str(), mask.c_str());
		}));
	fe_ignore_update(1);
	return ignores.size() != old_size;
}

/* check if a msg should be ignored by browsing our ignore list */

bool ignore_check(const boost::string_ref& mask, ignore::ignore_type type)
{
	/* check if there's an UNIGNORE first, they take precendance. */
	for(const auto & ig : ignores)
	{
		if (ig.type & ignore::IG_UNIG)
		{
			if (ig.type & type)
			{
				if (match (ig.mask.c_str(), mask.data()))
					return false;
			}
		}
	}

	for(const auto & ig : ignores)
	{
		if (ig.type & type)
		{
			if (match (ig.mask.c_str(), mask.data()))
			{
				ignored_total++;
				if (type & ignore::IG_PRIV)
					ignored_priv++;
				if (type & ignore::IG_NOTI)
					ignored_noti++;
				if (type & ignore::IG_CHAN)
					ignored_chan++;
				if (type & ignore::IG_CTCP)
					ignored_ctcp++;
				if (type & ignore::IG_INVI)
					ignored_invi++;
				fe_ignore_update (2);
				return true;
			}
		}
	}

	return false;
}

static char *
ignore_read_next_entry (char *my_cfg, ignore &ig)
{
	char tbuf[1024];

	/* Casting to char * done below just to satisfy compiler */

	if (my_cfg)
	{
		my_cfg = cfg_get_str (my_cfg, "mask", tbuf, sizeof (tbuf));
		if (!my_cfg)
			return NULL;
		ig.mask = tbuf;
	}
	if (my_cfg)
	{
		my_cfg = cfg_get_str (my_cfg, "type", tbuf, sizeof (tbuf));
		ig.type = atoi (tbuf);
	}
	return my_cfg;
}

void
ignore_load ()
{
	struct stat st;
	char *my_cfg;
	int fh, i;

	fh = hexchat_open_file ("ignore.conf", O_RDONLY, 0, 0);
	if (fh != -1)
	{
		fstat (fh, &st);
		if (st.st_size)
		{
			std::string cfg(st.st_size + 1, '\0');
			cfg[0] = '\0';
			i = read (fh, &cfg[0], st.st_size);
			if (i >= 0)
				cfg[i] = '\0';
			my_cfg = &cfg[0];
			while (my_cfg)
			{
				ignore ig;
				if ((my_cfg = ignore_read_next_entry(my_cfg, ig)))
					ignores.emplace_back(std::move(ig));
			}
		}
		close (fh);
	}
}

void
ignore_save ()
{
	char buf[1024];
	int fh;

	fh = hexchat_open_file ("ignore.conf", O_TRUNC | O_WRONLY | O_CREAT, 0600, XOF_DOMODE);
	if (fh != -1)
	{
		for(const auto & ig : ignores)
		{
			if (!(ig.type & ignore::IG_NOSAVE))
			{
				snprintf (buf, sizeof (buf), "mask = %s\ntype = %u\n\n",
							 ig.mask.c_str(), ig.type);
				write (fh, buf, strlen (buf));
			}
		}
		close (fh);
	}
}

static gboolean
flood_autodialog_timeout (gpointer)
{
	prefs.hex_gui_autoopen_dialog = 1;
	return FALSE;
}

bool
flood_check (const char *nick, const char *ip, server &serv, session *sess, flood_check_type what)	/*0=ctcp  1=priv */
{
	/*
	   serv
	   int ctcp_counter; 
	   time_t ctcp_last_time;
	   prefs
	   unsigned int ctcp_number_limit;
	   unsigned int ctcp_time_limit;
	 */
	char buf[512];
	char real_ip[132];
	int i;
	time_t current_time;
	current_time = time (NULL);

	if (what == flood_check_type::CTCP)
	{
		if (serv.ctcp_last_time == 0)	/*first ctcp in this server */
		{
			serv.ctcp_last_time = time (NULL);
			serv.ctcp_counter++;
		} else
		{
			if (difftime (current_time, serv.ctcp_last_time) < prefs.hex_flood_ctcp_time)	/*if we got the ctcp in the seconds limit */
			{
				serv.ctcp_counter++;
				if (serv.ctcp_counter == prefs.hex_flood_ctcp_num)	/*if we reached the maximun numbers of ctcp in the seconds limits */
				{
					serv.ctcp_last_time = current_time;	/*we got the flood, restore all the vars for next one */
					serv.ctcp_counter = 0;
					for (i = 0; i < 128; i++)
						if (ip[i] == '@')
							break;
					snprintf (real_ip, sizeof (real_ip), "*!*%s", &ip[i]);

					snprintf (buf, sizeof (buf),
								 _("You are being CTCP flooded from %s, ignoring %s\n"),
								 nick, real_ip);
					PrintText (sess, buf);

					/* ignore CTCP */
					ignore_add(real_ip, ignore::IG_CTCP, FALSE);
					return false;
				}
			}
		}
	} else
	{
		if (serv.msg_last_time == 0)
		{
			serv.msg_last_time = time (NULL);
			serv.ctcp_counter++;
		} else
		{
			if (difftime (current_time, serv.msg_last_time) <
				 prefs.hex_flood_msg_time)
			{
				serv.msg_counter++;
				if (serv.msg_counter == prefs.hex_flood_msg_num)	/*if we reached the maximun numbers of ctcp in the seconds limits */
				{
					snprintf (buf, sizeof (buf),
					 _("You are being MSG flooded from %s, setting gui_autoopen_dialog OFF.\n"),
								 ip);
					PrintText (sess, buf);
					serv.msg_last_time = current_time;	/*we got the flood, restore all the vars for next one */
					serv.msg_counter = 0;

					if (prefs.hex_gui_autoopen_dialog)
					{
						prefs.hex_gui_autoopen_dialog = 0;
						/* turn it back on in 30 secs */
						fe_timeout_add (30000, flood_autodialog_timeout, NULL);
					}
					return false;
				}
			}
		}
	}
	return true;
}

const std::vector<ignore>&
get_ignore_list()
{
	return ignores;
}

