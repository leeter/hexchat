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

#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <boost/filesystem.hpp>
#include <boost/utility/string_ref.hpp>
#include "fe-gtk.hpp"


#ifdef WIN32
#include <Windows.h>
#include <mmsystem.h>
#include <shellapi.h>
#include <gdk/gdkwin32.h>
#else
#include <unistd.h>
#endif

#include "../common/hexchat.hpp"
#include "../common/fe.hpp"
#include "../common/util.hpp"
#include "../common/text.hpp"
#include "../common/cfgfiles.hpp"
#include "../common/hexchatc.hpp"
#include "../common/plugin.hpp"
#include "../common/server.hpp"
#include "../common/url.hpp"
#include "../common/dcc.hpp"
#include "../common/charset_helpers.hpp"
#include "../common/session.hpp"
#include "../common/glist_iterators.hpp"
#include "gtkutil.hpp"
#include "maingui.hpp"
#include "pixmaps.hpp"
#include "chanlist.hpp"
#include "joind.hpp"
#include "xtext.hpp"
#include "palette.hpp"
#include "menu.hpp"
#include "notifygui.hpp"
#include "textgui.hpp"
#include "fkeys.hpp"
#include "plugin-tray.hpp"
#include "urlgrab.hpp"
#include "setup.hpp"

#ifdef USE_LIBCANBERRA
#include <canberra.h>
#endif
namespace fe = hexchat::fe;
namespace dcc = hexchat::dcc;
GdkPixmap *channelwin_pix;

#ifdef USE_LIBCANBERRA
static ca_context *ca_con;
#endif

#ifdef HAVE_GTK_MAC
GtkosxApplication *osx_app;
#endif

/* === command-line parameter parsing : requires glib 2.6 === */

static char *arg_cfgdir = nullptr;
static gint arg_show_autoload = 0;
static gint arg_show_config = 0;
static gint arg_show_version = 0;
static gint arg_minimize = 0;

static const GOptionEntry gopt_entries[] = 
{
 {"no-auto",	'a', 0, G_OPTION_ARG_NONE,	&arg_dont_autoconnect, N_("Don't auto connect to servers"), nullptr},
 {"cfgdir",	'd', 0, G_OPTION_ARG_STRING,	&arg_cfgdir, N_("Use a different config directory"), "PATH"},
 {"no-plugins",	'n', 0, G_OPTION_ARG_NONE,	&arg_skip_plugins, N_("Don't auto load any plugins"), nullptr},
 {"plugindir",	'p', 0, G_OPTION_ARG_NONE,	&arg_show_autoload, N_("Show plugin/script auto-load directory"), nullptr},
 {"configdir",	'u', 0, G_OPTION_ARG_NONE,	&arg_show_config, N_("Show user config directory"), nullptr},
 {"url",	 0,  G_OPTION_FLAG_HIDDEN, G_OPTION_ARG_STRING, &arg_url, N_("Open an irc://server:port/channel?key URL"), "URL"},
 {"command",	'c', 0, G_OPTION_ARG_STRING,	&arg_command, N_("Execute command:"), "COMMAND"},
#ifdef USE_DBUS
 {"existing",	'e', 0, G_OPTION_ARG_NONE,	&arg_existing, N_("Open URL or execute command in an existing HexChat"), nullptr},
#endif
 {"minimize",	 0,  0, G_OPTION_ARG_INT,	&arg_minimize, N_("Begin minimized. Level 0=Normal 1=Iconified 2=Tray"), N_("level")},
 {"version",	'v', 0, G_OPTION_ARG_NONE,	&arg_show_version, N_("Show version information"), nullptr},
 {G_OPTION_REMAINING, '\0', 0, G_OPTION_ARG_STRING_ARRAY, &arg_urls, N_("Open an irc://server:port/channel?key URL"), "URL"},
 {nullptr}
};

#ifdef WIN32
static void
create_msg_dialog (gchar *title, gchar *message)
{
	std::unique_ptr<GtkWidget, decltype(&gtk_widget_destroy)> dialog(
		gtk_message_dialog_new(nullptr, GTK_DIALOG_MODAL, GTK_MESSAGE_INFO, GTK_BUTTONS_CLOSE, "%s", message), &gtk_widget_destroy);
	gtk_window_set_title (GTK_WINDOW (dialog.get()), title);

/* On Win32 we automatically have the icon. If we try to load it explicitly, it will look ugly for some reason. */
#ifndef WIN32
	pixmaps_init ();
	gtk_window_set_icon (GTK_WINDOW (dialog.get()), pix_hexchat);
#endif

	gtk_dialog_run (GTK_DIALOG (dialog.get()));
}
#endif

int
fe_args (int argc, char *argv[])
{
	GError *error = nullptr;
	GOptionContext *context;
	char *buffer;

#ifdef ENABLE_NLS
	bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
	textdomain (GETTEXT_PACKAGE);
#endif

	context = g_option_context_new (nullptr);
#ifdef WIN32
	g_option_context_set_help_enabled (context, false);	/* disable stdout help as stdout is unavailable for subsystem:windows */
#endif
	g_option_context_add_main_entries (context, gopt_entries, GETTEXT_PACKAGE);
	g_option_context_add_group (context, gtk_get_option_group (false));
	g_option_context_parse (context, &argc, &argv, &error);

#ifdef WIN32
	if (error)											/* workaround for argv not being available when using subsystem:windows */
	{
		if (error->message)								/* the error message contains argv so search for patterns in that */
		{
			if (strstr (error->message, "--help-all") != nullptr)
			{
				buffer = g_strdup_printf (g_option_context_get_help (context, false, nullptr));
				gtk_init (&argc, &argv);
				create_msg_dialog ("Long Help", buffer);
				g_free (buffer);
				return 0;
			}
			else if (strstr (error->message, "--help") != nullptr || strstr (error->message, "-?") != nullptr)
			{
				buffer = g_strdup_printf (g_option_context_get_help (context, true, nullptr));
				gtk_init (&argc, &argv);
				create_msg_dialog ("Help", buffer);
				g_free (buffer);
				return 0;
			}
			else 
			{
				buffer = g_strdup_printf ("%s\n", error->message);
				gtk_init (&argc, &argv);
				create_msg_dialog ("Error", buffer);
				g_free (buffer);
				return 1;
			}
		}
	}
#else
	if (error)
	{
		if (error->message)
			printf ("%s\n", error->message);
		return 1;
	}
#endif

	g_option_context_free (context);

	if (arg_show_version)
	{
		buffer = g_strdup_printf ("%s %s", PACKAGE_NAME, PACKAGE_VERSION);
#ifdef WIN32
		gtk_init (&argc, &argv);
		create_msg_dialog ("Version Information", buffer);
#else
		puts (buffer);
#endif
		g_free (buffer);

		return 0;
	}

	if (arg_show_autoload)
	{
		buffer = g_strdup_printf ("%s%caddons%c", get_xdir(), G_DIR_SEPARATOR, G_DIR_SEPARATOR);
#ifdef WIN32
		gtk_init (&argc, &argv);
		create_msg_dialog ("Plugin/Script Auto-load Directory", buffer);
#else
		puts (buffer);
#endif
		g_free (buffer);

		return 0;
	}

	if (arg_show_config)
	{
		buffer = g_strdup_printf ("%s%c", get_xdir(), G_DIR_SEPARATOR);
#ifdef WIN32
		gtk_init (&argc, &argv);
		create_msg_dialog ("User Config Directory", buffer);
#else
		puts (buffer);
#endif
		g_free (buffer);

		return 0;
	}

#ifdef WIN32
	/* this is mainly for irc:// URL handling. When windows calls us from */
	/* I.E, it doesn't give an option of "Start in" directory, like short */
	/* cuts can. So we have to set the current dir manually, to the path  */
	/* of the exe. */
	{
		wchar_t path[MAX_PATH] = { 0 };
		DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);
		if (len)
		{
			 auto dir = boost::filesystem::canonical(boost::filesystem::path(path).parent_path());
			_wchdir (dir.wstring().c_str());
		}
	}
#endif

	gtk_init (&argc, &argv);

#ifdef HAVE_GTK_MAC
	osx_app = g_object_new(GTKOSX_TYPE_APPLICATION, nullptr);
#endif

	return -1;
}

extern const char cursor_color_rc[] =
	"style \"xc-ib-st\""
	"{"
		"GtkEntry::cursor-color=\"#%02x%02x%02x\""
	"}"
	"widget \"*.hexchat-inputbox\" style : application \"xc-ib-st\"";

GtkStyle *
create_input_style (GtkStyle *style)
{
	char buf[256];
	static bool done_rc = false;

	pango_font_description_free (style->font_desc);
	style->font_desc = pango_font_description_from_string (prefs.hex_text_font);

	/* fall back */
	if (pango_font_description_get_size (style->font_desc) == 0)
	{
		snprintf (buf, sizeof (buf), _("Failed to open font:\n\n%s"), prefs.hex_text_font);
		fe_message (buf, FE_MSG_ERROR);
		pango_font_description_free (style->font_desc);
		style->font_desc = pango_font_description_from_string ("sans 11");
	}

	if (prefs.hex_gui_input_style && !done_rc)
	{
		done_rc = true;
		sprintf (buf, cursor_color_rc, (colors[COL_FG].red >> 8),
			(colors[COL_FG].green >> 8), (colors[COL_FG].blue >> 8));
		gtk_rc_parse_string (buf);
	}

	style->bg[GTK_STATE_NORMAL] = colors[COL_FG];
	style->base[GTK_STATE_NORMAL] = colors[COL_BG];
	style->text[GTK_STATE_NORMAL] = colors[COL_FG];

	return style;
}

void
fe_init (void)
{
	palette_load ();
	key_init ();
	pixmaps_init ();

#ifdef HAVE_GTK_MAC
	gtkosx_application_set_dock_icon_pixbuf (osx_app, pix_hexchat);
#endif
	channelwin_pix = pixmap_load_from_file (prefs.hex_text_background);
	input_style = create_input_style (gtk_style_new ());
}

#ifdef HAVE_GTK_MAC
static void
gtkosx_application_terminate (GtkosxApplication *app, gpointer userdata)
{
	hexchat_exit();
}
#endif

void
fe_main (void)
{
#ifdef HAVE_GTK_MAC
	gtkosx_application_ready(osx_app);
	g_signal_connect (G_OBJECT(osx_app), "NSApplicationWillTerminate",
					G_CALLBACK(gtkosx_application_terminate), nullptr);
#endif

	gtk_main ();

	/* sleep for 2 seconds so any QUIT messages are not lost. The  */
	/* GUI is closed at this point, so the user doesn't even know! */
	if (prefs.wait_on_exit)
		std::this_thread::sleep_for(std::chrono::seconds(2));
}

void
fe_cleanup (void)
{
	/* it's saved when pressing OK in setup.c */
	/*palette_save ();*/
}

void
fe_exit (void)
{
	gtk_main_quit ();
}

int
fe_timeout_add(int interval, GSourceFunc callback, void *userdata)
{
	return g_timeout_add (interval, callback, userdata);
}

void
fe_timeout_remove (int tag)
{
	g_source_remove (tag);
}

#ifdef WIN32

static void
log_handler (const gchar   *log_domain,
			   GLogLevelFlags log_level,
			   const gchar   *message,
			   gpointer)
{
	session *sess;

	/* if (getenv ("HEXCHAT_WARNING_IGNORE")) this gets ignored sometimes, so simply just disable all warnings */
		return;

	sess = find_dialog (*static_cast<server*>(serv_list->data), "(warnings)");
	if (!sess)
		sess = new_ircwindow(static_cast<server*>(serv_list->data), "(warnings)", session::SESS_DIALOG, 0);

	PrintTextf (sess, "%s\t%s\n", log_domain, message);
	if (getenv ("HEXCHAT_WARNING_ABORT"))
		abort ();
}

#endif

/* install tray stuff */

static int
fe_idle (gpointer)
{
	session *sess = static_cast<session*>(sess_list->data);

	plugin_add (sess, nullptr, nullptr, tray_plugin_init, tray_plugin_deinit, nullptr, false);

	if (arg_minimize == 1)
		gtk_window_iconify (GTK_WINDOW (sess->gui->window));
	else if (arg_minimize == 2)
		tray_toggle_visibility (false);

	return 0;
}

void
fe_new_window (session *sess, bool focus)
{
	bool tab = false;

	if (sess->type == session::SESS_DIALOG)
	{
		if (prefs.hex_gui_tab_dialogs)
			tab = true;
	} else
	{
		if (prefs.hex_gui_tab_chans)
			tab = true;
	}

	mg_changui_new (sess, nullptr, tab, focus);

#ifdef WIN32
	g_log_set_handler ("GLib", static_cast<GLogLevelFlags>(G_LOG_LEVEL_CRITICAL|G_LOG_LEVEL_WARNING), (GLogFunc)log_handler, 0);
	g_log_set_handler("GLib-GObject", static_cast<GLogLevelFlags>(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING), (GLogFunc)log_handler, 0);
	g_log_set_handler("Gdk", static_cast<GLogLevelFlags>(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING), (GLogFunc)log_handler, 0);
	g_log_set_handler("Gtk", static_cast<GLogLevelFlags>(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING), (GLogFunc)log_handler, 0);
#endif

	if (!sess_list->next)
		g_idle_add (fe_idle, nullptr);

	sess->scrollback_replay_marklast = gtk_xtext_set_marker_last;
}

void
fe_new_server (struct server *serv)
{
	serv->gui = new server_gui();
}

void
fe_message(const boost::string_ref & msg, int flags)
{
	GtkWidget *dialog;
	GtkMessageType type = GTK_MESSAGE_WARNING;

	if (flags & FE_MSG_ERROR)
		type = GTK_MESSAGE_ERROR;
	if (flags & FE_MSG_INFO)
		type = GTK_MESSAGE_INFO;

	dialog = gtk_message_dialog_new (GTK_WINDOW (parent_window), GtkDialogFlags(), type,
												GTK_BUTTONS_OK, "%s", msg.data());
	if (flags & FE_MSG_MARKUP)
		gtk_message_dialog_set_markup (GTK_MESSAGE_DIALOG (dialog), msg.data());
	g_signal_connect (G_OBJECT (dialog), "response",
							G_CALLBACK (gtk_widget_destroy), 0);
	gtk_window_set_resizable (GTK_WINDOW (dialog), false);
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);
	gtk_widget_show (dialog);

	if (flags & FE_MSG_WAIT)
		gtk_dialog_run (GTK_DIALOG (dialog));
}

void
fe_idle_add(GSourceFunc func, void *data)
{
	g_idle_add (func, data);
}

void
fe_input_remove (int tag)
{
	g_source_remove (tag);
}

int
fe_input_add(int sok, fia_flags flags, GIOFunc func, void *data)
{
	int tag, type = 0;
	GIOChannel *channel;

#ifdef WIN32
	if (flags & FIA_FD)
		channel = g_io_channel_win32_new_fd (sok);
	else
		channel = g_io_channel_win32_new_socket (sok);
#else
	channel = g_io_channel_unix_new (sok);
#endif

	if (flags & FIA_READ)
		type |= G_IO_IN | G_IO_HUP | G_IO_ERR;
	if (flags & FIA_WRITE)
		type |= G_IO_OUT | G_IO_ERR;
	if (flags & FIA_EX)
		type |= G_IO_PRI;

	tag = g_io_add_watch (channel, static_cast<GIOCondition>(type), (GIOFunc) func, data);
	g_io_channel_unref (channel);

	return tag;
}

void fe_set_topic (session *sess, const std::string & topic, const std::string& stripped_topic)
{
	if (!sess->gui->is_tab || sess == current_tab)
	{
		if (prefs.hex_text_stripcolor_topic)
		{
			gtk_entry_set_text (GTK_ENTRY (sess->gui->topic_entry), stripped_topic.c_str());
		}
		else
		{
			gtk_entry_set_text (GTK_ENTRY (sess->gui->topic_entry), topic.c_str());
		}
		mg_set_topic_tip (sess);
	}
	else
	{
		if (prefs.hex_text_stripcolor_topic)
		{
			sess->res->topic_text = stripped_topic;
		}
		else
		{
			sess->res->topic_text = topic;
		}
	}
}

void
fe_set_hilight (struct session *sess)
{
	if (sess->gui->is_tab)
		fe_set_tab_color (sess, fe_tab_color::nick_seen);	/* set tab to blue */

	if (prefs.hex_input_flash_hilight && (!prefs.hex_away_omit_alerts || !sess->server->is_away))
		fe_flash_window (sess); /* taskbar flash */
}

static void
fe_update_mode_entry (session *sess, GtkWidget *entry, std::string & text, const std::string& new_text)
{
	if (!sess->gui->is_tab || sess == current_tab)
	{
		if (sess->gui->flag_wid[0])	/* channel mode buttons enabled? */
			gtk_entry_set_text (GTK_ENTRY (entry), new_text.c_str());
	} else
	{
		if (sess->gui->is_tab)
		{
			text = new_text;
		}
	}
}

void
fe_update_channel_key (struct session *sess)
{
	fe_update_mode_entry (sess, sess->gui->key_entry,
								 sess->res->key_text, sess->channelkey);
	fe_set_title (*sess);
}

void
fe_update_channel_limit (struct session *sess)
{
	auto tmp = std::to_string(sess->limit);
	fe_update_mode_entry (sess, sess->gui->limit_entry,
								 sess->res->limit_text, tmp);
	fe_set_title (*sess);
}

int
fe_is_chanwindow (struct server *serv)
{
	if (!serv->gui->chanlist_window)
		return 0;
	return 1;
}

void
fe_notify_update(const std::string* name)
{
	if (!name)
		hexchat::gui::notify::notify_gui_update ();
}

void
fe_text_clear (struct session *sess, int lines)
{
	gtk_xtext_clear (static_cast<xtext_buffer*>(sess->res->buffer), lines);
}

void
fe_close_window (struct session *sess)
{
	if (sess->gui->is_tab)
		mg_tab_close (sess);
	else
		gtk_widget_destroy (sess->gui->window);
}

void
fe_progressbar_start (session *sess)
{
	if (!sess->gui->is_tab || current_tab == sess)
	/* if it's the focused tab, create it for real! */
		mg_progressbar_create (sess->gui);
	else
	/* otherwise just remember to create on when it gets focused */
		sess->res->c_graph = true;
}

void
fe_progressbar_end (server *serv)
{
	/* check all windows that use this server and  *
	 * remove the connecting graph, if it has one. */
	for(auto & sess : glib_helper::glist_iterable<session>(sess_list))
	{
		if (sess.server == serv)
		{
			if (sess.gui->bar)
				mg_progressbar_destroy (sess.gui);
			sess.res->c_graph = false;
		}
	}
}

void
fe_print_text (session &sess, char *text, time_t stamp,
			   gboolean no_activity)
{
	PrintTextRaw (sess.res->buffer, (unsigned char *)text, prefs.hex_text_indent, stamp);

	if (!no_activity && !sess.new_data && &sess != current_tab &&
		sess.gui->is_tab && !sess.nick_said)
	{
		sess.new_data = true;
		lastact_update (&sess);
		if (sess.msg_said)
			fe_set_tab_color (&sess, fe_tab_color::new_message);
		else
			fe_set_tab_color (&sess, fe_tab_color::new_data);
	}
}

void
fe_beep (session *sess)
{
#ifdef WIN32
	/* Play the "Instant Message Notification" system sound
	 */
	if (!PlaySoundW (L"Notification.IM", nullptr, SND_ALIAS | SND_ASYNC))
	{
		/* The user does not have the "Instant Message Notification" sound set. Fall back to system beep.
		 */
		Beep (1000, 50);
	}
#else
#ifdef USE_LIBCANBERRA
	if (ca_con == nullptr)
	{
		ca_context_create (&ca_con);
		ca_context_change_props (ca_con,
										CA_PROP_APPLICATION_ID, "hexchat",
										CA_PROP_APPLICATION_NAME, DISPLAY_NAME,
										CA_PROP_APPLICATION_ICON_NAME, "hexchat", nullptr);
	}

	if (ca_context_play (ca_con, 0, CA_PROP_EVENT_ID, "message-new-instant", nullptr) != 0)
#endif
	gdk_beep ();
#endif
}

void
fe_lastlog (session *sess, session *lastlog_sess, char *sstr, gtk_xtext_search_flags flags)
{
	auto buf = static_cast<xtext_buffer*>(sess->res->buffer);

	if (buf && gtk_xtext_is_empty (*buf))
	{
		PrintText (lastlog_sess, _("Search buffer is empty.\n"));
		return;
	}

	auto lbuf = static_cast<xtext_buffer*>(lastlog_sess->res->buffer);
	if (flags & regexp)
	{
		GRegexCompileFlags gcf = (flags & case_match) ? GRegexCompileFlags() : G_REGEX_CASELESS;
		GError *err = nullptr;
		lbuf->search_re = g_regex_new (sstr, gcf, GRegexMatchFlags(), &err);
		if (err)
		{
			std::unique_ptr<GError, decltype(&g_error_free)> err_ptr(err, g_error_free);
			PrintText (lastlog_sess, _(err->message));
			return;
		}
	}
	else
	{
		if (flags & case_match)
		{
			lbuf->search_nee = sstr;
		}
		else
		{
			glib_string folded{ g_utf8_casefold(sstr, strlen(sstr)) };
			lbuf->search_nee = folded.get();
		}
	}
	lbuf->search_flags = flags;
	lbuf->search_text = sstr;
	gtk_xtext_lastlog (lbuf, buf);
}

void
fe_set_lag (server &serv, long lag)
{
	char lagtext[64];
	char lagtip[128];
	unsigned long nowtim;

	if (lag == -1)
	{
		if (!serv.lag_sent)
			return;
		nowtim = make_ping_time ();
		lag = nowtim - serv.lag_sent;
	}

	/* if there is no pong for >30s report the lag as +30s */
	if (lag > 30000 && serv.lag_sent)
		lag=30000;

	auto per = static_cast<double>(lag) / 1000.0;
	if (per > 1.0)
		per = 1.0;

	snprintf (lagtext, sizeof (lagtext) - 1, "%s%ld.%lds",
			  serv.lag_sent ? "+" : "", lag / 1000, (lag/100) % 10);
	snprintf (lagtip, sizeof (lagtip) - 1, "Lag: %s%ld.%ld seconds",
				 serv.lag_sent ? "+" : "", lag / 1000, (lag/100) % 10);

	for (auto & sess : glib_helper::glist_iterable<session>(sess_list))
	{
		if (sess.server != &serv)
		{
			continue;
		}

		sess.res->lag_tip = lagtip;

		if (!sess.gui->is_tab || current_tab == &sess)
		{
			if (sess.gui->lagometer)
			{
				gtk_progress_bar_set_fraction((GtkProgressBar *)sess.gui->lagometer, per);
				gtk_widget_set_tooltip_text(gtk_widget_get_parent(sess.gui->lagometer), lagtip);
			}
			if (sess.gui->laginfo)
				gtk_label_set_text((GtkLabel *)sess.gui->laginfo, lagtext);
		}
		else
		{
			sess.res->lag_value = per;
			sess.res->lag_text = lagtext;
		}
	}
}

void
fe_set_throttle (server *serv)
{
	char tbuf[96];
	char tip[160];

	auto per = static_cast<double>(serv->sendq_len) / 1024.0;
	if (per > 1.0)
		per = 1.0;

	for (auto & sess : glib_helper::glist_iterable<session>(sess_list))
	{
		if (sess.server != serv)
		{
			continue;
		}
		snprintf(tbuf, sizeof(tbuf) - 1, _("%d bytes"), serv->sendq_len);
		snprintf(tip, sizeof(tip) - 1, _("Network send queue: %d bytes"), serv->sendq_len);

		sess.res->queue_tip = tip;

		if (!sess.gui->is_tab || current_tab == &sess)
		{
			if (sess.gui->throttlemeter)
			{
				gtk_progress_bar_set_fraction((GtkProgressBar *)sess.gui->throttlemeter, per);
				gtk_widget_set_tooltip_text(gtk_widget_get_parent(sess.gui->throttlemeter), tip);
			}
			if (sess.gui->throttleinfo)
				gtk_label_set_text((GtkLabel *)sess.gui->throttleinfo, tbuf);
		}
		else
		{
			sess.res->queue_value = per;
			sess.res->queue_text = tbuf;
		}
	}
}

void
fe_ctrl_gui (session *sess, fe_gui_action action, int arg)
{
	switch (action)
	{
	case FE_GUI_HIDE:
		gtk_widget_hide (sess->gui->window); break;
	case FE_GUI_SHOW:
		gtk_widget_show (sess->gui->window);
		gtk_window_present (GTK_WINDOW (sess->gui->window));
		break;
	case FE_GUI_FOCUS:
		mg_bring_tofront_sess (sess); break;
	case FE_GUI_FLASH:
		fe_flash_window (sess); break;
	case FE_GUI_COLOR:
		fe_set_tab_color (sess, static_cast<fe_tab_color>(arg)); break;
	case FE_GUI_ICONIFY:
		gtk_window_iconify (GTK_WINDOW (sess->gui->window)); break;
	case FE_GUI_MENU:
		menu_bar_toggle ();	/* toggle menubar on/off */
		break;
	case FE_GUI_ATTACH:
		mg_detach (sess, arg);	/* arg: 0=toggle 1=detach 2=attach */
		break;
	case FE_GUI_APPLY:
		setup_apply_real (true, true, true);
	}
}

static void
dcc_saveas_cb (dcc::DCC *dcc, char *file)
{
	if (is_dcc (dcc))
	{
		if (dcc->dccstat == STAT_QUEUED)
		{
			if (file)
				dcc_get_with_destfile (dcc, file);
			else if (dcc->resume_sent == 0)
				dcc_abort (dcc->serv->front_session, dcc);
		}
	}
}

void
fe_confirm (const char *message, void (*yesproc)(void *), void (*noproc)(void *), void *ud)
{
	/* warning, assuming fe_confirm is used by DCC only! */
	dcc::DCC *dcc = static_cast<dcc::DCC*>(ud);
	char *filepath;

	if (dcc->file)
	{
		filepath = g_build_filename (prefs.hex_dcc_dir, dcc->file, nullptr);
		gtkutil_file_req (message, (filereqcallback)dcc_saveas_cb, ud, filepath, nullptr,
								FRF_WRITE|FRF_NOASKOVERWRITE|FRF_FILTERISINITIAL);
		g_free (filepath);
	}
}

int
fe_gui_info (session *sess, int info_type)
{
	switch (info_type)
	{
	case 0:	/* window status */
		if (!gtk_widget_get_visible (GTK_WIDGET (sess->gui->window)))
		{
			return 2;	/* hidden (iconified or systray) */
		}

		if (gtk_window_is_active (GTK_WINDOW (sess->gui->window)))
		{
			return 1;	/* active/focused */
		}

		return 0;		/* normal (no keyboard focus or behind a window) */
	}

	return -1;
}

void *
fe_gui_info_ptr (session *sess, int info_type)
{
	switch (info_type)
	{
	case 0:	/* native window pointer (for plugins) */
#ifdef WIN32
		return gdk_win32_window_get_impl_hwnd (gtk_widget_get_window (sess->gui->window));
#else
		return sess->gui->window;
#endif
		break;

	case 1:	/* GtkWindow * (for plugins) */
		return sess->gui->window;
	}
	return nullptr;
}

const char * fe_get_inputbox_contents (session *sess)
{
	/* not the current tab */
	if (sess != current_sess)
		return sess->res->input_text.c_str();

	/* current focused tab */
	return SPELL_ENTRY_GET_TEXT (sess->gui->input_box);
}

int
fe_get_inputbox_cursor (session *sess)
{
	/* not the current tab (we don't remember the cursor pos) */
	if (sess != current_sess)
		return 0;

	/* current focused tab */
	return SPELL_ENTRY_GET_POS (sess->gui->input_box);
}

void
fe_set_inputbox_cursor (session *sess, int delta, int pos)
{
	if (!sess->gui->is_tab || sess == current_tab)
	{
		if (delta)
			pos += SPELL_ENTRY_GET_POS (sess->gui->input_box);
		SPELL_ENTRY_SET_POS (sess->gui->input_box, pos);
	} else
	{
		/* we don't support changing non-front tabs yet */
	}
}

void
fe_set_inputbox_contents (session *sess, char *text)
{
	if (!sess->gui->is_tab || sess == current_tab)
	{
		SPELL_ENTRY_SET_TEXT (sess->gui->input_box, text);
	} else
	{
		sess->res->input_text = text;
	}
}

#ifdef __APPLE__
static char *
url_escape_hostname (const char *url)
{
	char *host_start, *host_end, *ret, *hostname;

	host_start = strstr (url, "://");
	if (host_start != nullptr)
	{
		*host_start = '\0';
		host_start += 3;
		host_end = strchr (host_start, '/');

		if (host_end != nullptr)
		{
			*host_end = '\0';
			host_end++;
		}

		hostname = g_hostname_to_ascii (host_start);
		if (host_end != nullptr)
			ret = g_strdup_printf ("%s://%s/%s", url, hostname, host_end);
		else
			ret = g_strdup_printf ("%s://%s", url, hostname);

		g_free (hostname);
		return ret;
	}

	return g_strdup (url);
}

static void
osx_show_uri (const char *url)
{
	char *escaped_url, *encoded_url, *open, *cmd;

	escaped_url = url_escape_hostname (url);
	encoded_url = g_filename_from_utf8 (escaped_url, -1, nullptr, nullptr, nullptr);
	if (encoded_url)
	{
		open = g_find_program_in_path ("open");
		cmd = g_strjoin (" ", open, encoded_url, nullptr);

		hexchat_exec (cmd);

		g_free (encoded_url);
		g_free (cmd);
	}

	g_free (escaped_url);
}

#endif

static void
fe_open_url_inner (const char *url)
{
#ifdef WIN32
	ShellExecuteW (0, L"open", charset::widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
	osx_show_uri (url);
#else
	gtk_show_uri (nullptr, url, GDK_CURRENT_TIME, nullptr);
#endif
}

void
fe_open_url (const char *url)
{
	int url_type = url_check_word (url);
	char *uri;

	/* gvfs likes file:// */
	if (url_type == WORD_PATH)
	{
#ifndef WIN32
		uri = g_strconcat ("file://", url, nullptr);
		fe_open_url_inner (uri);
		g_free (uri);
#else
		fe_open_url_inner (url);
#endif
	}
	/* IPv6 addr. Add http:// */
	else if (url_type == WORD_HOST6)
	{
		/* IPv6 addrs in urls should be enclosed in [ ] */
		if (*url != '[')
			uri = g_strdup_printf ("http://[%s]", url);
		else
			uri = g_strdup_printf ("http://%s", url);

		fe_open_url_inner (uri);
		g_free (uri);
	}
	/* the http:// part's missing, prepend it, otherwise it won't always work */
	else if (strchr (url, ':') == nullptr)
	{
		url = g_strdup_printf ("http://%s", url);
		fe_open_url_inner (url);
		g_free ((char *)url);
	}
	/* we have a sane URL, send it to the browser untouched */
	else
	{
		fe_open_url_inner (url);
	}
}

void
fe_server_event(server *serv, fe_serverevents type, int arg)
{
	for (auto & sess : glib_helper::glist_iterable<session>(sess_list))
	{
		if (sess.server == serv && (current_tab == &sess || !sess.gui->is_tab))
		{
			session_gui *gui = sess.gui;

			switch (type)
			{
			case fe_serverevents::CONNECTING:	/* connecting in progress */
			case fe_serverevents::RECONDELAY:	/* reconnect delay begun */
				/* enable Disconnect item */
				gtk_widget_set_sensitive (gui->menu_item[MENU_ID_DISCONNECT], 1);
				break;

			case fe_serverevents::CONNECT:
				/* enable Disconnect and Away menu items */
				gtk_widget_set_sensitive (gui->menu_item[MENU_ID_AWAY], 1);
				gtk_widget_set_sensitive (gui->menu_item[MENU_ID_DISCONNECT], 1);
				break;

			case fe_serverevents::LOGGEDIN:	/* end of MOTD */
				gtk_widget_set_sensitive (gui->menu_item[MENU_ID_JOIN], 1);
				/* if number of auto-join channels is zero, open joind */
				if (arg == 0)
					joind_open (serv);
				break;

			case fe_serverevents::DISCONNECT:
				/* disable Disconnect and Away menu items */
				gtk_widget_set_sensitive (gui->menu_item[MENU_ID_AWAY], 0);
				gtk_widget_set_sensitive (gui->menu_item[MENU_ID_DISCONNECT], 0);
				gtk_widget_set_sensitive (gui->menu_item[MENU_ID_JOIN], 0);
				/* close the join-dialog, if one exists */
				joind_close (serv);
			}
		}
	}
}

void
fe_get_file (const char *title, char *initial,
				 void (*callback) (void *userdata, char *file), void *userdata,
				 fe_file_flags flags)
				
{
	/* OK: Call callback once per file, then once more with file=nullptr. */
	/* CANCEL: Call callback once with file=nullptr. */
	gtkutil_file_req (title, callback, userdata, initial, nullptr, flags | FRF_FILTERISINITIAL);
}

void
fe_open_chan_list (server *serv, const char*, bool do_refresh)
{
	chanlist_opengui (serv, do_refresh);
}

const char *
fe_get_default_font (void)
{
#ifdef WIN32
	if (gtkutil_find_font ("Consolas"))
		return "Consolas 10";
	else
#else
#ifdef __APPLE__
	if (gtkutil_find_font ("Menlo"))
		return "Menlo 13";
	else
#endif
#endif
		return nullptr;
}
