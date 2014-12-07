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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include "hexchat.hpp"
#include "modes.hpp"
#include "fe.hpp"
#include "notify.hpp"
#include "hexchatc.hpp"
#include "server.hpp"
#include "util.hpp"


User::~User()
{
	free(this->realname);
	free(this->hostname);
	free(this->servername);
	free(this->account);
}

User::User()
	:nick(),
	hostname(),
	realname(),
	servername(),
	account(),
	lasttalk(),
	access(),	/* axs bit field */
	prefix(),	/* @ + % */
	op(),
	hop(),
	voice(),
	me(),
	away(),
	selected()
{}

namespace{

	static int
		nick_cmp_az_ops(const server &serv, const User & user1, const User & user2)
	{
		unsigned int access1 = user1.access;
		unsigned int access2 = user2.access;

		if (access1 != access2)
		{
			for (int pos = 0; pos < USERACCESS_SIZE; pos++)
			{
				if ((access1 & (1 << pos)) && (access2 & (1 << pos)))
					break;
				if ((access1 & (1 << pos)) && !(access2 & (1 << pos)))
					return -1;
				if (!(access1 & (1 << pos)) && (access2 & (1 << pos)))
					return 1;
			}
		}

		return serv.p_cmp(user1.nick, user2.nick);
	}

	static int
		nick_cmp_alpha(struct User *user1, struct User *user2, server *serv)
	{
		return serv->p_cmp(user1->nick, user2->nick);
	}

	static int
		nick_cmp(const User &user1, const User &user2, const server &serv)
	{
		switch (prefs.hex_gui_ulist_sort)
		{
		case 0:
			return nick_cmp_az_ops(serv, user1, user2);
		case 1:
			return serv.p_cmp(user1.nick, user2.nick);
		case 2:
			return -1 * nick_cmp_az_ops(serv, user1, user2);
		case 3:
			return -1 * serv.p_cmp(user1.nick, user2.nick);
		default:
			return -1;
		}
	}


	static int
		userlist_resort(session & sess, const char nick[])
	{
		std::sort(
			sess.usertree.begin(),
			sess.usertree.end(),
			[&sess](const std::unique_ptr<User> &a, const std::unique_ptr<User> &b)
		{
			return nick_cmp(*a, *b, *sess.server) < 0;
		});

		std::sort(
			sess.usertree_alpha.begin(),
			sess.usertree_alpha.end(),
			[&sess](const User* a, const User* b){
			return sess.server->p_cmp(a->nick, b->nick) < 0;
		});
		if (!nick)
			return 0;
		auto result = std::find_if(
			sess.usertree.cbegin(),
			sess.usertree.cend(),
			[nick, &sess](const std::unique_ptr<User> & ptr){
			return sess.server->p_cmp(nick, ptr->nick) == 0;
		});
		return std::distance(sess.usertree.cbegin(), result);
	}

	/*
	insert name in appropriate place in linked list. Returns row number or:
	-1: duplicate
	*/
	static int
		userlist_insertname(session *sess, std::unique_ptr<User> newuser)
	{
		auto result = std::find_if(
			sess->usertree.cbegin(),
			sess->usertree.cend(),
			[sess, &newuser](const std::unique_ptr<User>& a){
			return sess->server->p_cmp(a->nick, newuser->nick) == 0;
		});
		if (result != sess->usertree.cend())
		{
			return -1;
		}

		const char* nick = newuser->nick;
		sess->usertree.emplace_back(std::move(newuser));
		sess->usertree_alpha.emplace_back(sess->usertree.back().get());
		return userlist_resort(*sess, nick);
	}
} // end anonymous namespace

void
userlist_set_away (struct session *sess, const char nick[], bool away)
{
	auto user = userlist_find (sess, nick);
	if (user)
	{
		if (user->away != away)
		{
			user->away = away;
			/* rehash GUI */
			fe_userlist_rehash (sess, user);
			if (away)
				fe_userlist_update (sess, user);
		}
	}
}

void
userlist_set_account (struct session *sess, const char nick[], const char account[])
{
	auto user = userlist_find (sess, nick);
	if (user)
	{
		free (user->account);
			
		if (strcmp (account, "*") == 0)
			user->account = nullptr;
		else
			user->account = strdup (account);
			
		/* gui doesnt currently reflect login status, maybe later
		fe_userlist_rehash (sess, user); */
	}
}

bool
userlist_add_hostname (struct session *sess, const char nick[], const char hostname[],
							const char realname[], const char servername[], const char account[], unsigned int away)
{
	auto user = userlist_find (sess, nick);
	if (user)
	{
		bool do_rehash = false;
		if (!user->hostname && hostname)
		{
			if (prefs.hex_gui_ulist_show_hosts)
				do_rehash = true;
			user->hostname = strdup (hostname);
		}
		if (!user->realname && realname && *realname)
			user->realname = strdup (realname);
		if (!user->servername && servername)
			user->servername = strdup (servername);
		if (!user->account && account && strcmp (account, "0") != 0)
			user->account = strdup (account);
		if (away != 0xff)
		{
			bool actually_away = !!away;
			if (user->away != actually_away)
				do_rehash = true;
			user->away = actually_away;
		}

		fe_userlist_update (sess, user);
		if (do_rehash)
			fe_userlist_rehash (sess, user);

		return true;
	}
	return false;
}

void
userlist_free (session &sess)
{
	sess.usertree_alpha.clear();
	sess.usertree.clear();

	sess.me = nullptr;

	sess.ops = 0;
	sess.hops = 0;
	sess.voices = 0;
	sess.total = 0;
}

void
userlist_clear (session *sess)
{
	fe_userlist_clear (*sess);
	userlist_free (*sess);
	fe_userlist_numbers (*sess);
}

struct User *
userlist_find (struct session *sess, const char name[])
{
	auto result = std::find_if(
		sess->usertree_alpha.cbegin(),
		sess->usertree_alpha.cend(),
		[sess, name](const User* u){
			return sess->server->p_cmp(name, u->nick) == 0;
		});
	if (result != sess->usertree_alpha.cend())
		return *result;

	return nullptr;
}

struct User *
userlist_find_global (struct server *serv, const char name[])
{
	GSList *list = sess_list;
	while (list)
	{
		session *sess = static_cast<session *>(list->data);
		if (sess->server == serv)
		{
			auto user = userlist_find(sess, name);
			if (user)
				return user;
		}
		list = list->next;
	}
	return nullptr;
}

static void
update_counts (session *sess, struct User *user, char prefix,
					bool level, int offset)
{
	switch (prefix)
	{
	case '@':
		user->op = level;
		sess->ops += offset;
		break;
	case '%':
		user->hop = level;
		sess->hops += offset;
		break;
	case '+':
		user->voice = level;
		sess->voices += offset;
		break;
	}
}

void
userlist_update_mode (session *sess, const char name[], char mode, char sign)
{
	auto result = std::find_if(
		sess->usertree.begin(),
		sess->usertree.end(),
		[sess, name](const std::unique_ptr<User> &u){
		return sess->server->p_cmp(name, u->nick) == 0;
	});
	if (result == sess->usertree.end())
		return;

	User * user = result->get();

	/* which bit number is affected? */
	char prefix;
	auto access = mode_access (sess->server, mode, &prefix);
	bool level = false;
	int offset = 0;
	if (sign == '+')
	{
		level = true;
		if (!(user->access & (1 << access)))
		{
			offset = 1;
			user->access |= (1 << access);
		}
	} else
	{
		level = false;
		if (user->access & (1 << access))
		{
			offset = -1;
			user->access &= ~(1 << access);
		}
	}

	/* now what is this users highest prefix? e.g. @ for ops */
	user->prefix[0] = get_nick_prefix (sess->server, user->access);

	/* update the various counts using the CHANGED prefix only */
	update_counts (sess, user, prefix, level, offset);
	
	int pos = userlist_resort(*sess, user->nick);

	/* let GTK move it too */
	fe_userlist_move (sess, user, pos);
	fe_userlist_numbers (*sess);
}

bool
userlist_change (struct session *sess, const char oldname[], const char newname[])
{
	auto user = std::find_if(
		sess->usertree.begin(),
		sess->usertree.end(),
		[sess, oldname](const std::unique_ptr<User> &u){
			return sess->server->p_cmp(oldname, u->nick) == 0;
		});
	if (user == sess->usertree.end())
		return false;


	safe_strcpy(user->get()->nick, newname, NICKLEN);
	User* user_ref = user->get();
	int pos = userlist_resort(*sess, user_ref->nick);
	fe_userlist_move(sess, user_ref, pos);
	fe_userlist_numbers(*sess);

	return true;
}

bool
userlist_remove (struct session *sess, const char name[])
{
	auto user = userlist_find (sess, name);
	if (!user)
		return false;

	userlist_remove_user (sess, user);
	return true;
}

void
userlist_remove_user (struct session *sess, struct User *user)
{
	if (user->voice)
		sess->voices--;
	if (user->op)
		sess->ops--;
	if (user->hop)
		sess->hops--;
	sess->total--;
	fe_userlist_numbers (*sess);
	fe_userlist_remove (sess, user);

	if (user == sess->me)
		sess->me = nullptr;

	sess->usertree_alpha.erase(
		std::remove(
			sess->usertree_alpha.begin(),
			sess->usertree_alpha.end(),
			user));
	sess->usertree.erase(
		std::remove_if(
			sess->usertree.begin(),
			sess->usertree.end(),
			[user](const std::unique_ptr<User>& ptr){
				return user == ptr.get();
			}));
}

void
userlist_add (struct session *sess, const char name[], const char hostname[],
				const char account[], const char realname[], const message_tags_data *tags_data)
{
	int prefix_chars;
	auto acc = nick_access (sess->server, name, &prefix_chars);

	notify_set_online (*sess->server, name + prefix_chars, tags_data);

	std::unique_ptr<User> user(new User());

	user->access = acc;

	/* assume first char is the highest level nick prefix */
	if (prefix_chars)
		user->prefix[0] = name[0];

	/* add it to our linked list */
	if (hostname)
		user->hostname = strdup (hostname);
	safe_strcpy (user->nick, name + prefix_chars, NICKLEN);
	/* is it me? */
	if (!sess->server->p_cmp (user->nick, sess->server->nick))
		user->me = true;
	/* extended join info */
	if (sess->server->have_extjoin)
	{
		if (account && *account)
			user->account = strdup (account);
		if (realname && *realname)
			user->realname = strdup (realname);
	}

	User * user_ref = user.get();
	auto row = userlist_insertname (sess, std::move(user));

	/* duplicate? some broken servers trigger this */
	if (row == -1)
	{
		return;
	}

	sess->total++;

	/* most ircds don't support multiple modechars in front of the nickname
	  for /NAMES - though they should. */
	while (prefix_chars)
	{
		update_counts (sess, user_ref, name[0], true, 1);
		name++;
		prefix_chars--;
	}

	if (user_ref->me)
		sess->me = user_ref;

	fe_userlist_insert(sess, user_ref, row, FALSE);
	fe_userlist_numbers (*sess);
}

void
userlist_rehash (session *sess)
{
	for (auto & user : sess->usertree_alpha)
		fe_userlist_rehash(sess, user);
}

GSList *
userlist_flat_list (session *sess)
{
	GSList *list = nullptr;
	for (auto & user : sess->usertree_alpha)
		list = g_slist_prepend(list, user);
	return g_slist_reverse (list);
}

GList *
userlist_double_list(session *sess)
{
	GList *list = nullptr;
	for (auto & user : sess->usertree_alpha)
		list = g_list_prepend(list, user);
	return list;
}
