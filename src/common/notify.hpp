/* HexChat
 * Copyright (C) 1998-2010 Peter Zelezny.
 * Copyright (C) 2009-2013 Berke Viktor.
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

#ifndef HEXCHAT_NOTIFY_HPP
#define HEXCHAT_NOTIFY_HPP

#include <string>
#include <vector>
#include "proto-irc.hpp"

struct notify
{
	std::string name;
	std::vector<std::string> networks;
	GSList *server_list;
};

struct notify_per_server
{
	struct server *server;
	struct notify *notify;
	time_t laston;
	time_t lastseen;
	time_t lastoff;
	bool ison;
};

extern GSList *notify_list;
extern int notify_tag;

/* the WATCH stuff */
void notify_set_online(server & serv, const std::string &nick,
								const message_tags_data *tags_data);
void notify_set_offline (server & serv, const std::string & nick, bool quiet,
								 const message_tags_data *tags_data);
/* the MONITOR stuff */
void notify_set_online_list (server & serv, const std::string& users,
								const message_tags_data *tags_data);
void notify_set_offline_list (server & serv, const std::string& users, bool quiet,
								 const message_tags_data *tags_data);
void notify_send_watches (server & serv);

/* the general stuff */
void notify_adduser (const char *name, const char *networks);
bool notify_deluser (const char *name);
void notify_cleanup (void);
void notify_load (void);
void notify_save (void);
void notify_showlist (session *sess, const message_tags_data *tags_data);
bool notify_is_in_list (const server &serv, const std::string & name);
bool notify_isnotify (session *sess, const char *name);
struct notify_per_server *notify_find_server_entry (struct notify *notify, server &serv);

/* the old ISON stuff - remove me? */
void notify_markonline (server &serv, const char * const word[], 
								const message_tags_data *tags_data);
int notify_checklist (void);

#endif
