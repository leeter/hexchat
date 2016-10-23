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

#ifndef HEXCHAT_FE_HPP
#define HEXCHAT_FE_HPP

#include <cstdint>
#include <string>
#include <boost/optional.hpp>
#include <boost/utility/string_ref_fwd.hpp>
#include <glib.h>
#include "sessfwd.hpp"
struct User;

/* for storage of /menu entries */
struct menu_entry
{
	std::string path;
	boost::optional<std::string> label;
	boost::optional<std::string> cmd;
	boost::optional<std::string> ucmd; /* unselect command (toggles) */
	boost::optional<std::string> group; /* for radio items or NULL */
	boost::optional<std::string> icon; /* filename */
	std::int32_t pos;	/* position */
	int key;
	std::int16_t modifier;	/* keybinding */
	std::int16_t root_offset;	/* bytes to offset ->path */

	bool is_main;	/* is part of the Main menu? (not a popup) */
	char state;	/* state of toggle items */
	bool markup;	/* use pango markup? */
	bool enable;	/* enabled? sensitivity */
};

int fe_args (int argc, char *argv[]);
void fe_init (void);
void fe_main (void);
void fe_cleanup (void);
void fe_exit (void);
int fe_timeout_add(int interval, GSourceFunc callback, void *userdata);
void fe_timeout_remove (int tag);
void fe_new_window (struct session *sess, bool focus);
void fe_new_server (struct server *serv);
void fe_add_rawlog (struct server *serv, const boost::string_ref & text, bool outbound);
enum fe_msg
{
	FE_MSG_WAIT = 1,
	FE_MSG_INFO = 2,
	FE_MSG_WARN = 4,
	FE_MSG_ERROR = 8,
	FE_MSG_MARKUP = 16
};
void fe_message(const boost::string_ref & msg, int flags);
typedef int fia_flags;
enum fia : fia_flags
{
	FIA_READ = 1,
	FIA_WRITE = 2,
	FIA_EX = 4,
	FIA_FD = 8
};
int fe_input_add(int sok, fia_flags flags, GIOFunc func, void *data);
void fe_input_remove (int tag);
void fe_idle_add(GSourceFunc func, void *data);
void fe_set_topic (session *sess, const std::string& topic, const std::string & stripped_topic);
void fe_set_hilight (struct session *sess);
enum class fe_tab_color
{
	theme_default,
	new_data,
	new_message,
	nick_seen
};
void fe_set_tab_color (struct session *sess, fe_tab_color col);
void fe_flash_window (struct session *sess);
void fe_update_mode_buttons (struct session *sess, char mode, char sign);
void fe_update_channel_key (struct session *sess);
void fe_update_channel_limit (struct session *sess);
int fe_is_chanwindow (struct server *serv);
void fe_add_chan_list (struct server *serv, char *chan, char *users,
							  char *topic);
void fe_chan_list_end (struct server *serv);
gboolean fe_add_ban_list (struct session *sess, char *mask, char *who, char *when, int rplcode);
gboolean fe_ban_list_end (struct session *sess, int rplcode);
void fe_notify_update(const std::string* name);
void fe_text_clear (struct session *sess, int lines);
void fe_close_window (struct session *sess);
void fe_progressbar_start (struct session *sess);
void fe_progressbar_end (struct server *serv);
void fe_print_text (session &sess, char *text, time_t stamp,
					gboolean no_activity);
void fe_userlist_insert (struct session *sess, struct User *newuser, int row, bool sel);
bool fe_userlist_remove (struct session *sess, struct User const *user);
void fe_userlist_rehash (struct session *sess, struct User const *user);
void fe_userlist_update (struct session *sess, struct User *user);
void fe_userlist_move (struct session *sess, struct User *user, int new_row);
void fe_userlist_numbers (session &sess);
void fe_userlist_clear (session &sess);
void fe_userlist_set_selected (struct session *sess);
void fe_uselect (session *sess, char *word[], int do_clear, int scroll_to);
int fe_dcc_open_recv_win (int passive);
int fe_dcc_open_send_win (int passive);
int fe_dcc_open_chat_win (int passive);
void fe_clear_channel (session &sess);
void fe_session_callback (struct session *sess);
void fe_server_callback (struct server *serv);
void fe_url_add (const std::string & text);
void fe_pluginlist_update (void);
void fe_buttons_update (session &sess);
void fe_dlgbuttons_update (struct session *sess);
void fe_dcc_send_filereq (struct session *sess, char *nick, int maxcps, int passive);
void fe_set_channel (struct session *sess);
void fe_set_title (session &sess);
void fe_set_nonchannel (struct session *sess, int state);
void fe_set_nick (const server &serv, const char *newnick);
void fe_ignore_update (int level);
void fe_beep ();
void fe_lastlog (session *sess, session *lastlog_sess, const char *sstr, gtk_xtext_search_flags flags);
void fe_set_lag (server &serv, long lag);
void fe_set_throttle (server *serv);
void fe_set_away (server &serv);
void fe_serverlist_open (session *sess);
void fe_get_bool (char *title, char *prompt, GSourceFunc callback, void *userdata);
void fe_get_str(char *prompt, char *def, GSourceFunc callback, void *ud);
void fe_get_int(char *prompt, int def, GSourceFunc callback, void *ud);
typedef int fe_file_flags;
enum fe_file_flag_types{
	FRF_WRITE = 1,				/* save file */
	FRF_MULTIPLE = 2,			/* multi-select */
	FRF_RECENTLYUSED = 4,		/* let gtk decide start dir instead of our config */
	FRF_CHOOSEFOLDER = 8,		/* choosing a folder only */
	FRF_FILTERISINITIAL = 16,	/* filter is initial directory */
	FRF_NOASKOVERWRITE = 32,	/* don't ask to overwrite existing files */
	FRF_EXTENSIONS = 64,		/* specify file extensions to be displayed */
	FRF_MIMETYPES = 128		/* specify file mimetypes to be displayed */
};
void fe_get_file (const char *title, char *initial,
				 void (*callback) (void *userdata, char *file), void *userdata,
				 fe_file_flags flags);
enum fe_gui_action{
	FE_GUI_HIDE,
	FE_GUI_SHOW,
	FE_GUI_FOCUS,
	FE_GUI_FLASH,
	FE_GUI_COLOR,
	FE_GUI_ICONIFY,
	FE_GUI_MENU,
	FE_GUI_ATTACH,
	FE_GUI_APPLY
};
void fe_ctrl_gui (session *sess, fe_gui_action action, int arg);
int fe_gui_info (session &sess, int info_type);
void *fe_gui_info_ptr (session *sess, int info_type);
void fe_confirm (const char *message, void (*yesproc)(void *), void (*noproc)(void *), void *ud);
const char *fe_get_inputbox_contents (struct session *sess);
int fe_get_inputbox_cursor (struct session *sess);
void fe_set_inputbox_contents (struct session *sess, char *text);
void fe_set_inputbox_cursor (struct session *sess, int delta, int pos);
void fe_open_url (const char *url);
void fe_menu_del (menu_entry *);
char *fe_menu_add (menu_entry *);
void fe_menu_update (menu_entry *);
enum class fe_serverevents{
	CONNECT,
	LOGGEDIN,
	DISCONNECT,
	RECONDELAY,
	CONNECTING,
};
void fe_server_event(server *serv, fe_serverevents type, int arg);
/* pass NULL filename2 for default HexChat icon */
void fe_tray_set_flash (const char *filename1, const char *filename2, int timeout);
/* pass NULL filename for default HexChat icon */
void fe_tray_set_file (const char *filename);
enum feicon
{
	FE_ICON_NORMAL = 0,
	FE_ICON_MESSAGE = 2,
	FE_ICON_HIGHLIGHT = 5,
	FE_ICON_PRIVMSG = 8,
	FE_ICON_FILEOFFER = 11
};
void fe_tray_set_icon (feicon icon);
void fe_tray_set_tooltip (const char *text);
void fe_tray_set_balloon (const char *title, const char *text);
void fe_open_chan_list (server *serv, const char *filter, bool do_refresh);
const char *fe_get_default_font ();

#endif
