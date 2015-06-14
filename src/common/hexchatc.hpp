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

#ifndef HEXCHAT_C_HPP
#define HEXCHAT_C_HPP
#include <atomic>
#include <string>
#include <boost/utility/string_ref_fwd.hpp>
#include "sessfwd.hpp"
#include "serverfwd.hpp"

extern struct hexchatprefs prefs;

extern std::atomic_bool hexchat_is_quitting;
extern gint arg_skip_plugins;	/* command-line args */
extern gint arg_dont_autoconnect;
extern char *arg_url;
extern char **arg_urls;
extern char *arg_command;
extern gint arg_existing;

extern session *current_sess;
extern session *current_tab;

extern std::vector<popup> popup_list;
extern std::vector<popup> button_list;
extern std::vector<popup> dlgbutton_list;
extern std::vector<popup> command_list;
extern std::vector<popup> ctcp_list;
extern std::vector<popup> replace_list;
extern std::vector<popup> usermenu_list;
extern std::vector<popup> urlhandler_list;
extern std::vector<popup> tabmenu_list;
extern GSList *sess_list;
extern GSList *dcc_list;
extern GList *sess_list_by_lastact[];

session * find_channel(const server &serv, const boost::string_ref &chan);
session * find_dialog(const server &serv, const boost::string_ref &nick);
void lastact_update (session * sess);
session * lastact_getfirst (int (*filter) (session *sess));
bool is_session (session * sess);
void session_free (session *killsess);
void lag_check (void);
void hexchat_exit (void);
void hexchat_exec (const char *cmd);

#endif
