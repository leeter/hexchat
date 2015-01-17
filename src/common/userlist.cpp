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
#include "userlist.hpp"
#include "session.hpp"

User::User()
	:lasttalk(),
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
		nick_cmp_az_ops(const std::locale &locale, const User & user1, const User & user2)
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
		auto& collate = std::use_facet<std::collate<char>>(locale);
		return collate.compare(user1.nick.c_str(), user1.nick.c_str() + user1.nick.size(), user2.nick.c_str(), user2.nick.c_str() + user2.nick.size());
	}

	static int
		nick_cmp_alpha(struct User *user1, struct User *user2, server *serv)
	{
		return serv->compare(user1->nick, user2->nick);
	}

	static int
		nick_cmp(const User &user1, const User &user2, const std::locale &locale)
	{
		auto& collate = std::use_facet<std::collate<char>>(locale);
		switch (prefs.hex_gui_ulist_sort)
		{
		case 0:
			return nick_cmp_az_ops(locale, user1, user2);
		case 1:
			return collate.compare(user1.nick.c_str(), user1.nick.c_str() + user1.nick.size(), user2.nick.c_str(), user2.nick.c_str() + user2.nick.size());
		case 2:
			return -1 * nick_cmp_az_ops(locale, user1, user2);
		case 3:
			return -1 * collate.compare(user1.nick.c_str(), user1.nick.c_str() + user1.nick.size(), user2.nick.c_str(), user2.nick.c_str() + user2.nick.size());
		default:
			return -1;
		}
	}


	static int userlist_resort(session & sess, const std::string & nick)
	{
		std::sort(
			sess.usertree.begin(),
			sess.usertree.end(),
			[&sess](const std::unique_ptr<User> &a, const std::unique_ptr<User> &b)
		{
			return nick_cmp(*a, *b, sess.server->current_locale()) < 0;
		});

		std::sort(
			sess.usertree_alpha.begin(),
			sess.usertree_alpha.end(),
			[&sess](const User* a, const User* b){
			return sess.server->current_locale()(a->nick, b->nick);
		});

		auto result = std::find_if(
			sess.usertree.cbegin(),
			sess.usertree.cend(),
			[nick, &sess](const std::unique_ptr<User> & ptr){
			return sess.server->compare(nick, ptr->nick) == 0;
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
			return sess->server->compare(a->nick, newuser->nick) == 0;
		});
		if (result != sess->usertree.cend())
		{
			return -1;
		}

		const std::string& nick = newuser->nick;
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
		if (strcmp (account, "*") == 0)
			user->account = boost::none;
		else
			user->account = std::string(account);
			
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
			user->hostname = hostname;
		}
		if (!user->realname && realname && *realname)
			user->realname = std::string(realname);
		if (!user->servername && servername)
			user->servername = std::string(servername);
		if (!user->account && account && strcmp (account, "0") != 0)
			user->account = std::string(account);
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
userlist_find (struct session *sess, const std::string & name)
{
	auto result = std::find_if(
		sess->usertree_alpha.cbegin(),
		sess->usertree_alpha.cend(),
		[sess, name](const User* u){
			return sess->server->compare(name, u->nick) == 0;
		});
	if (result != sess->usertree_alpha.cend())
		return *result;

	return nullptr;
}

struct User *
userlist_find_global (struct server *serv, const std::string & name)
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
		return sess->server->compare(name, u->nick) == 0;
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
userlist_change(struct session *sess, const std::string & oldname, const std::string & newname)
{
	auto user = std::find_if(
		sess->usertree.begin(),
		sess->usertree.end(),
		[sess, oldname](const std::unique_ptr<User> &u){
			return sess->server->compare(oldname, u->nick) == 0;
		});
	if (user == sess->usertree.end())
		return false;

	user->get()->nick = newname;
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
	auto acc = nick_access (sess->server, name, prefix_chars);

	notify_set_online (*sess->server, name + prefix_chars, tags_data);

	std::unique_ptr<User> user(new User());

	user->access = acc;

	/* assume first char is the highest level nick prefix */
	if (prefix_chars)
		user->prefix[0] = name[0];

	/* add it to our linked list */
	if (hostname)
		user->hostname = std::string(hostname);
	user->nick = (name + prefix_chars);
	/* is it me? */
	if (!sess->server->compare (user->nick, sess->server->nick))
		user->me = true;
	/* extended join info */
	if (sess->server->have_extjoin)
	{
		if (account && *account)
			user->account = std::string (account);
		if (realname && *realname)
			user->realname = std::string (realname);
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

	fe_userlist_insert(sess, user_ref, row, false);
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

class userlist::userlist_impl
{
	size_type ops_;								/* num. of ops in channel */
	size_type hops_;						  /* num. of half-oped users */
	size_type voices_;							/* num. of voiced people */
	// ordered with Ops first
	std::vector<std::unique_ptr<User>> users_;
	// pure alphabetical tree
	std::vector<User*> users_alpha_;
	std::locale locale_;
public:
	userlist_impl()
		:ops_(0),
		hops_(0),
		voices_(0)
	{}
public:
	size_type total() const
	{
		return users_.size();
	}
	size_type ops() const
	{
		return ops_;
	}
	size_type voices() const
	{
		return voices_;
	}
	size_type hops() const
	{
		return hops_;
	}
public:
	void imbue(const std::locale & locale)
	{
		this->locale_ = locale;
	}

	void sort()
	{
		std::sort(
			this->users_.begin(),
			this->users_.end(),
			[this](const std::unique_ptr<User> &a, const std::unique_ptr<User> &b)
		{
			return nick_cmp(*a, *b, this->locale_) < 0;
		});

		std::sort(
			this->users_alpha_.begin(),
			this->users_alpha_.end(),
			[this](const User* a, const User* b){
			return locale_(a->nick, b->nick);
			}
		);
	}

	std::pair<bool, size_type> insert(std::unique_ptr<User> user)
	{
		auto result = std::find_if(
			this->users_.cbegin(),
			this->users_.cend(),
			[&user, this](const std::unique_ptr<User>& a){
				return locale_(a->nick, user->nick);
			});
		if (result != this->users_.cend())
		{
			return std::make_pair(false, 0);
		}

		const std::string & nick = user->nick;
		this->users_.emplace_back(std::move(user));
		this->users_alpha_.emplace_back(this->users_.back().get());
		this->sort();

		return std::make_pair(true, this->users_.size());
	}

	boost::optional<const User&> find(const std::string & nick) const
	{
		auto result = std::find_if(
			users_alpha_.cbegin(),
			users_alpha_.cend(),
			[&nick, this](const User* u){
			return this->locale_(nick, u->nick);
		});
		if (result != users_alpha_.cend())
			return boost::optional<const User&>(*(*result));
		return boost::none;
	}

	boost::optional<User&> find(const std::string & nick)
	{
		auto result = const_cast<const userlist_impl&>(*this).find(nick);
		if (result)
		{
			User& res = const_cast<User&>(result.get());
			return boost::optional<User&>(res);
		}
		return boost::none;
	}

	void foreach_alpha(std::function<void(User&)> func)
	{

	}
};

userlist::userlist()
	:impl_(new userlist::userlist_impl)
{}

userlist::~userlist()
{}

userlist::size_type userlist::ops() const
{
	return impl_->ops();
}

userlist::size_type userlist::total() const
{
	return impl_->total();
}

userlist::size_type userlist::hops() const
{
	return impl_->hops();
}

userlist::size_type userlist::voices() const
{
	return impl_->voices();
}

void userlist::imbue(const std::locale & locale)
{
	impl_->imbue(locale);
}
