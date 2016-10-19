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
#include <chrono>
#include "proto-irc.hpp"
#include "serverfwd.hpp"
#include "sessfwd.hpp"
#include <boost/utility/string_ref_fwd.hpp>
#include <gsl.h>

struct notify_per_server;

struct notify
{
	std::string name;
	std::vector<std::string> networks;
	std::vector<notify_per_server> server_list;
};

struct notify_per_server
{
	using clock = std::chrono::system_clock;
	using time_point = clock::time_point;

	gsl::not_null<server*> server;
	gsl::not_null<notify*> notify;
	time_point laston;
	time_point lastseen;
	time_point lastoff;
	bool ison;
};
extern int notify_tag;

gsl::span<notify> get_notifies() noexcept;

/* the WATCH stuff */
void notify_set_online(server & serv, const std::string &nick,
								const message_tags_data &tags_data);
void notify_set_offline (server & serv, const std::string & nick, bool quiet,
								 const message_tags_data &tags_data);
/* the MONITOR stuff */
void notify_set_online_list (server & serv, const std::string& users,
								const message_tags_data *tags_data);
void notify_set_offline_list (server & serv, const std::string& users, bool quiet,
								 const message_tags_data &tags_data);
void notify_send_watches (server & serv);

/* the general stuff */
void notify_adduser (boost::string_ref name, boost::string_ref networks);
bool notify_deluser (const boost::string_ref name);
void notify_cleanup (void);
void notify_load ();
void notify_save ();
void notify_showlist (session &sess, const message_tags_data &tags_data);
bool notify_is_in_list (const server &serv, const std::string & name);
//bool notify_isnotify (session *sess, const boost::string_ref name);
struct notify_per_server *notify_find_server_entry (struct notify &notify, server &serv);

/* the old ISON stuff - remove me? */
void notify_markonline (server &serv, const gsl::span<const char*> word, 
								const message_tags_data &tags_data);
int notify_checklist (void);

#endif
