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

#include <algorithm>
#include <vector>
#include <sstream>
#include <string>
#include <locale>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctime>
#include <functional>
#include <algorithm>
#include <boost/algorithm/string.hpp>

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "hexchat.hpp"
#include "notify.hpp"
#include "cfgfiles.hpp"
#include "fe.hpp"
#include "server.hpp"
#include "text.hpp"
#include "util.hpp"
#include "hexchatc.hpp"


GSList *notify_list = 0;
int notify_tag = 0;

/* monitor this nick on this particular network? */

static bool
notify_do_network (struct notify *notify, const server &serv)
{
	if (notify->networks.empty())	/* ALL networks for this nick */
		return true;
	std::string serv_str(serv.get_network(true));
	serv_str.erase(
		std::remove_if(
		serv_str.begin(),
		serv_str.end(),
		std::bind(std::isspace<char>, std::placeholders::_1, std::locale())),
		serv_str.end());
	return std::find_if(
		notify->networks.cbegin(),
		notify->networks.cend(),
		[&serv_str](const std::string & net){
			return rfc_casecmp(net.c_str(), serv_str.c_str()) == 0;
		}) == notify->networks.cend();
}

struct notify_per_server *
notify_find_server_entry (struct notify *notify, struct server &serv)
{
	GSList *list = notify->server_list;
	struct notify_per_server *servnot;

	while (list)
	{
		servnot = (struct notify_per_server *) list->data;
		if (servnot->server == &serv)
			return servnot;
		list = list->next;
	}

	/* not found, should we add it, or is this not a network where
      we're monitoring this nick? */
	if (!notify_do_network (notify, serv))
		return NULL;

	servnot = new notify_per_server();
	servnot->server = &serv;
	servnot->notify = notify;
	notify->server_list = g_slist_prepend(notify->server_list, servnot);
	return servnot;
}

void
notify_save (void)
{
	int fh;
	struct notify *notify;
	GSList *list = notify_list;

	fh = hexchat_open_file ("notify.conf", O_TRUNC | O_WRONLY | O_CREAT, 0600, XOF_DOMODE);
	if (fh != -1)
	{
		while (list)
		{
			notify = (struct notify *) list->data;
			write (fh, notify->name.c_str(), notify->name.size());
			if (!notify->networks.empty())
			{
				write (fh, " ", 1);
				auto result = boost::join(notify->networks, ",");
				write (fh, result.c_str(), result.size());
			}
			write (fh, "\n", 1);
			list = list->next;
		}
		close (fh);
	}
}

void
notify_load (void)
{
	int fh;
	char buf[256];
	char *sep;

	fh = hexchat_open_file ("notify.conf", O_RDONLY, 0, 0);
	if (fh != -1)
	{
		while (waitline (fh, buf, sizeof buf, FALSE) != -1)
		{
			if (buf[0] != '#' && buf[0] != 0)
			{
				sep = strchr (buf, ' ');
				if (sep)
				{
					sep[0] = 0;
					notify_adduser (buf, sep + 1);					
				}
				else
					notify_adduser (buf, NULL);
			}
		}
		close (fh);
	}
}

static struct notify_per_server *
notify_find (server &serv, const std::string& nick)
{
	GSList *list = notify_list;
	struct notify_per_server *servnot;
	struct notify *notify;

	while (list)
	{
		notify = (struct notify *) list->data;

		servnot = notify_find_server_entry (notify, serv);
		if (!servnot)
		{
			list = list->next;
			continue;
		}

		if (!serv.compare(notify->name, nick))
			return servnot;

		list = list->next;
	}

	return 0;
}

static void
notify_announce_offline (server & serv, struct notify_per_server *servnot,
								 const std::string &nick, bool quiet, 
								 const message_tags_data *tags_data)
{
	session *sess;

	sess = serv.front_session;

	servnot->ison = false;
	servnot->lastoff = time (0);
	std::string mutable_nick(nick);
	if (!quiet)
		EMIT_SIGNAL_TIMESTAMP (XP_TE_NOTIFYOFFLINE, sess, &mutable_nick[0], serv.servername,
									  serv.get_network (true), NULL, 0,
									  tags_data->timestamp);
	fe_notify_update(&mutable_nick[0]);
	fe_notify_update (0);
}

static void
notify_announce_online (server & serv, struct notify_per_server *servnot,
								const std::string& nick, const message_tags_data *tags_data)
{
	session *sess;

	sess = serv.front_session;

	servnot->lastseen = time (0);
	if (servnot->ison)
		return;

	servnot->ison = true;
	servnot->laston = time (0);
	std::string mutable_nick = nick;
	mutable_nick.push_back(0);
	EMIT_SIGNAL_TIMESTAMP (XP_TE_NOTIFYONLINE, sess, &mutable_nick[0], serv.servername,
					 serv.get_network (true), NULL, 0,
					 tags_data->timestamp);
	fe_notify_update (&mutable_nick[0]);
	fe_notify_update (0);

	if (prefs.hex_notify_whois_online)
	{

	    /* Let's do whois with idle time (like in /quote WHOIS %s %s) */
		std::string wii_str(nick.size() * 2 + 2, '\0');
		sprintf(&wii_str[0], "%s %s", nick.c_str(), nick.c_str());
		serv.p_whois (wii_str);
	}
}

/* handles numeric 601 */

void
notify_set_offline(server & serv, const std::string & nick, bool quiet,
						  const message_tags_data *tags_data)
{
	struct notify_per_server *servnot;

	servnot = notify_find (serv, nick);
	if (!servnot)
		return;

	notify_announce_offline (serv, servnot, nick, quiet, tags_data);
}

/* handles numeric 604 and 600 */

void
notify_set_online (server & serv, const std::string& nick,
						 const message_tags_data *tags_data)
{
	struct notify_per_server *servnot;

	servnot = notify_find (serv, nick);
	if (!servnot)
		return;

	notify_announce_online (serv, servnot, nick, tags_data);
}

/* monitor can send lists for numeric 730/731 */

void
notify_set_offline_list (server & serv, const std::string & users, bool quiet,
						  const message_tags_data *tags_data)
{
	struct notify_per_server *servnot;
	//char nick[NICKLEN] = { 0 };

	std::istringstream stream(users);
	for (std::string token; std::getline(stream, token, ',');)
	{
		auto pos = token.find_first_of('!');
		if (pos == std::string::npos)
			continue;

		if (pos + 1 >= NICKLEN)
			continue;
		auto nick = token.substr(0, pos);
		//std::copy_n(token.cbegin(), pos, std::begin(nick));

		servnot = notify_find (serv, nick);
		if (servnot)
			notify_announce_offline (serv, servnot, nick, quiet, tags_data);
	}
}

void
notify_set_online_list (server & serv, const std::string& users,
						 const message_tags_data *tags_data)
{
	struct notify_per_server *servnot;
	char nick[NICKLEN] = { 0 };

	std::istringstream stream(users);
	for (std::string token; std::getline(stream, token, ',');)
	{
		auto pos = token.find_first_of('!');
		if (pos == std::string::npos)
			continue;

		if (pos + 1 >= sizeof(nick))
			continue;

		std::copy_n(token.cbegin(), pos, std::begin(nick));

		servnot = notify_find (serv, nick);
		if (servnot)
			notify_announce_online (serv, servnot, nick, tags_data);
	}
}

static void
notify_watch (server * serv, const std::string& nick, bool add)
{
	char tbuf[256];
	char addchar = '+';

	if (!add)
		addchar = '-';

	if (serv->supports_monitor)
		snprintf (tbuf, sizeof (tbuf), "MONITOR %c %s", addchar, nick.c_str());
	else if (serv->supports_watch)
		snprintf (tbuf, sizeof (tbuf), "WATCH %c%s", addchar, nick.c_str());
	else
		return;

	serv->p_raw (tbuf);
}

static void
notify_watch_all (struct notify *notify, bool add)
{
	server *serv;
	GSList *list = serv_list;
	while (list)
	{
		serv = static_cast<server*>(list->data);
		if (serv->connected && serv->end_of_motd && notify_do_network (notify, *serv))
			notify_watch (serv, notify->name, add);
		list = list->next;
	}
}

static void
notify_flush_watches(server & serv, std::vector<struct notify*>::const_iterator from, std::vector<struct notify*>::const_iterator end)
{
	std::ostringstream buffer;
	buffer << (serv.supports_monitor ? "MONITOR + " : "WATCH");
	auto it = from;
	buffer << (*it)->name;
	++it;
	for (;it != end; ++it)
	{
		struct notify *notify = *it;
		if (serv.supports_monitor)
			buffer << ",";
		else
			buffer << " +";
		buffer << notify->name;
	}
	serv.p_raw (buffer.str());
}

/* called when logging in. e.g. when End of motd. */

void
notify_send_watches (server & serv)
{
	struct notify *notify;
	const int format_len = serv.supports_monitor ? 1 : 2; /* just , for monitor or + and space for watch */
	GSList *list;
	std::vector<struct notify*> send_list;
	int len = 0;

	/* Only get the list for this network */
	list = notify_list;
	while (list)
	{
		notify = static_cast<struct notify*>(list->data);

		if (notify_do_network (notify, serv))
		{
			send_list.push_back(notify);
		}

		list = list->next;
	}

	/* Now send that list in batches */
	auto point = send_list.cbegin();
	for (auto it = send_list.cbegin(); it != send_list.cend(); ++it)
	{
		notify = *it;

		len += notify->name.size() + format_len;
		if (len > 500)
		{
			/* Too long send existing list */
			notify_flush_watches (serv, send_list.begin(), it);
			len = notify->name.size() + format_len;
			point = it; /* We left off here */
		}
	}

	if (len) /* We had leftovers under 500, send them all */
	{
		notify_flush_watches (serv, point, send_list.cend());
	}
}

/* called when receiving a ISON 303 - should this func go? */

void
notify_markonline(server &serv, const char * const word[], const message_tags_data *tags_data)
{
	struct notify *notify;
	struct notify_per_server *servnot;
	GSList *list = notify_list;
	int i;

	while (list)
	{
		notify = (struct notify *) list->data;
		servnot = notify_find_server_entry (notify, serv);
		if (!servnot)
		{
			list = list->next;
			continue;
		}
		i = 4;
		bool seen = false;
		while (*word[i])
		{
			if (!serv.p_cmp (notify->name.c_str(), word[i]))
			{
				seen = true;
				notify_announce_online (serv, servnot, notify->name, tags_data);
				break;
			}
			i++;
			/* FIXME: word[] is only a 32 element array, limits notify list to
			   about 27 people */
			if (i > PDIWORDS - 5)
			{
				/*fprintf (stderr, _("*** HEXCHAT WARNING: notify list too large.\n"));*/
				break;
			}
		}
		if (!seen && servnot->ison)
		{
			notify_announce_offline (serv, servnot, notify->name, FALSE, tags_data);
		}
		list = list->next;
	}
	fe_notify_update (0);
}

/* yuck! Old routine for ISON notify */

static void
notify_checklist_for_server (server &serv)
{
	char outbuf[512] = { 0 };
	struct notify *notify;
	GSList *list = notify_list;
	int i = 0;

	strcpy (outbuf, "ISON ");
	while (list)
	{
		notify = static_cast<struct notify*>(list->data);
		if (notify_do_network (notify, serv))
		{
			i++;
			strcat (outbuf, notify->name.c_str());
			strcat (outbuf, " ");
			if (strlen (outbuf) > 460)
			{
				/* LAME: we can't send more than 512 bytes to the server, but     *
				 * if we split it in two packets, our offline detection wouldn't  *
				 work                                                           */
				/*fprintf (stderr, _("*** HEXCHAT WARNING: notify list too large.\n"));*/
				break;
			}
		}
		list = list->next;
	}

	if (i)
		serv.p_raw (outbuf);
}

int
notify_checklist (void)	/* check ISON list */
{
	struct server *serv;
	GSList *list = serv_list;

	while (list)
	{
		serv = static_cast<server*>(list->data);
		if (serv->connected && serv->end_of_motd && !serv->supports_watch && !serv->supports_monitor)
		{
			notify_checklist_for_server (*serv);
		}
		list = list->next;
	}
	return 1;
}

void
notify_showlist (struct session *sess, const message_tags_data *tags_data)
{
	char outbuf[256];
	struct notify *notify;
	GSList *list = notify_list;
	struct notify_per_server *servnot;
	int i = 0;

	EMIT_SIGNAL_TIMESTAMP (XP_TE_NOTIFYHEAD, sess, NULL, NULL, NULL, NULL, 0,
								  tags_data->timestamp);
	while (list)
	{
		i++;
		notify = (struct notify *) list->data;
		servnot = notify_find_server_entry (notify, *sess->server);
		if (servnot && servnot->ison)
			snprintf (outbuf, sizeof (outbuf), _("  %-20s online\n"), notify->name.c_str());
		else
			snprintf (outbuf, sizeof (outbuf), _("  %-20s offline\n"), notify->name.c_str());
		PrintTextTimeStamp (sess, outbuf, tags_data->timestamp);
		list = list->next;
	}
	if (i)
	{
		sprintf (outbuf, "%d", i);
		EMIT_SIGNAL_TIMESTAMP (XP_TE_NOTIFYNUMBER, sess, outbuf, NULL, NULL, NULL,
									  0, tags_data->timestamp);
	} else
		EMIT_SIGNAL_TIMESTAMP (XP_TE_NOTIFYEMPTY, sess, NULL, NULL, NULL, NULL, 0,
									  tags_data->timestamp);
}

bool
notify_deluser (const char *name)
{
	struct notify *notify;
	struct notify_per_server *servnot;
	GSList *list = notify_list;

	while (list)
	{
		notify = (struct notify *) list->data;
		if (!rfc_casecmp (notify->name.c_str(), name))
		{
			fe_notify_update (&notify->name[0]);
			/* Remove the records for each server */
			while (notify->server_list)
			{
				servnot = (struct notify_per_server *) notify->server_list->data;
				notify->server_list =
					g_slist_remove (notify->server_list, servnot);
				delete servnot;
			}
			notify_list = g_slist_remove (notify_list, notify);
			notify_watch_all (notify, false);
			/*if (notify->networks)
				free (notify->networks);
			free (notify->name);*/
			delete notify;
			fe_notify_update (0);
			return true;
		}
		list = list->next;
	}
	return false;
}

void
notify_adduser (const char *name, const char *networks)
{
	struct notify *notify = new struct notify;
	if (notify)
	{
		notify->name = name;
		//if (strlen (name) >= NICKLEN)
		//{
		//	// static_cast<char*>(malloc(NICKLEN));
		//	//safe_strcpy (notify->name., name, NICKLEN);
		//} else
		//{
		//	notify->name = strdup (name);
		//}
		if (networks)
		{
			std::string netwks_str(networks);
			netwks_str.erase(
				std::remove_if(
				netwks_str.begin(),
				netwks_str.end(),
				std::bind(std::isspace<char>, std::placeholders::_1, std::locale())),
				netwks_str.end());
			boost::split(notify->networks, netwks_str, boost::is_any_of(","));
		}
		notify->server_list = 0;
		notify_list = g_slist_prepend (notify_list, notify);
		notify_checklist ();
		fe_notify_update (&notify->name[0]);
		fe_notify_update (0);
		notify_watch_all (notify, TRUE);
	}
}

bool
notify_is_in_list (const server &serv, const std::string & name)
{
	struct notify *notify;
	GSList *list = notify_list;

	while (list)
	{
		notify = (struct notify *) list->data;
		if (!serv.compare(notify->name, name))
			return true;
		list = list->next;
	}

	return false;
}

bool
notify_isnotify (struct session *sess, const char *name)
{
	struct notify *notify;
	struct notify_per_server *servnot;
	GSList *list = notify_list;

	while (list)
	{
		notify = (struct notify *) list->data;
		if (!sess->server->p_cmp (notify->name.c_str(), name))
		{
			servnot = notify_find_server_entry (notify, *sess->server);
			if (servnot && servnot->ison)
				return true;
		}
		list = list->next;
	}

	return false;
}

void
notify_cleanup ()
{
	GSList *list = notify_list;
	GSList *nslist, *srvlist;
	struct notify *notify;
	struct notify_per_server *servnot;
	struct server *serv;
	bool valid;

	while (list)
	{
		/* Traverse the list of notify structures */
		notify = (struct notify *) list->data;
		nslist = notify->server_list;
		while (nslist)
		{
			/* Look at each per-server structure */
			servnot = (struct notify_per_server *) nslist->data;

			/* Check the server is valid */
			valid = false;
			srvlist = serv_list;
			while (srvlist)
			{
				serv = (struct server *) srvlist->data;
				if (servnot->server == serv)
				{
					valid = serv->connected;	/* Only valid if server is too */
					break;
				}
				srvlist = srvlist->next;
			}
			if (!valid)
			{
				notify->server_list =
					g_slist_remove (notify->server_list, servnot);
				free (servnot);
				nslist = notify->server_list;
			} else
			{
				nslist = nslist->next;
			}
		}
		list = list->next;
	}
	fe_notify_update (0);
}
