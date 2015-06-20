/* HexChat
* Copyright (C) 2014 Leetsoftwerx.
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

#include "session.hpp"

#include "chanopt.hpp"
#include "dcc.hpp"
#include "fe.hpp"
#include "hexchat.hpp"
#include "hexchatc.hpp"
#include "glist_iterators.hpp"
#include "notify.hpp"
#include "outbound.hpp"
#include "plugin.hpp"
#include "session_logging.hpp"
#include "server.hpp"
#include "text.hpp"
#include "userlist.hpp"
#include "util.hpp"

// TODO move out
#include "plugin-timer.hpp"

#ifdef USE_DBUS
#include "dbus/dbus-client.h"
#include "dbus/dbus-plugin.h"
#endif /* USE_DBUS */



// TODO: move out
static void
exec_notify_kill(session * sess)
{
#ifndef WIN32
	struct nbexec *re;
	if (sess->running_exec != nullptr)
	{
		re = sess->running_exec;
		sess->running_exec = nullptr;
		kill(re->childpid, SIGKILL);
		waitpid(re->childpid, nullptr, WNOHANG);
		fe_input_remove(re->iotag);
		close(re->myfd);
		delete re;
	}
#else
	UNREFERENCED_PARAMETER(sess);
#endif
}

session::session(struct server *serv, const char *from, ::session::session_type type)
	:server(serv),
	session_name(),
	channelkey(),
	limit(),
	logfd(-1),
	scrollfd(-1),
	scrollwritten(),
	type(type),
	alert_beep(SET_DEFAULT),
	alert_taskbar(SET_DEFAULT),
	alert_tray(SET_DEFAULT),

	text_hidejoinpart(SET_DEFAULT),
	text_logging(SET_DEFAULT),
	text_scrollback(SET_DEFAULT),
	text_strip(SET_DEFAULT),

	lastact_idx(LACT_NONE),
	me(nullptr),
	channel(),
	waitchannel(),
	willjoinchannel(),

	lastlog_sess(nullptr),
	running_exec(nullptr),
	gui(nullptr),
	res(nullptr),

	scrollback_replay_marklast(nullptr),

	ops(),
	hops(),
	voices(),
	total(),
	new_data(),
	nick_said(),
	msg_said(),
	ignore_date(),
	ignore_mode(),
	ignore_names(),
	end_of_names(),
	doing_who(),
	done_away_check(),
	lastlog_flags()
{
	if (from)
	{
		safe_strcpy(this->channel, from);
		this->name = from;
	}
}

session::~session()
{
	if (this->type == session::SESS_CHANNEL)
		userlist_free(*this);

	exec_notify_kill(this);
}


static int
away_check(void)
{
	session *sess;
	GSList *list;
	int sent, loop = 0;
	bool full;
	if (!prefs.hex_away_track)
		return 1;

doover:
	/* request an update of AWAY status of 1 channel every 30 seconds */
	full = true;
	sent = 0;	/* number of WHOs (users) requested */
	list = sess_list;
	while (list)
	{
		sess = static_cast<session*>(list->data);

		if (sess->server->connected &&
			sess->type == session::SESS_CHANNEL &&
			sess->channel[0] &&
			(sess->total <= prefs.hex_away_size_max || !prefs.hex_away_size_max))
		{
			if (!sess->done_away_check)
			{
				full = false;

				/* if we're under 31 WHOs, send another channels worth */
				if (sent < 31 && !sess->doing_who)
				{
					sess->done_away_check = true;
					sess->doing_who = true;
					/* this'll send a WHO #channel */
					sess->server->p_away_status(sess->channel);
					sent += sess->total;
				}
			}
		}

		list = list->next;
	}

	/* done them all, reset done_away_check to FALSE and start over unless we have away-notify */
	if (full)
	{
		list = sess_list;
		while (list)
		{
			sess = static_cast<session*>(list->data);
			if (!sess->server->have_awaynotify)
				sess->done_away_check = false;
			list = list->next;
		}
		loop++;
		if (loop < 2)
			goto doover;
	}

	return 1;
}

static void
lagcheck_update(void)
{
	if (!prefs.hex_gui_lagometer)
		return;

	for (auto & serv : glib_helper::glist_iterable<server>(serv_list))
	{
		if (serv.lag_sent)
			fe_set_lag(serv, -1);
	}
}

static int
hexchat_misc_checks(void)		/* this gets called every 1/2 second */
{
	namespace dcc = hexchat::dcc;
	static int count = 0;

	count++;

	lagcheck_update();			/* every 500ms */

	if (count % 2)
		dcc::dcc_check_timeouts();	/* every 1 second */

	if (count >= 60)				/* every 30 seconds */
	{
		if (prefs.hex_gui_lagometer)
			lag_check();
		count = 0;
	}

	return 1;
}

/* executed when the first irc window opens */

static void
irc_init(session *sess)
{
	static bool done_init = false;

	if (done_init)
		return;

	done_init = true;

	plugin_add(sess, nullptr, nullptr, timer_plugin_init, timer_plugin_deinit, nullptr, false);

#ifdef USE_PLUGIN
	if (!arg_skip_plugins)
		plugin_auto_load(sess);	/* autoload ~/.xchat *.so */
#endif

#ifdef USE_DBUS
	plugin_add(sess, nullptr, nullptr, dbus_plugin_init, nullptr, nullptr, false);
#endif

	if (prefs.hex_notify_timeout)
		notify_tag = fe_timeout_add(prefs.hex_notify_timeout * 1000,
		(GSourceFunc)notify_checklist, 0);

	fe_timeout_add(prefs.hex_away_timeout * 1000, (GSourceFunc)away_check, 0);
	fe_timeout_add(500, (GSourceFunc)hexchat_misc_checks, 0);

	if (arg_url != nullptr)
	{
		glib_string arg_url_ptr{ arg_url }; /* from GOption */
		glib_string buf{ g_strdup_printf("server %s", arg_url) };
		arg_url = nullptr;
		handle_command(sess, buf.get(), false);
	}

	if (arg_urls != nullptr)
	{
		std::unique_ptr<gchar*, decltype(&g_strfreev)> arg_urls_ptr(arg_urls, &g_strfreev);
		for (guint i = 0; i < g_strv_length(arg_urls); i++)
		{
			glib_string buf{ g_strdup_printf("%s %s", i == 0 ? "server" : "newserver", arg_urls[i]) };
			handle_command(sess, buf.get(), false);
		}
	}

	if (arg_command != nullptr)
	{
		glib_string arg_command_ptr{ arg_command };
		handle_command(sess, arg_command, false);
		arg_command = nullptr;
	}

	/* load -e <xdir>/startup.txt */
	load_perform_file(sess, "startup.txt");
}


static session *
session_new(server *serv, const char *from, int type, int focus)
{
	session *sess = new session(serv, from, type);

	sess_list = g_slist_prepend(sess_list, sess);

	fe_new_window(sess, focus);

	return sess;
}

session *
new_ircwindow(server *serv, const char *name, ::session::session_type type, int focus)
{
	session *sess;

	switch (type)
	{
	case session::SESS_SERVER:
		serv = server_new();
		if (prefs.hex_gui_tab_server)
			sess = session_new(serv, name, session::SESS_SERVER, focus);
		else
			sess = session_new(serv, name, session::SESS_CHANNEL, focus);
		serv->server_session = sess;
		serv->front_session = sess;
		break;
	case session::SESS_DIALOG:
		sess = session_new(serv, name, type, focus);
		log_open_or_close(sess);
		break;
	default:
		/*	case SESS_CHANNEL:
		case SESS_NOTICES:
		case SESS_SNOTICES:*/
		sess = session_new(serv, name, type, focus);
		break;
	}

	irc_init(sess);
	chanopt_load(sess);
	scrollback_load(*sess);
	if (sess->scrollwritten && sess->scrollback_replay_marklast)
		sess->scrollback_replay_marklast(sess);
	plugin_emit_dummy_print(sess, "Open Context");

	return sess;
}