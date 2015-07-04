/* HexChat
* Copyright (C) 2015 Leetsoftwerx.
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

//#define BOOST_TEST_MODULE common_tests
#include <ctime>
#include <glib.h>
#include <fe.hpp>
#include <util.hpp>
#include <dcc.hpp>
#include <boost/test/unit_test.hpp>

void fe_new_window(struct session *sess, int focus) {}

void fe_print_text(session &sess, char *text, time_t stamp,
		   gboolean no_activity)
{
}

void fe_timeout_remove(int tag) {}

int fe_timeout_add(int interval, GSourceFunc callback, void *userdata)
{
	return g_timeout_add(interval, callback, userdata);
}

void fe_input_remove(int tag) { g_source_remove(tag); }

int fe_input_add(int sok, fia_flags flags, GIOFunc func, void *data)
{
	int tag, type = 0;
	GIOChannel *channel;

#ifdef G_OS_WIN32
	if (flags & FIA_FD)
		channel = g_io_channel_win32_new_fd(sok);
	else
		channel = g_io_channel_win32_new_socket(sok);
#else
	channel = g_io_channel_unix_new(sok);
#endif

	if (flags & FIA_READ)
		type |= G_IO_IN | G_IO_HUP | G_IO_ERR;
	if (flags & FIA_WRITE)
		type |= G_IO_OUT | G_IO_ERR;
	if (flags & FIA_EX)
		type |= G_IO_PRI;

	tag = g_io_add_watch(channel, static_cast<GIOCondition>(type),
			     (GIOFunc)func, data);
	g_io_channel_unref(channel);

	return tag;
}

boost::unit_test::test_suite *init_function(int, char *[])
{
	// create test cases and suites and return a pointer to any enclosing
	// suite, or 0.
	return nullptr;
}

/* === command-line parameter parsing : requires glib 2.6 === */

int fe_args(int argc, char *argv[]) {
	return -1; 
}

void fe_init(void) {}

void fe_main(void) {
	return;
}

void fe_exit(void) {}

void fe_new_server(struct server *) {}

void fe_message(const boost::string_ref &, int) {}

void fe_close_window(struct session *) {}

void fe_beep(session *) { putchar(7); }

void fe_add_rawlog(struct server *, const boost::string_ref &, bool) {}
void fe_set_topic(session *, const std::string &, const std::string &) {}
void fe_cleanup(void) {}
void fe_set_hilight(struct session *) {}
void fe_set_tab_color(struct session *, fe_tab_color) {}
void fe_update_mode_buttons(struct session *, char , char ) {}
void fe_update_channel_key(struct session *) {}
void fe_update_channel_limit(struct session *) {}
int fe_is_chanwindow(struct server *) { return 0; }

void fe_add_chan_list(struct server *, char *, char *, char *)
{
}
void fe_chan_list_end(struct server *) {}
gboolean fe_add_ban_list(struct session *, char *, char *,
			 char *, int)
{
	return 0;
}
gboolean fe_ban_list_end(struct session *, int) { return 0; }
void fe_notify_update(const std::string *) {}
namespace hexchat
{
namespace fe
{
	namespace notify
	{
		void fe_notify_ask(char *name, char *networks) {}
	}
}
}
void fe_text_clear(struct session *sess, int lines) {}
void fe_progressbar_start(struct session *sess) {}
void fe_progressbar_end(struct server *) {}
void fe_userlist_insert(struct session *, struct User *, int, bool) {}
bool fe_userlist_remove(struct session *, struct User const *) { return false; }
void fe_userlist_rehash(struct session *, struct User const *) {}
void fe_userlist_move(struct session *, struct User *, int) {}
void fe_userlist_numbers(session &) {}
void fe_userlist_clear(session &) {}
void fe_userlist_set_selected(struct session *) {}
namespace hexchat
{
namespace fe
{
	namespace dcc
	{
		void fe_dcc_add(struct ::hexchat::dcc::DCC *) {}
		void fe_dcc_update(struct ::hexchat::dcc::DCC *) {}
		void fe_dcc_remove(struct ::hexchat::dcc::DCC *) {}
	}
}
}
void fe_clear_channel(session &sess) {}
void fe_session_callback(struct session *sess) {}
void fe_server_callback(struct server *serv) {}
void fe_url_add(const std::string &) {}
void fe_pluginlist_update(void) {}
void fe_buttons_update(struct session *sess) {}
void fe_dlgbuttons_update(struct session *sess) {}
void fe_dcc_send_filereq(struct session *sess, char *nick, int maxcps,
			 int passive)
{
}
void fe_set_channel(struct session *sess) {}
void fe_set_title(session &sess) {}
void fe_set_nonchannel(struct session *, int) {}
void fe_set_nick(const server &, const char *) {}
void fe_change_nick(struct server *, char *, char *) {}
void fe_ignore_update(int) {}
int fe_dcc_open_recv_win(int) { return FALSE; }
int fe_dcc_open_send_win(int) { return FALSE; }
int fe_dcc_open_chat_win(int) { return FALSE; }
void fe_userlist_hide(session *) {}
void fe_lastlog(session *, session *, char *,
		gtk_xtext_search_flags)
{
}
void fe_set_lag(server &, long) {}
void fe_set_throttle(server *) {}
void fe_set_away(server &) {}
void fe_serverlist_open(session *) {}
void fe_get_bool(char *, char *, GSourceFunc,
		 void *)
{
}
void fe_get_str(char *, char *, GSourceFunc, void *) {}
void fe_get_int(char *, int, GSourceFunc, void *) {}
void fe_idle_add(GSourceFunc func, void *data) { g_idle_add(func, data); }
void fe_ctrl_gui(session *, fe_gui_action, int) {}
int fe_gui_info(session *, int) { return -1; }
void *fe_gui_info_ptr(session *, int) { return nullptr; }
void fe_confirm(const char *, void (*)(void *), void (*)(void *), void *) {}
const char *fe_get_inputbox_contents(struct session *) { return nullptr; }
void fe_set_inputbox_contents(struct session *, char *) {}
int fe_get_inputbox_cursor(struct session *) { return 0; }
void fe_set_inputbox_cursor(struct session *, int, int) {}
void fe_open_url(const char *) {}
void fe_menu_del(menu_entry *) {}
char *fe_menu_add(menu_entry *) { return nullptr; }
void fe_menu_update(menu_entry *) {}
void fe_uselect(struct session *, char *[], int, int) {}
void fe_server_event(server *, fe_serverevents, int) {}
void fe_flash_window(struct session *) {}
void fe_get_file(const char *, char *, void (*)(void *, char *), void *,
		 fe_file_flags)
{
}
void fe_tray_set_flash(const char *, const char *, int) {}
void fe_tray_set_file(const char *) {}
void fe_tray_set_icon(feicon) {}
void fe_tray_set_tooltip(const char *) {}
void fe_tray_set_balloon(const char *, const char *) {}
void fe_userlist_update(session *, struct User *) {}
void fe_open_chan_list(server *, const char *, bool) {}
const char *fe_get_default_font(void) { return nullptr; }
