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

#ifndef HEXCHAT_MENU_HPP
#define HEXCHAT_MENU_HPP

#include <string>
#include <boost/utility/string_ref.hpp>
void nick_command_parse(session *sess, const boost::string_ref &cmd, const boost::string_ref &nick, const boost::string_ref &allnick);

GtkWidget *menu_create_main (void *accel_group, bool bar, int away, int toplevel, GtkWidget **menu_widgets);
void menu_urlmenu (GdkEventButton * event, const std::string& url);
void menu_chanmenu (session *sess, GdkEventButton * event, char *chan);
void menu_addfavoritemenu (server *serv, GtkWidget *menu, const char channel[], bool istree);
void menu_addconnectmenu (server *serv, GtkWidget *menu);
void menu_nickmenu (session *sess, GdkEventButton * event, const std::string &nick, int num_sel);
void menu_middlemenu (session *sess, GdkEventButton *event);
void userlist_button_cb (GtkWidget * button, const char *cmd);

void usermenu_update (void);
GtkWidget *menu_toggle_item (const char *label, GtkWidget *menu, GCallback callback, void *userdata, int state);
GtkWidget *menu_quick_item (const std::string *cmd, const char label[], GtkWidget * menu, int flags, gpointer userdata, const char icon[]);
GtkWidget *menu_quick_sub (const char *name, GtkWidget *menu, GtkWidget **sub_item_ret, int flags, int pos);
GtkWidget* create_icon_menu_from_stock(const char labeltext[], const char stock_name[]);
void menu_create (GtkWidget *menu, const std::vector<popup> & list, char *target, int check_path);
void menu_bar_toggle (void);
void menu_add_plugin_items (GtkWidget *menu, char *root, char *target);
void menu_change_layout (void);

void menu_set_away (session_gui *gui, int away);
void menu_set_fullscreen (session_gui *gui, int fullscreen);

/* for menu_quick functions */
enum xcmenu_flags{
	XCMENU_DOLIST = 1,
	XCMENU_SHADED = 1,
	XCMENU_MARKUP = 2,
	XCMENU_MNEMONIC = 4
};

#define MENU_ID_HEXCHAT_DEF 14
/* menu items we keep a GtkWidget* for (to change their state) */
enum menu_id{
	MENU_ID_AWAY = 1,
	MENU_ID_MENUBAR = 2,
	MENU_ID_TOPICBAR = 3,
	MENU_ID_USERLIST = 4,
	MENU_ID_ULBUTTONS = 5,
	MENU_ID_MODEBUTTONS = 6,
	MENU_ID_LAYOUT_TABS = 7,
	MENU_ID_LAYOUT_TREE = 8,
	MENU_ID_DISCONNECT = 9,
	MENU_ID_RECONNECT = 10,
	MENU_ID_JOIN = 11,
	MENU_ID_USERMENU = 12,
	MENU_ID_FULLSCREEN = 13,
	MENU_ID_HEXCHAT = MENU_ID_HEXCHAT_DEF
};

#if (MENU_ID_NUM < MENU_ID_HEXCHAT_DEF)
#error MENU_ID_NUM is set wrong
#endif

#endif
