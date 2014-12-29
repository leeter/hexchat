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

#ifndef HEXCHAT_SESSION_HPP
#define HEXCHAT_SESSION_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "sessfwd.hpp"
#include "serverfwd.hpp"
#include "history.hpp"

struct session
{
	typedef int session_type;
	/* Session types */
	enum type : session_type{
		SESS_SERVER = 1,
		SESS_CHANNEL,
		SESS_DIALOG,
		SESS_NOTICES,
		SESS_SNOTICES
	};
	session(struct server *serv, const char *from, session_type type);
	~session();
	/* Per-Channel Alerts */
	/* use a byte, because we need a pointer to each element */
	std::uint8_t alert_beep;
	std::uint8_t alert_taskbar;
	std::uint8_t alert_tray;

	/* Per-Channel Settings */
	std::uint8_t text_hidejoinpart;
	std::uint8_t text_logging;
	std::uint8_t text_scrollback;
	std::uint8_t text_strip;

	struct server *server;
	std::vector<struct User*> usertree_alpha;			/* pure alphabetical tree */
	std::vector<std::unique_ptr<struct User>> usertree;		/* ordered with Ops first */
	struct User *me;					/* points to myself in the usertree */
	char channel[CHANLEN];
	char waitchannel[CHANLEN];		  /* waiting to join channel (/join sent) */
	char willjoinchannel[CHANLEN];	  /* will issue /join for this channel */
	char session_name[CHANLEN];		 /* the name of the session, should not modified */
	char channelkey[64];			  /* XXX correct max length? */
	int limit;						  /* channel user limit */
	int logfd;
	int scrollfd;							/* scrollback filedes */
	int scrollwritten;					/* number of lines written */

	char lastnick[NICKLEN];			  /* last nick you /msg'ed */

	history hist;
	std::string name;

	int ops;								/* num. of ops in channel */
	int hops;						  /* num. of half-oped users */
	int voices;							/* num. of voiced people */
	int total;							/* num. of users in channel */

	std::string quitreason;
	std::string topic;
	std::string current_modes;

	int mode_timeout_tag;

	struct session *lastlog_sess;
	struct nbexec *running_exec;

	struct session_gui *gui;		/* initialized by fe_new_window */
	struct restore_gui *res;

	session_type type;					/* SESS_* */

	int lastact_idx;		/* the sess_list_by_lastact[] index of the list we're in.
							* For valid values, see defines of LACT_*. */

	bool new_data;			/* new data avail? (purple tab) */
	bool nick_said;		/* your nick mentioned? (blue tab) */
	bool msg_said;			/* new msg available? (red tab) */

	bool ignore_date;
	bool ignore_mode;
	bool ignore_names;
	bool end_of_names;
	bool doing_who;		/* /who sent on this channel */
	bool done_away_check;	/* done checking for away status changes */
	gtk_xtext_search_flags lastlog_flags;
	void(*scrollback_replay_marklast) (struct session *sess);
};

session * new_ircwindow(server *serv, const char *name, session::session_type type, int focus);

#endif // HEXCHAT_SESSION_HPP