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

#ifndef HEXCHAT_USERLIST_HPP
#define HEXCHAT_USERLIST_HPP

#include <ctime>
#include "proto-irc.hpp"

struct User
{
	User();
	~User();
	char nick[NICKLEN];
	char *hostname;
	char *realname;
	char *servername;
	char *account;
	time_t lasttalk;
	unsigned int access;	/* axs bit field */
	char prefix[2]; /* @ + % */
	bool op;
	bool hop;
	bool voice;
	bool me;
	bool away;
	bool selected;
};
struct server;
struct session;

const int USERACCESS_SIZE = 12;

bool userlist_add_hostname (session *sess, const char nick[],
									const char hostname[], const char realname[],
									const char servername[], const char account[], unsigned int away);
void userlist_set_away (session *sess, const char nick[], bool away);
void userlist_set_account (session *sess, const char nick[], const char account[]);
struct User *userlist_find (session *sess, const char name[]);
struct User *userlist_find_global (server *serv, const char name[]);
void userlist_clear (session *sess);
void userlist_free (session &sess);
void userlist_add (session *sess, const char name[], const char hostname[], const char account[],
						const char realname[], const message_tags_data *tags_data);
bool userlist_remove (session *sess, const char name[]);
void userlist_remove_user (session *sess, struct User *user);
bool userlist_change (session *sess, const char oldname[], const char newname[]);
void userlist_update_mode (session *sess, const char name[], char mode, char sign);
GSList *userlist_flat_list (session *sess);
GList *userlist_double_list (session *sess);
void userlist_rehash (session *sess);

#endif
