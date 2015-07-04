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

#include <atomic>
#include <random>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <chrono>
#include <vector>
#include <sys/types.h>
#include <sys/stat.h>
#include <boost/utility/string_ref.hpp>

#define WANTSOCKET
#include "inet.hpp"

#ifdef WIN32
#include <boost/locale.hpp>
#include <boost/filesystem/path.hpp>
#include <windows.h>
#else
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#endif
#include "hexchat.hpp"
#include "fe.hpp"
#include "util.hpp"
#include "cfgfiles.hpp"
#include "chanopt.hpp"
#include "ignore.hpp"
#include "plugin.hpp"
#include "plugin-timer.hpp"
#include "notify.hpp"
#include "server.hpp"
#include "session.hpp"
#include "session_logging.hpp"
#include "servlist.hpp"
#include "outbound.hpp"
#include "text.hpp"
#include "url.hpp"
#include "hexchatc.hpp"
#include "dcc.hpp"
#include "userlist.hpp"
#include "glist_iterators.hpp"

#if ! GLIB_CHECK_VERSION (2, 36, 0)
#include <glib-object.h>			/* for g_type_init() */
#endif

#ifdef USE_LIBPROXY
#include <proxy.h>
#endif

namespace dcc = hexchat::dcc;

std::vector<popup> popup_list;
std::vector<popup> button_list;
std::vector<popup> dlgbutton_list;
std::vector<popup> command_list;
std::vector<popup> ctcp_list;
std::vector<popup> replace_list;
std::vector<popup> usermenu_list;
std::vector<popup> urlhandler_list;
std::vector<popup> tabmenu_list;
GSList *sess_list = nullptr;
GSList *dcc_list = nullptr;

/*
 * This array contains 5 double linked lists, one for each priority in the
 * "interesting session" queue ("channel" stands for everything but
 * SESS_DIALOG):
 *
 * [0] queries with hilight
 * [1] queries
 * [2] channels with hilight
 * [3] channels with dialogue
 * [4] channels with other data
 *
 * Each time activity happens the corresponding session is put at the
 * beginning of one of the lists.  The aim is to be able to switch to the
 * session with the most important/recent activity.
 */
GList *sess_list_by_lastact[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
using sess_itr = glib_helper::glist_iterator < session >;

static std::atomic_bool in_hexchat_exit = { false };
std::atomic_bool hexchat_is_quitting = { false };

/* command-line args */
int arg_dont_autoconnect = FALSE;
int arg_skip_plugins = FALSE;
char *arg_url = nullptr;
char **arg_urls = nullptr;
char *arg_command = nullptr;
gint arg_existing = FALSE;

#ifdef USE_DBUS
#include "dbus/dbus-client.h"
#include "dbus/dbus-plugin.h"
#endif /* USE_DBUS */

struct session *current_tab;
struct session *current_sess = nullptr;
struct hexchatprefs prefs;

#ifdef USE_LIBPROXY
pxProxyFactory *libproxy_factory;
#endif

int RAND_INT(int n)
{
	static std::random_device rd;
	static std::mt19937 twstr(rd());
	std::uniform_int_distribution<int> dist(0, n);
	return dist(twstr);
}

/*
 * Update the priority queue of the "interesting sessions"
 * (sess_list_by_lastact).
 */
void
lastact_update(session *sess)
{
	int oldidx = sess->lastact_idx;
	int newidx = LACT_NONE;
	bool dia = (sess->type == session::SESS_DIALOG);

	if (sess->nick_said)
		newidx = dia? LACT_QUERY_HI: LACT_CHAN_HI;
	else if (sess->msg_said)
		newidx = dia? LACT_QUERY: LACT_CHAN;
	else if (sess->new_data)
		newidx = dia? LACT_QUERY: LACT_CHAN_DATA;

	/* If already first at the right position, just return */
	if (oldidx == newidx &&
		 (newidx == LACT_NONE || g_list_index(sess_list_by_lastact[newidx], sess) == 0))
		return;

	/* Remove from the old position */
	if (oldidx != LACT_NONE)
		sess_list_by_lastact[oldidx] = g_list_remove(sess_list_by_lastact[oldidx], sess);

	/* Add at the new position */
	sess->lastact_idx = newidx;
	if (newidx != LACT_NONE)
		sess_list_by_lastact[newidx] = g_list_prepend(sess_list_by_lastact[newidx], sess);
	return;
}

/*
 * Extract the first session from the priority queue of sessions with recent
 * activity. Return NULL if no such session can be found.
 *
 * If filter is specified, skip a session if filter(session) returns 0. This
 * can be used for UI-specific needs, e.g. in fe-gtk we want to filter out
 * detached sessions.
 */
session *
lastact_getfirst(int (*filter) (session *sess))
{
	session *sess = nullptr;

	/* 5 is the number of priority classes LACT_ */
	for (int i = 0; i < 5 && !sess; i++)
	{
		auto curitem = sess_list_by_lastact[i];
		while (curitem && !sess)
		{
			sess = static_cast<session*>(g_list_nth_data(curitem, 0));
			if (!sess || (filter && !filter(sess)))
			{
				sess = nullptr;
				curitem = g_list_next(curitem);
			}
		}

		if (sess)
		{
			sess_list_by_lastact[i] = g_list_remove(sess_list_by_lastact[i], sess);
			sess->lastact_idx = LACT_NONE;
		}
	}
	
	return sess;
}

bool
is_session (session * sess)
{
	return g_slist_find(sess_list, sess) != nullptr;
}

session * find_dialog(const server &serv, const boost::string_ref &nick)
{
	sess_itr end;
	auto result = std::find_if(
		sess_itr(sess_list),
		end,
		[&nick, &serv](const session& sess)
		{
			return (sess.server == &serv && sess.type == session::SESS_DIALOG) && !serv.compare(nick, sess.channel);
		});
	return result != end ? &(*result) : nullptr;
}

session *find_channel(const server &serv, const boost::string_ref &chan)
{
	sess_itr end;
	auto result = std::find_if(
		sess_itr(sess_list),
		end,
		[&chan, &serv](const session& sess)
	{
		return (sess.server == &serv && sess.type == session::SESS_CHANNEL) && !serv.compare(chan, sess.channel);
	});
	return result != end ? &(*result) : nullptr;
}

void
lag_check (void)
{
	using namespace boost;
	char tbuf[128];
	auto now = chrono::steady_clock::now();

	auto tim = make_ping_time ();

	for (auto & serv : glib_helper::glist_iterable<server>(serv_list))
	{
		if (serv.connected && serv.end_of_motd)
		{
			auto seconds = chrono::duration_cast<chrono::seconds>(now - serv.ping_recv).count();
			if (prefs.hex_net_ping_timeout && seconds > prefs.hex_net_ping_timeout && seconds > 0)
			{
				snprintf(tbuf, sizeof(tbuf), "%" PRId64, seconds);
				EMIT_SIGNAL (XP_TE_PINGTIMEOUT, serv.server_session, tbuf, nullptr,
								 nullptr, nullptr, 0);
				if (prefs.hex_net_auto_reconnect)
					serv.auto_reconnect (false, -1);
			} else
			{
				snprintf (tbuf, sizeof (tbuf), "LAG%lu", tim);
				serv.p_ping({}, tbuf);
				
				if (!serv.lag_sent)
				{
					serv.lag_sent = tim;
					fe_set_lag (serv, -1);
				}
			}
		}
	}
}

nbexec::nbexec(session *sess)
	:myfd(),
	childpid(),
	tochannel(),						/* making this int keeps the struct 4-byte aligned */
	iotag(),
	sess(sess){}

static void
send_quit_or_part (session * killsess)
{
	bool willquit = true;
	server *killserv = killsess->server;

	/* check if this is the last session using this server */
	for(const auto & sess : glib_helper::glist_iterable<session>(sess_list))
	{
		if (sess.server == killserv && (&sess) != killsess)
		{
			willquit = false;
			break;
		}
	}

	if (hexchat_is_quitting)
		willquit = true;

	if (killserv->connected)
	{
		if (willquit)
		{
			if (!killserv->sent_quit)
			{
				killserv->flush_queue ();
				server_sendquit (killsess);
				killserv->sent_quit = true;
			}
		} else
		{
			if (killsess->type == session::SESS_CHANNEL && killsess->channel[0] &&
				 !killserv->sent_quit)
			{
				server_sendpart (*killserv, killsess->channel, boost::none);
			}
		}
	}
}

void
session_free (session *killsess)
{
	server *killserv = killsess->server;
	int oldidx;

	plugin_emit_dummy_print (killsess, "Close Context");

	if (current_tab == killsess)
		current_tab = nullptr;

	if (killserv->server_session == killsess)
		killserv->server_session = nullptr;

	if (killserv->front_session == killsess)
	{
		/* front_session is closed, find a valid replacement */
		killserv->front_session = nullptr;
		for(auto & sess : glib_helper::glist_iterable<session>(sess_list))
		{
			if (&sess != killsess && sess.server == killserv)
			{
				killserv->front_session = &sess;
				if (!killserv->server_session)
					killserv->server_session = &sess;
				break;
			}
		}
	}

	if (!killserv->server_session)
		killserv->server_session = killserv->front_session;

	sess_list = g_slist_remove (sess_list, killsess);

	oldidx = killsess->lastact_idx;
	if (oldidx != LACT_NONE)
		sess_list_by_lastact[oldidx] = g_list_remove(sess_list_by_lastact[oldidx], killsess);

	//log_close (*killsess);
	scrollback_close (*killsess);
	chanopt_save (killsess);

	send_quit_or_part (killsess);

	fe_session_callback (killsess);

	if (current_sess == killsess)
	{
		current_sess = nullptr;
		if (sess_list)
			current_sess = static_cast<session*>(sess_list->data);
	}

	delete killsess;

	if (!sess_list && !in_hexchat_exit)
		hexchat_exit ();						/* sess_list is empty, quit! */

	for (const auto & sess : glib_helper::glist_iterable<session>(sess_list))
	{
		if (sess.server == killserv)
			return;					  /* this server is still being used! */
	}

	server_free (killserv);
}

static void
free_sessions (void)
{
	GSList *list = sess_list;

	while (list)
	{
		fe_close_window (static_cast<session*>(list->data));
		list = sess_list;
	}
}


static const char defaultconf_ctcp[] =
	"NAME TIME\n"				"CMD nctcp %s TIME %t\n\n"\
	"NAME PING\n"				"CMD nctcp %s PING %d\n\n";

static const char defaultconf_replace[] =
	"NAME teh\n"				"CMD the\n\n";
/*	"NAME r\n"					"CMD are\n\n"\
	"NAME u\n"					"CMD you\n\n"*/

static const char defaultconf_commands[] =
	"NAME ACTION\n"		"CMD me &2\n\n"\
	"NAME AME\n"			"CMD allchan me &2\n\n"\
	"NAME ANICK\n"			"CMD allserv nick &2\n\n"\
	"NAME AMSG\n"			"CMD allchan say &2\n\n"\
	"NAME BANLIST\n"		"CMD quote MODE %c +b\n\n"\
	"NAME CHAT\n"			"CMD dcc chat %2\n\n"\
	"NAME DIALOG\n"		"CMD query %2\n\n"\
	"NAME DMSG\n"			"CMD msg =%2 &3\n\n"\
	"NAME EXIT\n"			"CMD quit\n\n"\
	"NAME GREP\n"			"CMD lastlog -r -- &2\n\n"\
	"NAME IGNALL\n"			"CMD ignore %2!*@* ALL\n\n"\
	"NAME J\n"				"CMD join &2\n\n"\
	"NAME KILL\n"			"CMD quote KILL %2 :&3\n\n"\
	"NAME LEAVE\n"			"CMD part &2\n\n"\
	"NAME M\n"				"CMD msg &2\n\n"\
	"NAME OMSG\n"			"CMD msg @%c &2\n\n"\
	"NAME ONOTICE\n"		"CMD notice @%c &2\n\n"\
	"NAME RAW\n"			"CMD quote &2\n\n"\
	"NAME SERVHELP\n"		"CMD quote HELP\n\n"\
	"NAME SPING\n"			"CMD ping\n\n"\
	"NAME SQUERY\n"		"CMD quote SQUERY %2 :&3\n\n"\
	"NAME SSLSERVER\n"	"CMD server -ssl &2\n\n"\
	"NAME SV\n"				"CMD echo HexChat %v %m\n\n"\
	"NAME UMODE\n"			"CMD mode %n &2\n\n"\
	"NAME UPTIME\n"		"CMD quote STATS u\n\n"\
	"NAME VER\n"			"CMD ctcp %2 VERSION\n\n"\
	"NAME VERSION\n"		"CMD ctcp %2 VERSION\n\n"\
	"NAME WALLOPS\n"		"CMD quote WALLOPS :&2\n\n"\
		"NAME WI\n"                     "CMD quote WHOIS %2\n\n"\
	"NAME WII\n"			"CMD quote WHOIS %2 %2\n\n";

static const char defaultconf_urlhandlers[] =
		"NAME Open Link in a new Firefox Window\n"		"CMD !firefox -new-window %s\n\n";

#ifdef USE_SIGACTION
/* Close and open log files on SIGUSR1. Usefull for log rotating */

static void 
sigusr1_handler (int /*signal*/, siginfo_t * /*si*/, void *)
{
	//GSList *list = sess_list;
	//session *sess;

	//while (list)
	//{
	//	sess = static_cast<session*>(list->data);
	//	//log_open_or_close (sess);
	//	list = list->next;
	//}
}

/* Execute /SIGUSR2 when SIGUSR2 received */

static void
sigusr2_handler (int /*signal*/, siginfo_t *, void *)
{
	session *sess = current_sess;
	if (sess)
	{
		char cmd[] = "SIGUSR2";
		handle_command (sess, cmd, false);
	}
}
#endif

static gint
xchat_auto_connect (gpointer)
{
	servlist_auto_connect (nullptr);
	return 0;
}

#ifdef WIN32
namespace
{
	class winsock_raii
	{
		WSADATA wsadata;
		bool success;

		winsock_raii(const winsock_raii&) = delete;
		winsock_raii& operator=(const winsock_raii&) = delete;
	public:
		explicit winsock_raii() NOEXCEPT
			:wsadata({ 0 }), success(WSAStartup(MAKEWORD(2, 2), &wsadata) == 0)
		{
		}
		~winsock_raii() NOEXCEPT
		{
			if (success)
				WSACleanup();
		}

		explicit operator bool() const NOEXCEPT
		{
			return success;
		}
	};
}
#endif

static void
xchat_init (void)
{
	char buf[3068];
	const char *cs = nullptr;

#ifdef USE_SIGACTION
	struct sigaction act;

	/* ignore SIGPIPE's */
	act.sa_handler = SIG_IGN;
	act.sa_flags = 0;
	sigemptyset (&act.sa_mask);
	sigaction (SIGPIPE, &act, nullptr);

	/* Deal with SIGUSR1's & SIGUSR2's */
	act.sa_sigaction = sigusr1_handler;
	act.sa_flags = 0;
	sigemptyset (&act.sa_mask);
	sigaction (SIGUSR1, &act, nullptr);

	act.sa_sigaction = sigusr2_handler;
	act.sa_flags = 0;
	sigemptyset (&act.sa_mask);
	sigaction (SIGUSR2, &act, nullptr);
#else
#ifndef WIN32
	/* good enough for these old systems */
	signal (SIGPIPE, SIG_IGN);
#endif
#endif

	if (g_get_charset (&cs))
		prefs.utf8_locale = TRUE;

	load_text_events ();
	sound_load ();
	notify_load ();
	ignore_load ();

	snprintf (buf, sizeof (buf),
		"NAME %s~%s~\n"				"CMD query %%s\n\n"\
		"NAME %s~%s~\n"				"CMD send %%s\n\n"\
		"NAME %s~%s~\n"				"CMD whois %%s %%s\n\n"\
		"NAME %s~%s~\n"				"CMD notify -n ASK %%s\n\n"\
		"NAME %s~%s~\n"				"CMD ignore %%s!*@* ALL\n\n"\

		"NAME SUB\n"					"CMD %s\n\n"\
			"NAME %s\n"					"CMD op %%a\n\n"\
			"NAME %s\n"					"CMD deop %%a\n\n"\
			"NAME SEP\n"				"CMD \n\n"\
			"NAME %s\n"					"CMD voice %%a\n\n"\
			"NAME %s\n"					"CMD devoice %%a\n"\
			"NAME SEP\n"				"CMD \n\n"\
			"NAME SUB\n"				"CMD %s\n\n"\
				"NAME %s\n"				"CMD kick %%s\n\n"\
				"NAME %s\n"				"CMD ban %%s\n\n"\
				"NAME SEP\n"			"CMD \n\n"\
				"NAME %s *!*@*.host\n""CMD ban %%s 0\n\n"\
				"NAME %s *!*@domain\n""CMD ban %%s 1\n\n"\
				"NAME %s *!*user@*.host\n""CMD ban %%s 2\n\n"\
				"NAME %s *!*user@domain\n""CMD ban %%s 3\n\n"\
				"NAME SEP\n"			"CMD \n\n"\
				"NAME %s *!*@*.host\n""CMD kickban %%s 0\n\n"\
				"NAME %s *!*@domain\n""CMD kickban %%s 1\n\n"\
				"NAME %s *!*user@*.host\n""CMD kickban %%s 2\n\n"\
				"NAME %s *!*user@domain\n""CMD kickban %%s 3\n\n"\
			"NAME ENDSUB\n"			"CMD \n\n"\
		"NAME ENDSUB\n"				"CMD \n\n",

		_("_Open Dialog Window"), "gtk-go-up",
		_("_Send a File"), "gtk-floppy",
		_("_User Info (WhoIs)"), "gtk-info",
		_("_Add to Friends List"), "gtk-add",
		_("_Ignore"), "gtk-stop",
		_("O_perator Actions"),

		_("Give Ops"),
		_("Take Ops"),
		_("Give Voice"),
		_("Take Voice"),

		_("Kick/Ban"),
		_("Kick"),
		_("Ban"),
		_("Ban"),
		_("Ban"),
		_("Ban"),
		_("Ban"),
		_("KickBan"),
		_("KickBan"),
		_("KickBan"),
		_("KickBan"));

	list_loadconf ("popup.conf", popup_list, buf);

	snprintf (buf, sizeof (buf),
		"NAME %s\n"				"CMD part\n\n"
		"NAME %s\n"				"CMD getstr # join \"%s\"\n\n"
		"NAME %s\n"				"CMD quote LINKS\n\n"
		"NAME %s\n"				"CMD ping\n\n"
		"NAME TOGGLE %s\n"	"CMD irc_hide_version\n\n",
				_("Leave Channel"),
				_("Join Channel..."),
				_("Enter Channel to Join:"),
				_("Server Links"),
				_("Ping Server"),
				_("Hide Version"));
	list_loadconf ("usermenu.conf", usermenu_list, buf);

	snprintf (buf, sizeof (buf),
		"NAME %s\n"		"CMD op %%a\n\n"
		"NAME %s\n"		"CMD deop %%a\n\n"
		"NAME %s\n"		"CMD ban %%s\n\n"
		"NAME %s\n"		"CMD getstr \"%s\" \"kick %%s\" \"%s\"\n\n"
		"NAME %s\n"		"CMD send %%s\n\n"
		"NAME %s\n"		"CMD query %%s\n\n",
				_("Op"),
				_("DeOp"),
				_("Ban"),
				_("Kick"),
				_("bye"),
				_("Enter reason to kick %s:"),
				_("Sendfile"),
				_("Dialog"));
	list_loadconf ("buttons.conf", button_list, buf);

	snprintf (buf, sizeof (buf),
		"NAME %s\n"				"CMD whois %%s %%s\n\n"
		"NAME %s\n"				"CMD send %%s\n\n"
		"NAME %s\n"				"CMD dcc chat %%s\n\n"
		"NAME %s\n"				"CMD clear\n\n"
		"NAME %s\n"				"CMD ping %%s\n\n",
				_("WhoIs"),
				_("Send"),
				_("Chat"),
				_("Clear"),
				_("Ping"));
	list_loadconf ("dlgbuttons.conf", dlgbutton_list, buf);

	list_loadconf ("tabmenu.conf", tabmenu_list, nullptr);
	list_loadconf ("ctcpreply.conf", ctcp_list, defaultconf_ctcp);
	list_loadconf ("commands.conf", command_list, defaultconf_commands);
	list_loadconf ("replace.conf", replace_list, defaultconf_replace);
	list_loadconf ("urlhandlers.conf", urlhandler_list,
						defaultconf_urlhandlers);

	servlist_init ();							/* load server list */

	/* if we got a URL, don't open the server list GUI */
	if (!prefs.hex_gui_slist_skip && !arg_url && !arg_urls)
		fe_serverlist_open (nullptr);

	/* turned OFF via -a arg or by passing urls */
	if (!arg_dont_autoconnect && !arg_urls)
	{
		/* do any auto connects */
		if (!servlist_have_auto ())	/* if no new windows open .. */
		{
			/* and no serverlist gui ... */
			if (prefs.hex_gui_slist_skip || arg_url || arg_urls)
				/* we'll have to open one. */
				new_ircwindow(nullptr, nullptr, session::SESS_SERVER, 0);
		} else
		{
			fe_idle_add (xchat_auto_connect, nullptr);
		}
	} else
	{
		if (prefs.hex_gui_slist_skip || arg_url || arg_urls)
			new_ircwindow(nullptr, nullptr, session::SESS_SERVER, 0);
	}
}

void
hexchat_exit (void)
{
	hexchat_is_quitting = true;
	in_hexchat_exit = true;
	plugin_kill_all ();
	fe_cleanup ();

	save_config ();
	if (prefs.save_pevents)
	{
		pevent_save (nullptr);
	}

	sound_save ();
	notify_save ();
	ignore_save ();
	free_sessions ();
	chanopt_save_all ();
	servlist_cleanup ();
	fe_exit ();
}

void
hexchat_exec (const char *cmd)
{
	util_exec (cmd);
}


static void
set_locale (void)
{
#ifdef WIN32
	char hexchat_lang[13] = { 0 };	/* LC_ALL= plus 5 chars of hex_gui_lang and trailing \0 */

	if (0 <= prefs.hex_gui_lang && prefs.hex_gui_lang < LANGUAGES_LENGTH)
		std::strcat (hexchat_lang, languages[prefs.hex_gui_lang]);
	else
		std::strcat (hexchat_lang, "en");

	auto result = _putenv_s("LC_ALL", hexchat_lang);
	if (result != 0)
		std::terminate();

	// Create and install global locale
	std::locale::global(boost::locale::generator().generate(""));
	// Make boost.filesystem use it
	boost::filesystem::path::imbue(std::locale());

#endif
}

int
hexmain (int argc, char *argv[])
{
	std::srand (static_cast<unsigned>(std::time (nullptr)));	/* CL: do this only once! */

	/* We must check for the config dir parameter, otherwise load_config() will behave incorrectly.
	 * load_config() must come before fe_args() because fe_args() calls gtk_init() which needs to
	 * know the language which is set in the config. The code below is copy-pasted from fe_args()
	 * for the most part. */
	if (argc >= 2)
	{
		for (int i = 1; i < argc; i++)
		{
			if ((std::strcmp (argv[i], "-d") == 0 || std::strcmp (argv[i], "--cfgdir") == 0)
				&& i + 1 < argc)
			{
				xdir = new_strdup (argv[i + 1]);
			}
			else if (strncmp (argv[i], "--cfgdir=", 9) == 0)
			{
				xdir = new_strdup (argv[i] + 9);
			}

			if (xdir != NULL)
			{
				const auto xdir_len = std::strlen(xdir);
				if (xdir[xdir_len - 1] == G_DIR_SEPARATOR)
				{
					xdir[xdir_len - 1] = 0;
				}
				break;
			}
		}
	}

#if ! GLIB_CHECK_VERSION (2, 36, 0)
	g_type_init ();
#endif

	if (check_config_dir () == 0)
	{
		if (load_config () != 0)
			load_default_config ();
	} else
	{
		/* this is probably the first run */
		load_default_config ();
		make_config_dirs ();
		make_dcc_dirs ();
	}

	/* we MUST do this after load_config () AND before fe_init (thus gtk_init) otherwise it will fail */
	set_locale ();

#ifdef SOCKS
	SOCKSinit (argv[0]);
#endif

	auto ret = fe_args (argc, argv);
	if (ret != -1)
		return ret;
	
#ifdef USE_DBUS
	hexchat_remote ();
#endif

#ifdef USE_LIBPROXY
	libproxy_factory = px_proxy_factory_new();
#endif

	fe_init ();

	/* This is done here because cfgfiles.c is too early in
	* the startup process to use gtk functions. */
	if (g_access (get_xdir (), W_OK) != 0)
	{
		char buf[2048];

		g_snprintf (buf, sizeof(buf),
			_("You do not have write access to %s. Nothing from this session can be saved."),
			get_xdir ());
		fe_message (buf, FE_MSG_ERROR);
	}

#ifndef WIN32
#ifndef __EMX__
	/* OS/2 uses UID 0 all the time */
	if (getuid () == 0)
		fe_message (_("* Running IRC as root is stupid! You should\n"
				  "  create a User Account and use that to login.\n"), FE_MSG_WARN|FE_MSG_WAIT);
#endif
#endif /* !WIN32 */
#ifdef WIN32
	winsock_raii winsock;

	if (!winsock)
	{
		MessageBoxW(nullptr, L"Cannot find winsock 2.2+", L"Error", MB_OK);
		std::exit (0);
	}
#endif

	xchat_init ();

	fe_main ();

#ifdef USE_LIBPROXY
	px_proxy_factory_free(libproxy_factory);
#endif

	return 0;
}
popup::popup(std::string cmd, std::string name)
	:cmd(std::move(cmd)), name(std::move(name))
{
}
popup::popup(popup && other)
{
	this->operator=(std::forward<popup&&>(other));
}
popup& popup::operator=(popup&& other)
{
	if (this != &other)
	{
		std::swap(this->name, other.name);
		std::swap(this->cmd, other.cmd);
	}
	return *this;
}
