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
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <locale>
#include <sstream>
#include <string>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <boost/algorithm/string.hpp>
#include <boost/format.hpp>
#include <boost/utility/string_ref.hpp>
#include <boost/filesystem/fstream.hpp>

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "notify.hpp"
#include "cfgfiles.hpp"
#include "fe.hpp"
#include "server.hpp"
#include "text.hpp"
#include "util.hpp"
#include "hexchatc.hpp"
#include "session.hpp"
#include "filesystem.hpp"


GSList *notify_list = nullptr;
int notify_tag = 0;

/* monitor this nick on this particular network? */

static bool notify_do_network (struct notify &notify, const server &serv)
{
	if (notify.networks.empty())	/* ALL networks for this nick */
		return true;
	std::string serv_str = serv.get_network(true).to_string();
	std::locale loc;
	serv_str.erase(
		std::remove_if(
		serv_str.begin(),
		serv_str.end(),
			[&loc](auto c) {return std::isspace<char>(c, loc); }),
		serv_str.end());
	return std::find_if(
		notify.networks.cbegin(),
		notify.networks.cend(),
		[&serv_str](const std::string & net){
			return rfc_casecmp(net.c_str(), serv_str.c_str()) == 0;
		}) == notify.networks.cend();
}

struct notify_per_server *
notify_find_server_entry (struct notify &notify, struct server &serv)
{
	for(auto & servnot : notify.server_list)
	{
		if (servnot.server == &serv)
			return &servnot;
	}

	/* not found, should we add it, or is this not a network where
	  we're monitoring this nick? */
	if (!notify_do_network (notify, serv))
		return nullptr;

	notify.server_list.emplace_back(notify_per_server{ &serv, &notify });
	return &notify.server_list.back();
}

void notify_save ()
{
	const auto path = io::fs::make_config_path("notify.conf");
	io::fs::create_file_with_mode(path, 0600);
	namespace bfs = boost::filesystem;
	bfs::ofstream out{ path, std::ios::trunc | std::ios::out | std::ios::binary | std::ios::app };
	if (!out)
	{
		return;
	}
	GSList *list = notify_list;
	while (list)
	{
		const auto notify = static_cast<struct notify *>(list->data);
		out << notify->name;
		if (!notify->networks.empty())
		{
			out << " " << boost::join(notify->networks, ",");
		}
		out << '\n';
		list = list->next;
	}
}

void notify_load ()
{
	const auto path = io::fs::make_config_path("notify.conf");
	namespace bfs = boost::filesystem;
	bfs::ifstream in{ path, std::ios::binary | std::ios::in };
	if (!in)
	{
		return;
	}

	for (std::string line; std::getline(in, line, '\n');)
	{
		if (boost::starts_with(line, "#")) {
			continue;
		}

		std::vector<std::string> parts;
		boost::split(parts, line, [](auto c) { return c == ' '; });
		if (parts.size() == 2) {
			notify_adduser(parts[0], parts[1]);
		}
		else if(parts.size() == 1) {
			notify_adduser(parts[0], {});
		}
	}
}

static notify_per_server * notify_find (server &serv, const std::string& nick)
{
	GSList *list = notify_list;
	while (list)
	{
		auto notify = static_cast<struct notify *>(list->data);

		auto servnot = notify_find_server_entry (*notify, serv);
		if (!servnot)
		{
			list = list->next;
			continue;
		}

		if (!serv.compare(notify->name, nick))
			return servnot;

		list = list->next;
	}

	return nullptr;
}

static void notify_announce_offline (server & serv, notify_per_server &servnot,
								 const boost::string_ref nick, bool quiet, 
								 const message_tags_data *tags_data)
{
	servnot.ison = false;
	servnot.lastoff = notify_per_server::clock::now();
	std::string mutable_nick = nick.to_string();
	if (!quiet)
	{
		auto net = serv.get_network(true).to_string();
		session *sess = serv.front_session;
		EMIT_SIGNAL_TIMESTAMP(XP_TE_NOTIFYOFFLINE, sess, &mutable_nick[0], serv.servername,
			&net[0], nullptr, 0,
			tags_data->timestamp);
	}
	fe_notify_update(&mutable_nick);
	fe_notify_update (nullptr);
}

static void notify_announce_online (server & serv, notify_per_server &servnot,
								const boost::string_ref nick, const message_tags_data *tags_data)
{
	servnot.lastseen = notify_per_server::clock::now();
	if (servnot.ison)
		return;
	
	session *sess = serv.front_session;
	
	servnot.ison = true;
	servnot.laston = notify_per_server::clock::now();
	auto mutable_nick = nick.to_string();
	auto mutable_net = serv.get_network(true).to_string();
	EMIT_SIGNAL_TIMESTAMP (XP_TE_NOTIFYONLINE, sess, &mutable_nick[0], serv.servername,
					 &mutable_net[0], nullptr, 0,
					 tags_data->timestamp);
	fe_notify_update (&mutable_nick);
	fe_notify_update (nullptr);

	if (prefs.hex_notify_whois_online)
	{
		/* Let's do whois with idle time (like in /quote WHOIS %s %s) */
		serv.p_whois ((boost::format("%s %s") % nick % nick).str());
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

	notify_announce_offline (serv, *servnot, nick, quiet, tags_data);
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

	notify_announce_online (serv, *servnot, nick, tags_data);
}

/* monitor can send lists for numeric 730/731 */

void
notify_set_offline_list (server & serv, const std::string & users, bool quiet,
						  const message_tags_data *tags_data)
{
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

		auto servnot = notify_find (serv, nick);
		if (servnot)
			notify_announce_offline (serv, *servnot, nick, quiet, tags_data);
	}
}

void notify_set_online_list (server & serv, const std::string& users,
						 const message_tags_data *tags_data)
{
	std::istringstream stream(users);
	for (std::string token; std::getline(stream, token, ',');)
	{
		auto pos = token.find_first_of('!');
		if (pos == std::string::npos || pos + 1 >= NICKLEN)
			continue;

		auto nick = token.substr(0, pos);
		auto servnot = notify_find (serv, nick);
		if (servnot)
			notify_announce_online (serv, *servnot, nick, tags_data);
	}
}

static void notify_watch (server * serv, const std::string& nick, bool add)
{
	if (!serv->supports_monitor && !serv->supports_watch)
		return;

	char addchar = add ? '+' : '-';

	std::ostringstream buf;
	if (serv->supports_monitor)
		buf << boost::format("MONITOR %c %s") % addchar % nick;
	else if (serv->supports_watch)
		buf << boost::format("WATCH %c%s") % addchar % nick;

	serv->p_raw (buf.str());
}

static void
notify_watch_all (struct notify &notify, bool add)
{
	GSList *list = serv_list;
	while (list)
	{
		auto serv = static_cast<server*>(list->data);
		if (serv->connected && serv->end_of_motd && notify_do_network (notify, *serv))
			notify_watch (serv, notify.name, add);
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
	const int format_len = serv.supports_monitor ? 1 : 2; /* just , for monitor or + and space for watch */
	std::vector<notify*> send_list;
	int len = 0;

	/* Only get the list for this network */
	auto list = notify_list;
	while (list)
	{
		auto notify = static_cast<struct notify*>(list->data);

		if (notify && notify_do_network (*notify, serv))
		{
			send_list.push_back(notify);
		}

		list = list->next;
	}

	/* Now send that list in batches */
	auto point = send_list.cbegin();
	for (auto it = send_list.cbegin(), end = send_list.cend(); it != end; ++it)
	{
		auto notify = *it;

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

void notify_markonline(server &serv, const char * const word[], const message_tags_data *tags_data)
{
	GSList *list = notify_list;
	int i;

	while (list)
	{
		auto notify = static_cast<struct notify *>(list->data);
		auto servnot = notify_find_server_entry (*notify, serv);
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
				notify_announce_online (serv, *servnot, notify->name, tags_data);
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
			notify_announce_offline (serv, *servnot, notify->name, false, tags_data);
		}
		list = list->next;
	}
	fe_notify_update (nullptr);
}

/* yuck! Old routine for ISON notify */

static void notify_checklist_for_server (server &serv)
{
	GSList *list = notify_list;
	int i = 0;
	std::ostringstream outbuf;
	outbuf << "ISON ";
	while (list)
	{
		auto notify = static_cast<struct notify*>(list->data);
		if (notify && notify_do_network (*notify, serv))
		{
			i++;
			outbuf << notify->name;
			outbuf << ' ';
			if (outbuf.tellp() > 460)
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
		serv.p_raw (outbuf.str());
}

int notify_checklist (void)	/* check ISON list */
{
	GSList *list = serv_list;

	while (list)
	{
		auto serv = static_cast<server*>(list->data);
		if (serv->connected && serv->end_of_motd && !serv->supports_watch && !serv->supports_monitor)
		{
			notify_checklist_for_server (*serv);
		}
		list = list->next;
	}
	return 1;
}

void notify_showlist (struct session *sess, const message_tags_data *tags_data)
{
	char outbuf[256] = {};
	GSList *list = notify_list;
	int i = 0;

	EMIT_SIGNAL_TIMESTAMP (XP_TE_NOTIFYHEAD, sess, nullptr, nullptr, nullptr, nullptr, 0,
								  tags_data->timestamp);
	while (list)
	{
		i++;
		auto notify = (struct notify *) list->data;
		auto servnot = notify_find_server_entry (*notify, *sess->server);
		if (servnot && servnot->ison)
			snprintf (outbuf, sizeof (outbuf), _("  %-20s online\n"), notify->name.c_str());
		else
			snprintf (outbuf, sizeof (outbuf), _("  %-20s offline\n"), notify->name.c_str());
		PrintTextTimeStamp (sess, outbuf, tags_data->timestamp);
		list = list->next;
	}
	if (i)
	{
		snprintf (outbuf, sizeof(outbuf), "%d", i);
		EMIT_SIGNAL_TIMESTAMP (XP_TE_NOTIFYNUMBER, sess, outbuf, nullptr, nullptr, nullptr,
									  0, tags_data->timestamp);
	} else
		EMIT_SIGNAL_TIMESTAMP (XP_TE_NOTIFYEMPTY, sess, nullptr, nullptr, nullptr, nullptr, 0,
									  tags_data->timestamp);
}

bool notify_deluser(const boost::string_ref name)
{
	GSList *list = notify_list;
	const auto nameStr = name.to_string();
	while (list)
	{
		auto notfy = static_cast<struct notify *>(list->data);
		if (!rfc_casecmp (notfy->name.c_str(), nameStr.c_str()))
		{
			std::unique_ptr<struct notify> note(notfy);
			fe_notify_update (&note->name);
			notify_list = g_slist_remove (notify_list, note.get());
			notify_watch_all (*note, false);
			fe_notify_update (nullptr);
			return true;
		}
		list = list->next;
	}
	return false;
}

void notify_adduser (boost::string_ref name, boost::string_ref networks)
{
	auto notif = std::make_unique<struct notify>(notify{ name.to_string() });
	if (!networks.empty())
	{
		auto netwks_str = networks.to_string();
		std::locale loc;
		netwks_str.erase(
			std::remove_if(
			netwks_str.begin(),
			netwks_str.end(),
			[&loc](auto c){ return std::isspace<char>(c, loc); }),
			netwks_str.end());
		boost::split(notif->networks, netwks_str, boost::is_any_of(","));
	}
	//notify->server_list = 0;
	notify_list = g_slist_prepend(notify_list, notif.get());
	struct notify* note = notif.release();
	notify_checklist();
	fe_notify_update(&note->name);
	fe_notify_update(nullptr);
	notify_watch_all(*note, true);
}

bool
notify_is_in_list (const server &serv, const std::string & name)
{
	GSList *list = notify_list;

	while (list)
	{
		auto notify = (struct notify *) list->data;
		if (!serv.compare(notify->name, name))
			return true;
		list = list->next;
	}

	return false;
}

//bool
//notify_isnotify (struct session *sess, const boost::string_ref name)
//{
//	GSList *list = notify_list;
//	const auto nameStr = name.to_string();
//	while (list)
//	{
//		auto notify = static_cast<struct notify *>(list->data);
//		if (!sess->server->p_cmp (notify->name.c_str(), nameStr.c_str()))
//		{
//			auto servnot = notify_find_server_entry (*notify, *sess->server);
//			if (servnot && servnot->ison)
//				return true;
//		}
//		list = list->next;
//	}
//
//	return false;
//}

void
notify_cleanup ()
{
	GSList *list = notify_list;

	while (list)
	{
		/* Traverse the list of notify structures */
		auto notify = static_cast<struct notify *>(list->data);
		notify->server_list.erase(std::remove_if(
			notify->server_list.begin(),
			notify->server_list.end(),
			[](const auto & servnot) {
			bool valid = false;
			auto srvlist = serv_list;
			while (srvlist)
			{
				const auto serv = static_cast<struct server *>(srvlist->data);
				if (servnot.server == serv)
				{
					valid = serv->connected;	/* Only valid if server is too */
					break;
				}
				srvlist = srvlist->next;
			}
			return !valid;
		}), notify->server_list.end());

		list = list->next;
	}
	fe_notify_update (nullptr);
}
