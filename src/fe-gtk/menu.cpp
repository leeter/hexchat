/* X-Chat
 * Copyright (C) 1998-2007 Peter Zelezny.
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
#include <functional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <vector>

#include <fcntl.h>

#include <boost/optional.hpp>
#include <boost/utility/string_ref.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/system/error_code.hpp>

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include "fe-gtk.hpp"

#include <gdk/gdkkeysyms.h>

#include "../common/hexchat.hpp"
#include "../common/hexchatc.hpp"
#include "../common/cfgfiles.hpp"
#include "../common/outbound.hpp"
#include "../common/ignore.hpp"
#include "../common/fe.hpp"
#include "../common/server.hpp"
#include "../common/servlist.hpp"
#include "../common/notify.hpp"
#include "../common/util.hpp"
#include "../common/text.hpp"
#include "../common/userlist.hpp"
#include "../common/session.hpp"
#include "xtext.hpp"
#include "ascii.hpp"
#include "banlist.hpp"
#include "chanlist.hpp"
#include "editlist.hpp"
#include "fkeys.hpp"
#include "gtkutil.hpp"
#include "gtk_helpers.hpp"
#include "maingui.hpp"
#include "notifygui.hpp"
#include "pixmaps.hpp"
#include "rawlog.hpp"
#include "palette.hpp"
#include "plugingui.hpp"
#include "textgui.hpp"
#include "urlgrab.hpp"
#include "menu.hpp"
#include "servlistgui.hpp"
#include "userlistgui.hpp"

namespace
{
	static GSList *submenu_list;

	enum class menu_type : char
	{
		ITEM,
		NEWMENU,
		END,
		SEP,
		TOG,
		RADIO,
		STOCK,
		PIX,
		SUB
	};

	struct mymenu
	{
		const char *text;
		GCallback callback;
		char *image;
		menu_type type;	/* M_XXX */
		unsigned char id;		/* MENU_ID_XXX (menu.hpp) */
		unsigned char state;	/* ticked or not? */
		unsigned char sensitive;	/* shaded out? */
		guint key;				/* GDK_KEY_x */
	};

	CUSTOM_PTR(GList, g_list_free)
}// anon namespace

/* execute a userlistbutton/popupmenu command */

static void
nick_command (session * sess, char *cmd)
{
	if (*cmd == '!')
		hexchat_exec (cmd + 1);
	else
		handle_command (sess, cmd, true);
}

/* fill in the %a %s %n etc and execute the command */

void
nick_command_parse(session *sess, const boost::string_ref & cmd, const boost::string_ref & nick, const boost::string_ref & allnick)
{
	// use string_ref when available
	std::string host(_("Host unknown"));
	const char *account = _("Account unknown");
	struct User *user;
	int len;

/*	if (sess->type == SESS_DIALOG)
	{
		buf = (char *)(GTK_ENTRY (sess->gui->topic_entry)->text);
		buf = strrchr (buf, '@');
		if (buf)
			host = buf + 1;
	} else*/
	{
		user = userlist_find (sess, nick);
		if (user)
		{
			if (user->hostname)
			{
				auto at_idx = user->hostname->find_first_of('@');
				if (at_idx == std::string::npos)
					throw std::runtime_error("invalid user hostname");
				host = user->hostname->substr(at_idx + 1);
			}
			if (user->account)
				account = user->account ? user->account->c_str() : nullptr;
		}
	}

	/* this can't overflow, since popup->cmd is only 256 */
	len = cmd.length() + nick.length() + allnick.length() + 512;
	std::string buf(len, '\0');

	auto_insert (buf, (const unsigned char*)cmd.data(), 0, 0, allnick.data(), sess->channel, "",
		sess->server->get_network(true).data(), host.c_str(),
					 sess->server->nick, nick.data(), account);

	nick_command(sess, &buf[0]);
}

/* userlist button has been clicked */

void
userlist_button_cb (GtkWidget * button, const char *cmd)
{
	bool using_allnicks = false;
	//char *allnicks;
	std::vector<std::string> nicks;
	std::string nick;
	session *sess;

	sess = current_sess;

	if (strstr (cmd, "%a"))
		using_allnicks = true;

	if (sess->type == session::SESS_DIALOG)
	{
		/* fake a selection */
		nicks.emplace_back(sess->channel);
	} else
	{
		/* find number of selected rows */
		nicks = userlist_selection_list (sess->gui->user_tree);
		if (nicks.size() < 1)
		{
			nick_command_parse (sess, cmd, "", "");
			return;
		}
	}

	/* create "allnicks" string */
	std::ostringstream allnicks;
	for (const auto & nick_str : nicks)
	{
		allnicks << nick_str << " ";

		//if (!nick)
		nick = nick_str;

		/* if not using "%a", execute the command once for each nickname */
		if (!using_allnicks)
			nick_command_parse (sess, cmd, nick_str, "");
	}

	if (using_allnicks)
	{
		nick_command_parse (sess, cmd, nick, allnicks.str());
	}
}

/* a popup-menu-item has been selected */

static void
popup_menu_cb (GtkWidget * item, const std::string *cmd)
{
	char *nick;

	/* the userdata is set in menu_quick_item() */
	nick = static_cast<char*>(g_object_get_data(G_OBJECT(item), "u"));

	if (!nick)	/* userlist popup menu */
	{
		/* treat it just like a userlist button */
		userlist_button_cb (nullptr, cmd->c_str());
		return;
	}

	if (!current_sess)	/* for url grabber window */
		nick_command_parse (static_cast<session*>(sess_list->data), *cmd, nick, nick);
	else
		nick_command_parse (current_sess, *cmd, nick, nick);
}

GtkWidget *
menu_toggle_item (const char *label, GtkWidget *menu, GCallback callback, void *userdata,
						int state)
{
	GtkWidget *item;

	item = gtk_check_menu_item_new_with_mnemonic (label);
	gtk_check_menu_item_set_active ((GtkCheckMenuItem*)item, state);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	g_signal_connect (G_OBJECT (item), "activate",
							G_CALLBACK (callback), userdata);
	gtk_widget_show (item);

	return item;
}

GtkWidget *
menu_quick_item (const std::string *cmd, const char label[], GtkWidget * menu, int flags,
					  gpointer userdata, const char icon[])
{
	GtkWidget *img, *item;
	char *path;

	if (!label)
		item = gtk_menu_item_new ();
	else
	{
		if (icon)
		{
			/*if (flags & XCMENU_MARKUP)
				item = gtk_image_menu_item_new_with_markup (label);
			else*/
				item = gtk_image_menu_item_new_with_mnemonic (label);
			img = nullptr;
			if (access (icon, R_OK) == 0)	/* try fullpath */
				img = gtk_image_new_from_file (icon);
			else
			{
				/* try relative to <xdir> */
				path = g_build_filename (get_xdir (), icon, nullptr);
				if (access (path, R_OK) == 0)
					img = gtk_image_new_from_file (path);
				else
					img = gtk_image_new_from_stock (icon, GTK_ICON_SIZE_MENU);
				g_free (path);
			}

			if (img)
				gtk_image_menu_item_set_image ((GtkImageMenuItem *)item, img);
		}
		else
		{
			if (flags & XCMENU_MARKUP)
			{
				item = gtk_menu_item_new_with_label ("");
				auto child_label = GTK_LABEL(gtk_bin_get_child(GTK_BIN(item)));
				if (flags & XCMENU_MNEMONIC)
					gtk_label_set_markup_with_mnemonic (child_label, label);
				else
					gtk_label_set_markup (child_label, label);
			} else
			{
				if (flags & XCMENU_MNEMONIC)
					item = gtk_menu_item_new_with_mnemonic (label);
				else
					item = gtk_menu_item_new_with_label (label);
			}
		}
	}
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	g_object_set_data (G_OBJECT (item), "u", userdata);
	if (cmd)
		g_signal_connect (G_OBJECT (item), "activate",
								G_CALLBACK (popup_menu_cb), (gpointer) cmd);
	if (flags & XCMENU_SHADED)
		gtk_widget_set_sensitive (GTK_WIDGET (item), false);
	gtk_widget_show_all (item);

	return item;
}

static void
menu_quick_item_with_callback(GCallback callback, char *label, GtkWidget * menu,
										 void *arg)
{
	GtkWidget *item;

	item = gtk_menu_item_new_with_label (label);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	g_signal_connect (G_OBJECT (item), "activate",
							G_CALLBACK (callback), arg);
	gtk_widget_show (item);
}

GtkWidget *
menu_quick_sub (const char *name, GtkWidget *menu, GtkWidget **sub_item_ret, int flags, int pos)
{
	if (!name)
		return menu;

	/* Code to add a submenu */
	auto sub_menu = gtk_menu_new ();
	GtkWidget *sub_item;
	if (flags & XCMENU_MARKUP)
	{
		sub_item = gtk_menu_item_new_with_label ("");
		gtk_label_set_markup (GTK_LABEL (gtk_bin_get_child(GTK_BIN (sub_item))), name);
	}
	else
	{
		if (flags & XCMENU_MNEMONIC)
			sub_item = gtk_menu_item_new_with_mnemonic (name);
		else
			sub_item = gtk_menu_item_new_with_label (name);
	}
	gtk_menu_shell_insert (GTK_MENU_SHELL (menu), sub_item, pos);
	gtk_widget_show (sub_item);
	gtk_menu_item_set_submenu (GTK_MENU_ITEM (sub_item), sub_menu);

	if (sub_item_ret)
		*sub_item_ret = sub_item;

	if (flags & XCMENU_DOLIST)
		/* We create a new element in the list */
		submenu_list = g_slist_prepend (submenu_list, sub_menu);
	return sub_menu;
}

static GtkWidget *
menu_quick_endsub ()
{
	/* Just delete the first element in the linked list pointed to by first */
	if (submenu_list)
		submenu_list = g_slist_remove (submenu_list, submenu_list->data);

	if (submenu_list)
		return static_cast<GtkWidget*>(submenu_list->data);
	else
		return nullptr;
}

static void
toggle_cb (GtkWidget *item, char *pref_name)
{
	char buf[256];

	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM (item)))
		snprintf (buf, sizeof (buf), "set %s 1", pref_name);
	else
		snprintf (buf, sizeof (buf), "set %s 0", pref_name);

	handle_command (current_sess, buf, false);
}

static int
is_in_path (const char *cmd)
{
	char *prog = g_strdup (cmd + 1);	/* 1st char is "!" */
	char *path, *orig;
	char **argv;
	int argc;

	orig = prog; /* save for free()ing */
	/* special-case these default entries. */
	/*                  123456789012345678 */
	if (strncmp (prog, "gnome-terminal -x ", 18) == 0)
	/* don't check for gnome-terminal, but the thing it's executing! */
		prog += 18;

	if (g_shell_parse_argv (prog, &argc, &argv, nullptr))
	{
		path = g_find_program_in_path (argv[0]);
		if (path)
		{
			g_free (path);
			g_free (orig);
			g_strfreev (argv);
			return 1;
		}
		g_strfreev (argv);
	}

	g_free (orig);
	return 0;
}

/* syntax: "LABEL~ICON~STUFF~ADDED~LATER~" */

static void
menu_extract_icon (const std::string & name, char **label, char **icon)
{
	const char *p = name.c_str();
	const char *start = nullptr;
	const char *end = nullptr;

	while (*p)
	{
		if (*p == '~')
		{
			/* escape \~ */
			if (p == name || p[-1] != '\\')
			{
				if (!start)
					start = p + 1;
				else if (!end)
					end = p + 1;
			}
		}
		p++;
	}

	if (!end)
		end = p;

	if (start && start != end)
	{
		*label = g_strndup (name.c_str(), (start - name.c_str()) - 1);
		*icon = g_strndup (start, (end - start) - 1);
	}
	else
	{
		*label = g_strdup (name.c_str());
		*icon = nullptr;
	}
}

/* append items to "menu" using the (struct popup*) list provided */

void
menu_create (GtkWidget *menu, const std::vector<popup> &list, char *target, int check_path)
{
	GtkWidget *tempmenu = menu, *subitem = nullptr;
	int childcount = 0;

	submenu_list = g_slist_prepend (0, menu);
	for (const auto & pop : list)
	{
		if (!g_ascii_strncasecmp (pop.name.c_str(), "SUB", 3))
		{
			childcount = 0;
			tempmenu = menu_quick_sub (pop.cmd.c_str(), tempmenu, &subitem, XCMENU_DOLIST|XCMENU_MNEMONIC, -1);

		} else if (!g_ascii_strncasecmp (pop.name.c_str(), "TOGGLE", 6))
		{
			childcount++;
			menu_toggle_item (pop.name.c_str() + 7, tempmenu, G_CALLBACK(toggle_cb), (void*)pop.cmd.c_str(),
									cfg_get_bool (pop.cmd.c_str()));

		} else if (!g_ascii_strncasecmp (pop.name.c_str(), "ENDSUB", 6))
		{
			/* empty sub menu due to no programs in PATH? */
			if (check_path && childcount < 1)
				gtk_widget_destroy (subitem);
			subitem = nullptr;

			if (tempmenu != menu)
				tempmenu = menu_quick_endsub ();
			/* If we get here and tempmenu equals menu that means we havent got any submenus to exit from */

		} else if (!g_ascii_strncasecmp (pop.name.c_str(), "SEP", 3))
		{
			menu_quick_item (0, 0, tempmenu, XCMENU_SHADED, 0, 0);

		} else
		{
			/* default command in hexchat.c */
			if (pop.cmd[0] == 'n' && pop.cmd == "notify -n ASK %s")
			{
				/* don't create this item if already in notify list */
				if (!target || notify_is_in_list (*(current_sess->server), target))
				{
					continue;
				}
			}

			char *icon, *label;
			menu_extract_icon (pop.name, &label, &icon);
			glib_string label_ptr{ label };
			glib_string icon_ptr{ icon };
			if (!check_path || pop.cmd[0] != '!')
			{
				menu_quick_item (&pop.cmd, label, tempmenu, 0, target, icon);
			/* check if the program is in path, if not, leave it out! */
			} else if (is_in_path (pop.cmd.c_str()))
			{
				childcount++;
				menu_quick_item (&pop.cmd, label, tempmenu, 0, target, icon);
			}
		}
	}

	/* Let's clean up the linked list from mem */
	while (submenu_list)
		submenu_list = g_slist_remove (submenu_list, submenu_list->data);
}

static std::unique_ptr<char[]> str_copy;		/* for all pop-up menus */
static GtkWidget *nick_submenu = nullptr;	/* user info submenu */

static void
menu_destroy (GtkWidget *menu, gpointer objtounref)
{
	gtk_widget_destroy (menu);
	g_object_unref (menu);
	if (objtounref)
		g_object_unref (G_OBJECT (objtounref));
	nick_submenu = nullptr;
}

static void
menu_popup (GtkWidget *menu, GdkEventButton *event, gpointer objtounref)
{
	if (event && event->window)
		gtk_menu_set_screen (GTK_MENU (menu), gdk_window_get_screen (event->window));

	g_object_ref (menu);
	g_object_ref_sink (menu);
	g_object_unref (menu);
	g_signal_connect (G_OBJECT (menu), "selection-done",
							G_CALLBACK (menu_destroy), objtounref);
	gtk_menu_popup (GTK_MENU (menu), nullptr, nullptr, nullptr, nullptr,
						 0, event ? event->time : 0);
}

static void
menu_nickinfo_cb (GtkWidget *menu, session *sess)
{
	char buf[512];

	if (!is_session (sess))
		return;

	/* issue a /WHOIS */
	snprintf (buf, sizeof (buf), "WHOIS %s %s", str_copy.get(), str_copy.get());
	handle_command (sess, buf, false);
	/* and hide the output */
	sess->server->skip_next_whois = 1;
}

static void
copy_to_clipboard_cb (GtkWidget *item, const char *url)
{
	gtkutil_copy_to_clipboard (item, nullptr, url);
}

/* returns boolean: Some data is missing */

static gboolean
menu_create_nickinfo_menu (struct User *user, GtkWidget *submenu)
{
	char buf[512];
	char unknown[96];
	gboolean missing = false;
	GtkWidget *item;

	/* let the translators tweak this if need be */
	const char* fmt = _("<tt><b>%-11s</b></tt> %s");
	snprintf (unknown, sizeof (unknown), "<i>%s</i>", _("Unknown"));

	if (user->realname)
	{
		auto real = strip_color (user->realname.get(), static_cast<strip_flags>(STRIP_ALL|STRIP_ESCMARKUP));
		snprintf (buf, sizeof (buf), fmt, _("Real Name:"), real.c_str());
	} else
	{
		snprintf (buf, sizeof (buf), fmt, _("Real Name:"), unknown);
	}
	item = menu_quick_item (nullptr, buf, submenu, XCMENU_MARKUP, nullptr, nullptr);
	g_signal_connect (G_OBJECT (item), "activate",
							G_CALLBACK (copy_to_clipboard_cb), 
							user->realname ? &(*user->realname)[0] : unknown);

	snprintf (buf, sizeof (buf), fmt, _("User:"),
				 user->hostname ? user->hostname->c_str() : unknown);
	item = menu_quick_item (nullptr, buf, submenu, XCMENU_MARKUP, nullptr, nullptr);
	g_signal_connect (G_OBJECT (item), "activate",
							G_CALLBACK (copy_to_clipboard_cb), 
							(gpointer)(user->hostname ? user->hostname->c_str() : unknown));
	
	snprintf (buf, sizeof (buf), fmt, _("Account:"),
				 user->account ? user->account->c_str() : unknown);
	item = menu_quick_item (nullptr, buf, submenu, XCMENU_MARKUP, nullptr, nullptr);
	g_signal_connect (G_OBJECT (item), "activate",
							G_CALLBACK (copy_to_clipboard_cb), 
							user->account ? &(*user->account)[0] : unknown);

	static std::string users_country = user->hostname ? country(user->hostname.get()) : std::string{};
	if (!users_country.empty())
	{
		snprintf (buf, sizeof (buf), fmt, _ ("Country:"), users_country.c_str());
		item = menu_quick_item (nullptr, buf, submenu, XCMENU_MARKUP, nullptr, nullptr);
		g_signal_connect (G_OBJECT (item), "activate",
			G_CALLBACK (copy_to_clipboard_cb), (gpointer)users_country.c_str());
	}

	snprintf (buf, sizeof (buf), fmt, _("Server:"),
				 user->servername ? user->servername->c_str() : unknown);
	item = menu_quick_item (nullptr, buf, submenu, XCMENU_MARKUP, nullptr, nullptr);
	g_signal_connect (G_OBJECT (item), "activate",
							G_CALLBACK (copy_to_clipboard_cb), 
							user->servername ? &(*user->servername)[0] : unknown);

	if (user->lasttalk != User::time_point())
	{
		namespace chrono = std::chrono;
		char min[96];
		const auto minutes_ago =
			chrono::duration_cast<chrono::minutes>(User::clock::now() - user->lasttalk);
		snprintf (min, sizeof (min), _("%d minutes ago"),
					minutes_ago.count());
		snprintf (buf, sizeof (buf), fmt, _("Last Msg:"), min);
	} else
	{
		snprintf (buf, sizeof (buf), fmt, _("Last Msg:"), unknown);
	}
	menu_quick_item (nullptr, buf, submenu, XCMENU_MARKUP, nullptr, nullptr);

	if (user->away)
	{
		auto away = current_sess->server->get_away_message(user->nick);// server_away_find_message(current_sess->server, user->nick);
		if (away)
		{
			auto msg = strip_color (away->first ? away->second : std::string(unknown), static_cast<strip_flags>(STRIP_ALL|STRIP_ESCMARKUP));
			snprintf (buf, sizeof (buf), fmt, _("Away Msg:"), msg.c_str());
			item = menu_quick_item (nullptr, buf, submenu, XCMENU_MARKUP, nullptr, nullptr);
			g_signal_connect (G_OBJECT (item), "activate",
									G_CALLBACK (copy_to_clipboard_cb), 
									away->first ? const_cast<char*>(away->second.c_str()) : unknown);
		}
		else
			missing = true;
	}

	return missing;
}

void
fe_userlist_update (session *sess, struct User *user)
{
	if (!nick_submenu || !str_copy)
		return;

	/* not the same nick as the menu? */
	if (sess->server->p_cmp (user->nick.c_str(), str_copy.get()))
		return;

	/* get rid of the "show" signal */
	g_signal_handlers_disconnect_by_func (nick_submenu, (void*)menu_nickinfo_cb, sess);

	/* destroy all the old items */
	auto menu_container = GTK_CONTAINER(nick_submenu);
	auto items = gtk_container_get_children(menu_container);
	GListPtr children{ items };
	while (items)
	{
		auto next = items->next;
		gtk_widget_destroy(static_cast<GtkWidget*>(items->data));
		items = next;
	}

	/* and re-create them with new info */
	menu_create_nickinfo_menu (user, nick_submenu);
}

void
menu_nickmenu (session *sess, GdkEventButton *event, const std::string &nick, int num_sel)
{
	char buf[512];
	struct User *user;
	GtkWidget *submenu, *menu = gtk_menu_new ();

	str_copy.reset(new_strdup(nick.c_str(), nick.size()));

	submenu_list = 0;	/* first time through, might not be 0 */

	/* more than 1 nick selected? */
	if (num_sel > 1)
	{
		snprintf (buf, sizeof (buf), _("%d nicks selected."), num_sel);
		menu_quick_item (nullptr, buf, menu, 0, nullptr, nullptr);
		menu_quick_item (nullptr, nullptr, menu, XCMENU_SHADED, nullptr, nullptr);
	} else
	{
		user = userlist_find (sess, nick);	/* lasttalk is channel specific */
		if (!user)
			user = userlist_find_global (current_sess->server, nick);
		if (user)
		{
			nick_submenu = submenu = menu_quick_sub (nick.c_str(), menu, nullptr, XCMENU_DOLIST, -1);

			if (menu_create_nickinfo_menu (user, submenu) ||
				 !user->hostname || !user->realname || !user->servername)
			{
				g_signal_connect (G_OBJECT (submenu), "show", G_CALLBACK (menu_nickinfo_cb), sess);
			}

			menu_quick_endsub ();
			menu_quick_item (nullptr, nullptr, menu, XCMENU_SHADED, nullptr, nullptr);
		}
	}

	if (num_sel > 1)
		menu_create (menu, popup_list, nullptr, false);
	else
		menu_create (menu, popup_list, str_copy.get(), false);

	if (num_sel == 0)	/* xtext click */
		menu_add_plugin_items (menu, "\x5$NICK", str_copy.get());
	else	/* userlist treeview click */
		menu_add_plugin_items (menu, "\x5$NICK", nullptr);

	menu_popup (menu, event, nullptr);
}

/* stuff for the View menu */

static void
menu_showhide_cb (session *sess)
{
	if (prefs.hex_gui_hide_menu)
		gtk_widget_hide (sess->gui->menu);
	else
		gtk_widget_show (sess->gui->menu);
}

static void
menu_topic_showhide_cb (session *sess)
{
	if (prefs.hex_gui_topicbar)
		gtk_widget_show (sess->gui->topic_bar);
	else
		gtk_widget_hide (sess->gui->topic_bar);
}

static void
menu_userlist_showhide_cb (session *sess)
{
	mg_decide_userlist (sess, true);
}

static void
menu_ulbuttons_showhide_cb (session *sess)
{
	if (prefs.hex_gui_ulist_buttons)
		gtk_widget_show (sess->gui->button_box);
	else
		gtk_widget_hide (sess->gui->button_box);
}

static void
menu_cmbuttons_showhide_cb (session *sess)
{
	switch (sess->type)
	{
	case session::SESS_CHANNEL:
		if (prefs.hex_gui_mode_buttons)
			gtk_widget_show (sess->gui->topicbutton_box);
		else
			gtk_widget_hide (sess->gui->topicbutton_box);
		break;
	default:
		gtk_widget_hide (sess->gui->topicbutton_box);
	}
}

static void
menu_setting_foreach (void (*callback) (session *), int id, guint state)
{
	static bool in_recursion = false;
	if (in_recursion)
		return;
	in_recursion = true;
	session *sess;
	GSList *list;
	bool maindone = false;	/* do it only once for EVERY tab */
	
	list = sess_list;
	while (list)
	{
		sess = static_cast<session*>(list->data);

		if (!sess->gui->is_tab || !maindone)
		{
			if (sess->gui->is_tab)
				maindone = true;
			if (id != -1)
				gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(sess->gui->menu_item[id]), state);
			if (callback)
				callback (sess);
		}

		list = list->next;
	}
	in_recursion = false;
}

void
menu_bar_toggle (void)
{
	prefs.hex_gui_hide_menu = !prefs.hex_gui_hide_menu;
	menu_setting_foreach (menu_showhide_cb, MENU_ID_MENUBAR, !prefs.hex_gui_hide_menu);
}

static void
menu_bar_toggle_cb (void)
{
	menu_bar_toggle ();
	if (prefs.hex_gui_hide_menu)
		fe_message (_("The Menubar is now hidden. You can show it again"
						  " by pressing Control+F9 or right-clicking in a blank part of"
						  " the main text area."), FE_MSG_INFO);
}

static void
menu_topicbar_toggle (GtkWidget *wid, gpointer ud)
{
	prefs.hex_gui_topicbar = !prefs.hex_gui_topicbar;
	menu_setting_foreach (menu_topic_showhide_cb, MENU_ID_TOPICBAR,
								 prefs.hex_gui_topicbar);
}

static void
menu_userlist_toggle (GtkWidget *wid, gpointer ud)
{
	prefs.hex_gui_ulist_hide = !prefs.hex_gui_ulist_hide;
	menu_setting_foreach (menu_userlist_showhide_cb, MENU_ID_USERLIST,
								 !prefs.hex_gui_ulist_hide);
}

static void
menu_ulbuttons_toggle (GtkWidget *wid, gpointer ud)
{
	prefs.hex_gui_ulist_buttons = !prefs.hex_gui_ulist_buttons;
	menu_setting_foreach (menu_ulbuttons_showhide_cb, MENU_ID_ULBUTTONS,
								 prefs.hex_gui_ulist_buttons);
}

static void
menu_cmbuttons_toggle (GtkWidget *wid, gpointer ud)
{
	prefs.hex_gui_mode_buttons = !prefs.hex_gui_mode_buttons;
	menu_setting_foreach (menu_cmbuttons_showhide_cb, MENU_ID_MODEBUTTONS,
								 prefs.hex_gui_mode_buttons);
}

static void
menu_fullscreen_toggle (GtkWidget *wid, gpointer ud)
{
	if (!prefs.hex_gui_win_fullscreen)
		gtk_window_fullscreen (GTK_WINDOW(parent_window));
	else
	{
		gtk_window_unfullscreen (GTK_WINDOW(parent_window));

#ifdef WIN32
		if (!prefs.hex_gui_win_state) /* not maximized */
		{
			/* other window managers seem to handle this */
			gtk_window_resize (GTK_WINDOW (parent_window),
				prefs.hex_gui_win_width, prefs.hex_gui_win_height);
			gtk_window_move (GTK_WINDOW (parent_window),
				prefs.hex_gui_win_left, prefs.hex_gui_win_top);
		}
#endif
	}
}

void
menu_middlemenu (session *sess, GdkEventButton *event)
{
	GtkWidget *menu;
	GtkAccelGroup *accel_group;

	accel_group = gtk_accel_group_new ();
	menu = menu_create_main (accel_group, false, sess->server->is_away, !sess->gui->is_tab, nullptr);
	//menu_popup (menu, event, accel_group);
}

static void
open_url_cb (GtkWidget *item, char *url)
{
	char buf[512];

	/* pass this to /URL so it can handle irc:// */
	snprintf (buf, sizeof (buf), "URL %s", url);
	handle_command (current_sess, buf, false);
}

void
menu_urlmenu (GdkEventButton *event, const std::string & url)
{
	GtkWidget *menu;
	char *chop;

	str_copy.reset(new_strdup (url.c_str(), url.size()));

	menu = gtk_menu_new ();
	/* more than 51 chars? Chop it */
	if (g_utf8_strlen (str_copy.get(), -1) >= 52)
	{
		std::unique_ptr<char[]> tmp(new_strdup (str_copy.get()));
		chop = g_utf8_offset_to_pointer (tmp.get(), 48);
		chop[0] = chop[1] = chop[2] = '.';
		chop[3] = '\0';
		menu_quick_item (nullptr, tmp.get(), menu, XCMENU_SHADED, nullptr, nullptr);
	} else
	{
		menu_quick_item (nullptr, str_copy.get(), menu, XCMENU_SHADED, nullptr, nullptr);
	}
	menu_quick_item (nullptr, nullptr, menu, XCMENU_SHADED, nullptr, nullptr);

	/* Two hardcoded entries */
	if (std::strncmp (str_copy.get(), "irc://", 6) == 0 ||
		std::strncmp (str_copy.get(), "ircs://",7) == 0)
		menu_quick_item_with_callback (G_CALLBACK(open_url_cb), _("Connect"), menu, str_copy.get());
	else
		menu_quick_item_with_callback (G_CALLBACK(open_url_cb), _("Open Link in Browser"), menu, str_copy.get());
	menu_quick_item_with_callback (G_CALLBACK(copy_to_clipboard_cb), _("Copy Selected Link"), menu, str_copy.get());
	/* custom ones from urlhandlers.conf */
	menu_create (menu, urlhandler_list, str_copy.get(), true);
	menu_add_plugin_items (menu, "\x4$URL", str_copy.get());
	menu_popup (menu, event, nullptr);
}

static void
menu_chan_cycle (GtkWidget * menu, char *chan)
{
	char tbuf[256];

	if (current_sess)
	{
		snprintf (tbuf, sizeof tbuf, "CYCLE %s", chan);
		handle_command (current_sess, tbuf, false);
	}
}

static void
menu_chan_part (GtkWidget * menu, char *chan)
{
	char tbuf[256];

	if (current_sess)
	{
		snprintf (tbuf, sizeof tbuf, "part %s", chan);
		handle_command (current_sess, tbuf, false);
	}
}

static void
menu_chan_join (GtkWidget * menu, const char *chan)
{
	char tbuf[256];

	if (current_sess)
	{
		snprintf (tbuf, sizeof tbuf, "join %s", chan);
		handle_command (current_sess, tbuf, false);
	}
}

void
menu_chanmenu (struct session *sess, GdkEventButton * event, char *chan)
{
	GtkWidget *menu;
	bool is_joined = false;

	if (find_channel (*(sess->server), chan))
		is_joined = true;

	str_copy.reset(new_strdup (chan));

	menu = gtk_menu_new ();

	menu_quick_item (nullptr, chan, menu, XCMENU_SHADED, str_copy.get(), nullptr);
	menu_quick_item (nullptr, nullptr, menu, XCMENU_SHADED, str_copy.get(), nullptr);

	if (!is_joined)
		menu_quick_item_with_callback (G_CALLBACK(menu_chan_join), _("Join Channel"), menu,
												 str_copy.get());
	else
	{
		menu_quick_item_with_callback (G_CALLBACK(menu_chan_part), _("Part Channel"), menu,
												 str_copy.get());
		menu_quick_item_with_callback (G_CALLBACK(menu_chan_cycle), _("Cycle Channel"), menu,
												 str_copy.get());
	}

	menu_addfavoritemenu (sess->server, menu, str_copy.get(), false);

	menu_add_plugin_items (menu, "\x5$CHAN", str_copy.get());
	menu_popup (menu, event, nullptr);
}

static void
menu_delfav_cb (GtkWidget *item, server *serv)
{
	servlist_autojoinedit (static_cast<ircnet*>(serv->network), str_copy.get(), false);
}

static void
menu_addfav_cb (GtkWidget *item, server *serv)
{
	servlist_autojoinedit(static_cast<ircnet*>(serv->network), str_copy.get(), true);
}

void
menu_addfavoritemenu (server *serv, GtkWidget *menu, const char channel[], bool istree)
{
	const char *str;
	
	if (!serv->network)
		return;

	if (channel != str_copy.get())
	{
		str_copy.reset(new_strdup (channel));
	}
	
	if (istree)
		str = _("_Autojoin");
	else
		str = _("Autojoin Channel");

	if (joinlist_is_in_list (serv, channel))
	{
		menu_toggle_item (str, menu, G_CALLBACK(menu_delfav_cb), serv, true);
	}
	else
	{
		menu_toggle_item (str, menu, G_CALLBACK(menu_addfav_cb), serv, false);
	}
}

static void
menu_delautoconn_cb (GtkWidget *item, server *serv)
{
	((ircnet*)serv->network)->flags &= ~FLAG_AUTO_CONNECT;
	servlist_save ();
}

static void
menu_addautoconn_cb (GtkWidget *item, server *serv)
{
	((ircnet*)serv->network)->flags |= FLAG_AUTO_CONNECT;
	servlist_save ();
}

void
menu_addconnectmenu (server *serv, GtkWidget *menu)
{
	if (!serv->network)
		return;

	if (((ircnet*)serv->network)->flags & FLAG_AUTO_CONNECT)
	{
		menu_toggle_item (_("_Auto-Connect"), menu, G_CALLBACK(menu_delautoconn_cb), serv, true);
	}
	else
	{
		menu_toggle_item (_("_Auto-Connect"), menu, G_CALLBACK(menu_addautoconn_cb), serv, false);
	}
}

static void
menu_open_server_list (GtkWidget *wid, gpointer none)
{
	fe_serverlist_open (current_sess);
}

void setup_open(void);
static void
menu_settings (GtkWidget * wid, gpointer none)
{
	setup_open ();
}

static void
menu_usermenu (void)
{
	editlist_gui_open (nullptr, nullptr, usermenu_list, _(DISPLAY_NAME": User menu"),
							 "usermenu", "usermenu.conf", nullptr);
}

static void
usermenu_create (GtkWidget *menu)
{
	menu_create (menu, usermenu_list, "", false);
	menu_quick_item (nullptr, nullptr, menu, XCMENU_SHADED, nullptr, nullptr);	/* sep */
	menu_quick_item_with_callback (menu_usermenu, _("Edit This Menu..."), menu, nullptr);
}

static void
usermenu_destroy (GtkWidget * menu) NOEXCEPT
{
	GListPtr children{ gtk_container_get_children(GTK_CONTAINER(menu)) };
	GList *items = children.get();
	GList *next;

	while (items)
	{
		next = items->next;
		gtk_widget_destroy(GTK_WIDGET(items->data));
		items = next;
	}
}

void
usermenu_update (void)
{
	int done_main = false;
	GSList *list = sess_list;
	session *sess;
	GtkWidget *menu;

	while (list)
	{
		sess = static_cast<session*>(list->data);
		menu = sess->gui->menu_item[MENU_ID_USERMENU];
		if (sess->gui->is_tab)
		{
			if (!done_main && menu)
			{
				usermenu_destroy (menu);
				usermenu_create (menu);
				done_main = true;
			}
		} else if (menu)
		{
			usermenu_destroy (menu);
			usermenu_create (menu);
		}
		list = list->next;
	}
}

static void
menu_newserver_window (GtkWidget * wid, gpointer none)
{
	int old = prefs.hex_gui_tab_chans;

	prefs.hex_gui_tab_chans = 0;
	new_ircwindow(nullptr, nullptr, session::SESS_SERVER, false);
	prefs.hex_gui_tab_chans = old;
}

static void
menu_newchannel_window (GtkWidget * wid, gpointer none)
{
	int old = prefs.hex_gui_tab_chans;

	prefs.hex_gui_tab_chans = 0;
	new_ircwindow(current_sess->server, nullptr, session::SESS_CHANNEL, false);
	prefs.hex_gui_tab_chans = old;
}

static void
menu_newserver_tab (GtkWidget * wid, gpointer none)
{
	int old = prefs.hex_gui_tab_chans;
	int oldf = prefs.hex_gui_tab_newtofront;

	prefs.hex_gui_tab_chans = 1;
	/* force focus if setting is "only requested tabs" */
	if (prefs.hex_gui_tab_newtofront == 2)
		prefs.hex_gui_tab_newtofront = 1;
	new_ircwindow(nullptr, nullptr, session::SESS_SERVER, false);
	prefs.hex_gui_tab_chans = old;
	prefs.hex_gui_tab_newtofront = oldf;
}

static void
menu_newchannel_tab (GtkWidget * wid, gpointer none)
{
	int old = prefs.hex_gui_tab_chans;

	prefs.hex_gui_tab_chans = 1;
	new_ircwindow(current_sess->server, nullptr, session::SESS_CHANNEL, false);
	prefs.hex_gui_tab_chans = old;
}

static void
menu_rawlog (GtkWidget * wid, gpointer none)
{
	open_rawlog (current_sess->server);
}

static void
menu_detach (GtkWidget * wid, gpointer none)
{
	mg_detach (current_sess, 0);
}

static void
menu_close (GtkWidget * wid, gpointer none)
{
	mg_close_sess (current_sess);
}

static void
menu_quit (GtkWidget * wid, gpointer none)
{
	mg_open_quit_dialog (false);
}

static void
menu_search ()
{
	mg_search_toggle (current_sess);
}

static void
menu_search_next (GtkWidget *wid)
{
	mg_search_handle_next(wid, current_sess);
}

static void
menu_search_prev (GtkWidget *wid)
{
	mg_search_handle_previous(wid, current_sess);
}

static void
menu_resetmarker (GtkWidget * wid, gpointer none)
{
	gtk_xtext_reset_marker_pos (GTK_XTEXT (current_sess->gui->xtext));
}

static void
menu_movetomarker (GtkWidget *wid, gpointer none)
{
	marker_reset_reason reason;
	const char *str;

	if (!prefs.hex_text_show_marker)
		PrintText (current_sess, _("Marker line disabled."));
	else
	{
		reason = static_cast<marker_reset_reason>(gtk_xtext_moveto_marker_pos (GTK_XTEXT (current_sess->gui->xtext)));
		switch (reason) {
		case MARKER_WAS_NEVER_SET:
			str = _("Marker line never set."); break;
		case MARKER_IS_SET:
			str = ""; break;
		case MARKER_RESET_MANUALLY:
			str = _("Marker line reset manually."); break;
		case MARKER_RESET_BY_KILL:
			str = _("Marker line reset because exceeded scrollback limit."); break;
		case MARKER_RESET_BY_CLEAR:
			str = _("Marker line reset by CLEAR command."); break;
		default:
			str = _("Marker line state unknown."); break;
		}
		if (str[0])
			PrintText (current_sess, str);
	}
}

static void
menu_copy_selection (GtkWidget * wid, gpointer none)
{
	gtk_xtext_copy_selection (GTK_XTEXT (current_sess->gui->xtext));
}

static void
menu_flushbuffer (GtkWidget * wid, gpointer none)
{
	fe_text_clear (current_sess, 0);
}

static void
savebuffer_req_done (session *sess, char *file)
{
	namespace bfs = boost::filesystem;
	if (!file)
		return;
	bfs::path file_path{ file };
	bfs::ofstream outfile{ file_path, std::ios::trunc | std::ios::out | std::ios::binary };

	if (!outfile)
	{
		return;
	}
	xtext::save(*GTK_XTEXT(sess->gui->xtext), outfile);
	outfile.flush();
	boost::system::error_code ec;
	bfs::permissions(file_path, bfs::owner_read | bfs::owner_write, ec);
}

static void
menu_savebuffer (GtkWidget *, gpointer)
{
	gtkutil_file_req (_("Select an output filename"), (filereqcallback)savebuffer_req_done,
							current_sess, nullptr, nullptr, FRF_WRITE);
}

static void
menu_disconnect (GtkWidget *, gpointer)
{
	handle_command (current_sess, "DISCON", false);
}

static void
menu_reconnect (GtkWidget *, gpointer)
{
	if (current_sess->server->hostname[0])
		handle_command (current_sess, "RECONNECT", false);
	else
		fe_serverlist_open (current_sess);
}

static void
menu_join_cb (GtkWidget *dialog, gint response, GtkEntry *entry)
{
	switch (response)
	{
	case GTK_RESPONSE_ACCEPT:
		menu_chan_join (nullptr, gtk_entry_get_text(entry));
		break;

	case GTK_RESPONSE_HELP:
		chanlist_opengui (current_sess->server, true);
		break;
	}

	gtk_widget_destroy (dialog);
}

static void
menu_join_entry_cb (GtkWidget *entry, GtkDialog *dialog)
{
	gtk_dialog_response (dialog, GTK_RESPONSE_ACCEPT);
}

static void
menu_join (GtkWidget * wid, gpointer none)
{
	GtkWidget *hbox, *dialog, *entry, *label;

	dialog = gtk_dialog_new_with_buttons (_("Join Channel"),
									GTK_WINDOW (parent_window), GtkDialogFlags(),
									_("Retrieve channel list..."), GTK_RESPONSE_HELP,
									GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
									GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
									nullptr);
	gtk_box_set_homogeneous (GTK_BOX (gtk_dialog_get_content_area(GTK_DIALOG (dialog))), true);
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);
	hbox = gtk_hbox_new (true, 0);

	entry = gtk_entry_new ();
	g_object_set(entry, "editable", false, nullptr); /* avoid auto-selection */
	//GTK_ENTRY (entry)->editable = 0;	
	gtk_entry_set_text (GTK_ENTRY (entry), "#");
	g_signal_connect (G_OBJECT (entry), "activate",
							G_CALLBACK (menu_join_entry_cb), dialog);
	gtk_box_pack_end (GTK_BOX (hbox), entry, false, false, 0);

	label = gtk_label_new (_("Enter Channel to Join:"));
	gtk_box_pack_end (GTK_BOX (hbox), label, false, false, 0);

	g_signal_connect (G_OBJECT (dialog), "response",
						   G_CALLBACK (menu_join_cb), entry);

	gtk_container_add (GTK_CONTAINER (gtk_dialog_get_content_area(GTK_DIALOG (dialog))), hbox);

	gtk_widget_show_all (dialog);

	gtk_editable_set_editable (GTK_EDITABLE (entry), true);
	gtk_editable_set_position (GTK_EDITABLE (entry), 1);
}

static void
menu_away (GtkCheckMenuItem *item, gpointer none)
{
	std::string hack(gtk_check_menu_item_get_active(item) ? "away" : "back");
	hack.push_back(0);
	handle_command (current_sess, &hack[0], false);
}

static void
menu_chanlist (GtkWidget * wid, gpointer none)
{
	chanlist_opengui (current_sess->server, false);
}

static void
menu_banlist (GtkWidget * wid, gpointer none)
{
	banlist_opengui (current_sess);
}

#ifdef USE_PLUGIN

static void
menu_loadplugin (void)
{
	plugingui_load ();
}

static void
menu_pluginlist (void)
{
	plugingui_open ();
}

#else

static void
menu_noplugin_info (void)
{
	fe_message (_(DISPLAY_NAME " has been build without plugin support."), FE_MSG_INFO);
}

#define menu_loadplugin menu_noplugin_info
#define menu_pluginlist menu_noplugin_info

#endif

#define usercommands_help  _("User Commands - Special codes:\n\n"\
						   "%c  =  current channel\n"\
									"%e  =  current network name\n"\
									"%m  =  machine info\n"\
						   "%n  =  your nick\n"\
									"%t  =  time/date\n"\
						   "%v  =  HexChat version\n"\
						   "%2  =  word 2\n"\
						   "%3  =  word 3\n"\
						   "&2  =  word 2 to the end of line\n"\
						   "&3  =  word 3 to the end of line\n\n"\
						   "eg:\n"\
						   "/cmd john hello\n\n"\
						   "%2 would be \042john\042\n"\
						   "&2 would be \042john hello\042.")

#define ulbutton_help       _("Userlist Buttons - Special codes:\n\n"\
							"%a  =  all selected nicks\n"\
							"%c  =  current channel\n"\
							"%e  =  current network name\n"\
							"%h  =  selected nick's hostname\n"\
							"%m  =  machine info\n"\
							"%n  =  your nick\n"\
							"%s  =  selected nick\n"\
							"%t  =  time/date\n"\
							"%u  =  selected users account")

#define dlgbutton_help      _("Dialog Buttons - Special codes:\n\n"\
							"%a  =  all selected nicks\n"\
							"%c  =  current channel\n"\
							"%e  =  current network name\n"\
							"%h  =  selected nick's hostname\n"\
							"%m  =  machine info\n"\
							"%n  =  your nick\n"\
							"%s  =  selected nick\n"\
							"%t  =  time/date\n"\
							"%u  =  selected users account")

#define ctcp_help          _("CTCP Replies - Special codes:\n\n"\
						   "%d  =  data (the whole ctcp)\n"\
									"%e  =  current network name\n"\
									"%m  =  machine info\n"\
						   "%s  =  nick who sent the ctcp\n"\
						   "%t  =  time/date\n"\
						   "%2  =  word 2\n"\
						   "%3  =  word 3\n"\
						   "&2  =  word 2 to the end of line\n"\
						   "&3  =  word 3 to the end of line\n\n")

#define url_help           _("URL Handlers - Special codes:\n\n"\
						   "%s  =  the URL string\n\n"\
						   "Putting a ! in front of the command\n"\
						   "indicates it should be sent to a\n"\
						   "shell instead of HexChat")

static void
menu_usercommands (void)
{
	editlist_gui_open (nullptr, nullptr, command_list, _(DISPLAY_NAME": User Defined Commands"),
							 "commands", "commands.conf", usercommands_help);
}

static void
menu_ulpopup (void)
{
	editlist_gui_open (nullptr, nullptr, popup_list, _(DISPLAY_NAME": Userlist Popup menu"), "popup",
							 "popup.conf", ulbutton_help);
}

static void
menu_rpopup (void)
{
	editlist_gui_open (_("Text"), _("Replace with"), replace_list, _(DISPLAY_NAME": Replace"), "replace",
							 "replace.conf", 0);
}

static void
menu_urlhandlers (void)
{
	editlist_gui_open (nullptr, nullptr, urlhandler_list, _(DISPLAY_NAME": URL Handlers"), "urlhandlers",
							 "urlhandlers.conf", url_help);
}

static void
menu_evtpopup (void)
{
	pevent_dialog_show ();
}

static void
menu_keypopup (void)
{
	key_dialog_show ();
}

static void
menu_ulbuttons (void)
{
	editlist_gui_open (nullptr, nullptr, button_list, _(DISPLAY_NAME": Userlist buttons"), "buttons",
							 "buttons.conf", ulbutton_help);
}

static void
menu_dlgbuttons (void)
{
	editlist_gui_open (nullptr, nullptr, dlgbutton_list, _(DISPLAY_NAME": Dialog buttons"), "dlgbuttons",
							 "dlgbuttons.conf", dlgbutton_help);
}

static void
menu_ctcpguiopen (void)
{
	editlist_gui_open (nullptr, nullptr, ctcp_list, _(DISPLAY_NAME": CTCP Replies"), "ctcpreply",
							 "ctcpreply.conf", ctcp_help);
}

static void
menu_docs (GtkWidget *, gpointer)
{
	fe_open_url ("http://hexchat.readthedocs.org");
}

static void
menu_dcc_win (GtkWidget *, gpointer)
{
	fe_dcc_open_recv_win (false);
	fe_dcc_open_send_win (false);
}

static void
menu_dcc_chat_win (GtkWidget *, gpointer)
{
	fe_dcc_open_chat_win (false);
}

void
menu_change_layout (void)
{
	if (prefs.hex_gui_tab_layout == 0)
	{
		menu_setting_foreach (nullptr, MENU_ID_LAYOUT_TABS, 1);
		menu_setting_foreach (nullptr, MENU_ID_LAYOUT_TREE, 0);
		mg_change_layout (0);
	} else
	{
		menu_setting_foreach (nullptr, MENU_ID_LAYOUT_TABS, 0);
		menu_setting_foreach (nullptr, MENU_ID_LAYOUT_TREE, 1);
		mg_change_layout (2);
	}
}

static void
menu_layout_cb (GtkWidget *item, gpointer)
{
	prefs.hex_gui_tab_layout = 2;
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM (item)))
		prefs.hex_gui_tab_layout = 0;

	menu_change_layout ();
}

static void
menu_apply_metres_cb (session *sess)
{
	mg_update_meters (sess->gui);
}

static void
menu_metres_off (GtkWidget *item, gpointer)
{
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM (item)))
	{
		prefs.hex_gui_lagometer = 0;
		prefs.hex_gui_throttlemeter = 0;
		menu_setting_foreach (menu_apply_metres_cb, -1, 0);
	}
}

static void
menu_metres_text (GtkWidget *item, gpointer none)
{
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)))
	{
		prefs.hex_gui_lagometer = 2;
		prefs.hex_gui_throttlemeter = 2;
		menu_setting_foreach (menu_apply_metres_cb, -1, 0);
	}
}

static void
menu_metres_graph (GtkWidget *item, gpointer)
{
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)))
	{
		prefs.hex_gui_lagometer = 1;
		prefs.hex_gui_throttlemeter = 1;
		menu_setting_foreach (menu_apply_metres_cb, -1, 0);
	}
}

static void
menu_metres_both (GtkWidget *item, gpointer)
{
	if (gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(item)))
	{
		prefs.hex_gui_lagometer = 3;
		prefs.hex_gui_throttlemeter = 3;
		menu_setting_foreach (menu_apply_metres_cb, -1, 0);
	}
}

static void
about_dialog_close (GtkDialog *dialog, int response, gpointer data)
{
	gtk_widget_destroy (GTK_WIDGET(dialog));
}

static gboolean
about_dialog_openurl (GtkAboutDialog *dialog, char *uri, gpointer data)
{
	fe_open_url (uri);
	return true;
}

static void
menu_about (GtkWidget *wid, gpointer sess)
{
	GtkAboutDialog *dialog = GTK_ABOUT_DIALOG(gtk_about_dialog_new());
	char comment[512];
	char *license = "This program is free software; you can redistribute it and/or modify\n" \
					"it under the terms of the GNU General Public License as published by\n" \
					"the Free Software Foundation; version 2.\n\n" \
					"This program is distributed in the hope that it will be useful,\n" \
					"but WITHOUT ANY WARRANTY; without even the implied warranty of\n" \
					"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the\n" \
					"GNU General Public License for more details.\n\n" \
					"You should have received a copy of the GNU General Public License\n" \
					"along with this program. If not, see <http://www.gnu.org/licenses/>";

	snprintf  (comment, sizeof(comment), "Compiled: " __DATE__ "\n"
#ifdef WIN32
				"Portable Mode: %s\n"
				"Build Type: x%d\n"
#endif
				"OS: %s",
#ifdef WIN32
				(portable_mode () ? "Yes" : "No"),
				get_cpu_arch (),
#endif
				get_sys_str (false));

	gtk_about_dialog_set_program_name (dialog, DISPLAY_NAME);
	gtk_about_dialog_set_version (dialog, PACKAGE_VERSION);
	gtk_about_dialog_set_license (dialog, license); /* gtk3 can use GTK_LICENSE_GPL_2_0 */
	gtk_about_dialog_set_website (dialog, "http://hexchat.github.io");
	gtk_about_dialog_set_website_label (dialog, "Website");
	gtk_about_dialog_set_logo (dialog, pix_hexchat);
	gtk_about_dialog_set_copyright (dialog, "\302\251 1998-2010 Peter \305\275elezn\303\275\n\302\251 2009-2014 Berke Viktor");
	gtk_about_dialog_set_comments (dialog, comment);

	gtk_window_set_transient_for (GTK_WINDOW(dialog), GTK_WINDOW(parent_window));
	g_signal_connect (G_OBJECT(dialog), "response", G_CALLBACK(about_dialog_close), nullptr);
	g_signal_connect (G_OBJECT(dialog), "activate-link", G_CALLBACK(about_dialog_openurl), nullptr);
	
	gtk_widget_show_all (GTK_WIDGET(dialog));
}

namespace{
	static struct mymenu mymenu[] = {
		{ N_("He_xChat"), 0, 0, menu_type::NEWMENU, MENU_ID_HEXCHAT, 0, 1 },
		{ N_("Network Li_st..."), G_CALLBACK(menu_open_server_list), (char *)&pix_book, menu_type::PIX, 0, 0, 1, GDK_KEY_s },
		{ 0, 0, 0, menu_type::SEP, 0, 0, 0 },

		{ N_("_New"), 0, GTK_STOCK_NEW, menu_type::SUB, 0, 0, 1 },
		{ N_("Server Tab..."), G_CALLBACK(menu_newserver_tab), 0, menu_type::ITEM, 0, 0, 1, GDK_KEY_t },
		{ N_("Channel Tab..."), G_CALLBACK(menu_newchannel_tab), 0, menu_type::ITEM, 0, 0, 1 },
		{ N_("Server Window..."), G_CALLBACK(menu_newserver_window), 0, menu_type::ITEM, 0, 0, 1, GDK_KEY_n },
		{ N_("Channel Window..."), G_CALLBACK(menu_newchannel_window), 0, menu_type::ITEM, 0, 0, 1 },
		{ 0, 0, 0, menu_type::END, 0, 0, 0 },
		{ 0, 0, 0, menu_type::SEP, 0, 0, 0 },

		{ N_("_Load Plugin or Script..."), menu_loadplugin, GTK_STOCK_REVERT_TO_SAVED, menu_type::STOCK, 0, 0, 1 },
		{ 0, 0, 0, menu_type::SEP, 0, 0, 0 },	/* 11 */
#define DETACH_OFFSET (12)
		{ 0, G_CALLBACK(menu_detach), GTK_STOCK_REDO, menu_type::STOCK, 0, 0, 1 },	/* 12 */
#define CLOSE_OFFSET (13)
		{ 0, G_CALLBACK(menu_close), GTK_STOCK_CLOSE, menu_type::STOCK, 0, 0, 1, GDK_KEY_w },
		{ 0, 0, 0, menu_type::SEP, 0, 0, 0 },
		{ N_("_Quit"), G_CALLBACK(menu_quit), GTK_STOCK_QUIT, menu_type::STOCK, 0, 0, 1, GDK_KEY_q },	/* 15 */

		{ N_("_View"), 0, 0, menu_type::NEWMENU, 0, 0, 1 },
#define MENUBAR_OFFSET (17)
		{ N_("_Menu Bar"), menu_bar_toggle_cb, 0, menu_type::TOG, MENU_ID_MENUBAR, 0, 1, GDK_KEY_F9 },
		{ N_("_Topic Bar"), G_CALLBACK(menu_topicbar_toggle), 0, menu_type::TOG, MENU_ID_TOPICBAR, 0, 1 },
		{ N_("_User List"), G_CALLBACK(menu_userlist_toggle), 0, menu_type::TOG, MENU_ID_USERLIST, 0, 1, GDK_KEY_F7 },
		{ N_("U_serlist Buttons"), G_CALLBACK(menu_ulbuttons_toggle), 0, menu_type::TOG, MENU_ID_ULBUTTONS, 0, 1 },
		{ N_("M_ode Buttons"), G_CALLBACK(menu_cmbuttons_toggle), 0, menu_type::TOG, MENU_ID_MODEBUTTONS, 0, 1 },
		{ 0, 0, 0, menu_type::SEP, 0, 0, 0 },
		{ N_("_Channel Switcher"), 0, 0, menu_type::SUB, 0, 0, 1 },	/* 23 */
#define TABS_OFFSET (24)
		{ N_("_Tabs"), G_CALLBACK(menu_layout_cb), 0, menu_type::RADIO, MENU_ID_LAYOUT_TABS, 0, 1 },
		{ N_("T_ree"), 0, 0, menu_type::RADIO, MENU_ID_LAYOUT_TREE, 0, 1 },
		{ 0, 0, 0, menu_type::END, 0, 0, 0 },
		{ N_("_Network Meters"), 0, 0, menu_type::SUB, 0, 0, 1 },	/* 27 */
#define METRE_OFFSET (2)
		{ N_("Off"), G_CALLBACK(menu_metres_off), 0, menu_type::RADIO, 0, 0, 1 },
		{ N_("Graph"), G_CALLBACK(menu_metres_graph), 0, menu_type::RADIO, 0, 0, 1 },
		{ N_("Text"), G_CALLBACK(menu_metres_text), 0, menu_type::RADIO, 0, 0, 1 },
		{ N_("Both"), G_CALLBACK(menu_metres_both), 0, menu_type::RADIO, 0, 0, 1 },
		{ 0, 0, 0, menu_type::END, 0, 0, 0 },	/* 32 */
		{ 0, 0, 0, menu_type::SEP, 0, 0, 0 },
		{ N_("_Fullscreen"), G_CALLBACK(menu_fullscreen_toggle), 0, menu_type::TOG, MENU_ID_FULLSCREEN, 0, 1, GDK_KEY_F11 },

		{ N_("_Server"), 0, 0, menu_type::NEWMENU, 0, 0, 1 },
		{ N_("_Disconnect"), G_CALLBACK(menu_disconnect), GTK_STOCK_DISCONNECT, menu_type::STOCK, MENU_ID_DISCONNECT, 0, 1 },
		{ N_("_Reconnect"), G_CALLBACK(menu_reconnect), GTK_STOCK_CONNECT, menu_type::STOCK, MENU_ID_RECONNECT, 0, 1 },
		{ N_("_Join a Channel..."), G_CALLBACK(menu_join), GTK_STOCK_JUMP_TO, menu_type::STOCK, MENU_ID_JOIN, 0, 1 },
		{ N_("_List of Channels..."), G_CALLBACK(menu_chanlist), GTK_STOCK_INDEX, menu_type::ITEM, 0, 0, 1 },
		{ 0, 0, 0, menu_type::SEP, 0, 0, 0 },
#define AWAY_OFFSET (41)
		{ N_("Marked _Away"), G_CALLBACK(menu_away), 0, menu_type::TOG, MENU_ID_AWAY, 0, 1, GDK_KEY_a },

		{ N_("_Usermenu"), 0, 0, menu_type::NEWMENU, MENU_ID_USERMENU, 0, 1 },	/* 40 */

		{ N_("S_ettings"), 0, 0, menu_type::NEWMENU, 0, 0, 1 },
		{ N_("_Preferences"), G_CALLBACK(menu_settings), GTK_STOCK_PREFERENCES, menu_type::STOCK, 0, 0, 1 },
		{ 0, 0, 0, menu_type::SEP, 0, 0, 0 },
		{ N_("Auto Replace..."), menu_rpopup, 0, menu_type::ITEM, 0, 0, 1 },
		{ N_("CTCP Replies..."), menu_ctcpguiopen, 0, menu_type::ITEM, 0, 0, 1 },
		{ N_("Dialog Buttons..."), menu_dlgbuttons, 0, menu_type::ITEM, 0, 0, 1 },
		{ N_("Keyboard Shortcuts..."), menu_keypopup, 0, menu_type::ITEM, 0, 0, 1 },
		{ N_("Text Events..."), menu_evtpopup, 0, menu_type::ITEM, 0, 0, 1 },
		{ N_("URL Handlers..."), menu_urlhandlers, 0, menu_type::ITEM, 0, 0, 1 },
		{ N_("User Commands..."), menu_usercommands, 0, menu_type::ITEM, 0, 0, 1 },
		{ N_("Userlist Buttons..."), menu_ulbuttons, 0, menu_type::ITEM, 0, 0, 1 },
		{ N_("Userlist Popup..."), menu_ulpopup, 0, menu_type::ITEM, 0, 0, 1 },	/* 52 */

		{ N_("_Window"), 0, 0, menu_type::NEWMENU, 0, 0, 1 },
		{ N_("_Ban List..."), G_CALLBACK(menu_banlist), 0, menu_type::ITEM, 0, 0, 1 },
		{ N_("Character Chart..."), ascii_open, 0, menu_type::ITEM, 0, 0, 1 },
		{ N_("Direct Chat..."), G_CALLBACK(menu_dcc_chat_win), 0, menu_type::ITEM, 0, 0, 1 },
		{ N_("File _Transfers..."), G_CALLBACK(menu_dcc_win), 0, menu_type::ITEM, 0, 0, 1 },
		{ N_("Friends List..."), hexchat::gui::notify::notify_opengui, 0, menu_type::ITEM, 0, 0, 1 },
		{ N_("Ignore List..."), ignore_gui_open, 0, menu_type::ITEM, 0, 0, 1 },
		{ N_("_Plugins and Scripts..."), menu_pluginlist, 0, menu_type::ITEM, 0, 0, 1 },
		{ N_("_Raw Log..."), G_CALLBACK(menu_rawlog), 0, menu_type::ITEM, 0, 0, 1 },	/* 61 */
		{ N_("URL Grabber..."), ::hexchat::gui::url::url_opengui, 0, menu_type::ITEM, 0, 0, 1 },
		{ 0, 0, 0, menu_type::SEP, 0, 0, 0 },
		{ N_("Reset Marker Line"), G_CALLBACK(menu_resetmarker), 0, menu_type::ITEM, 0, 0, 1, GDK_KEY_m },
		{ N_("Move to Marker Line"), G_CALLBACK(menu_movetomarker), 0, menu_type::ITEM, 0, 0, 1, GDK_KEY_M },
		{ N_("_Copy Selection"), G_CALLBACK(menu_copy_selection), 0, menu_type::ITEM, 0, 0, 1, GDK_KEY_C },
		{ N_("C_lear Text"), G_CALLBACK(menu_flushbuffer), GTK_STOCK_CLEAR, menu_type::STOCK, 0, 0, 1 },
		{ N_("Save Text..."), G_CALLBACK(menu_savebuffer), GTK_STOCK_SAVE, menu_type::STOCK, 0, 0, 1 },
#define SEARCH_OFFSET (70)
		{ N_("Search"), 0, GTK_STOCK_JUSTIFY_LEFT, menu_type::SUB, 0, 0, 1 },
		{ N_("Search Text..."), menu_search, GTK_STOCK_FIND, menu_type::STOCK, 0, 0, 1, GDK_KEY_f },
		{ N_("Search Next"), G_CALLBACK(menu_search_next), GTK_STOCK_FIND, menu_type::STOCK, 0, 0, 1, GDK_KEY_g },
		{ N_("Search Previous"), G_CALLBACK(menu_search_prev), GTK_STOCK_FIND, menu_type::STOCK, 0, 0, 1, GDK_KEY_G },
		{ 0, 0, 0, menu_type::END, 0, 0, 0 },

		{ N_("_Help"), 0, 0, menu_type::NEWMENU, 0, 0, 1 },	/* 74 */
		{ N_("_Contents"), G_CALLBACK(menu_docs), GTK_STOCK_HELP, menu_type::STOCK, 0, 0, 1, GDK_KEY_F1 },
		{ N_("_About"), G_CALLBACK(menu_about), GTK_STOCK_ABOUT, menu_type::STOCK, 0, 0, 1 },

		{ 0, 0, 0, menu_type::END, 0, 0, 0 },
	};
} // anonymous namespace

void
menu_set_away (session_gui *gui, int away)
{
	GtkCheckMenuItem *item = GTK_CHECK_MENU_ITEM (gui->menu_item[MENU_ID_AWAY]);

	g_signal_handlers_block_by_func (G_OBJECT (item), (void*)menu_away, nullptr);
	gtk_check_menu_item_set_active (item, away);
	g_signal_handlers_unblock_by_func (G_OBJECT (item), (void*)menu_away, nullptr);
}

void
menu_set_fullscreen (session_gui *gui, int full)
{
	GtkCheckMenuItem *item = GTK_CHECK_MENU_ITEM (gui->menu_item[MENU_ID_FULLSCREEN]);

	g_signal_handlers_block_by_func(G_OBJECT(item), (void*)menu_fullscreen_toggle, nullptr);
	gtk_check_menu_item_set_active (item, full);
	g_signal_handlers_unblock_by_func(G_OBJECT(item), (void*)menu_fullscreen_toggle, nullptr);
}

static GtkWidget * create_img_menu(const char labeltext[], GtkWidget *img)
{
	auto item = gtk_image_menu_item_new_with_mnemonic(labeltext);
	gtk_image_menu_item_set_image((GtkImageMenuItem *)item, img);
	gtk_widget_show(img);

	return item;
}

static GtkWidget * create_icon_menu (const char labeltext[], void *stock_name)
{
	auto img = gtk_image_new_from_pixbuf (*((GdkPixbuf **)stock_name));
	return create_img_menu(labeltext, img);
}

GtkWidget* create_icon_menu_from_stock(const char labeltext[], const char stock_name[])
{
	auto img = gtk_image_new_from_stock(stock_name, GTK_ICON_SIZE_MENU);
	return create_img_menu(labeltext, img);
}

/* Override the default GTK2.4 handler, which would make menu
   bindings not work when the menu-bar is hidden. */
static gboolean
menu_canacaccel (GtkWidget *widget, guint /*signal_id*/, gpointer /*user_data*/)
{
	/* GTK2.2 behaviour */
	return gtk_widget_is_sensitive (widget);
}

/* === STUFF FOR /MENU === */

static GtkMenuItem *
menu_find_item (GtkWidget *menu, const char name[])
{
	GListPtr children{ gtk_container_get_children(GTK_CONTAINER(menu)) };
	GList*items = children.get();
	GtkMenuItem *item;
	GtkWidget *child;
	const char *labeltext;

	while (items)
	{
		item = static_cast<GtkMenuItem*>(items->data);
		child = gtk_bin_get_child(GTK_BIN (item));
		if (child)	/* separators arn't labels, skip them */
		{
			labeltext = static_cast<const char*>(g_object_get_data (G_OBJECT (item), "name"));
			if (!labeltext)
				labeltext = gtk_label_get_text (GTK_LABEL (child));
			if (!menu_streq (labeltext, name, true))
				return item;
		} else if (name == nullptr)
		{
			return item;
		}
		items = items->next;
	}
	return nullptr;
}

static GtkWidget *
menu_find_path (GtkWidget *menu, const std::string & path)
{
	std::string name;
	/* grab the next part of the path */
	auto len = path.find_first_of('/');
	if (len == std::string::npos)
	{
		name = path;
		len = path.size();
	}
	else
		name = path.substr(0, len);

	auto item = menu_find_item (menu, name.c_str());
	if (!item)
		return nullptr;

	menu = gtk_menu_item_get_submenu (item);
	if (!menu)
		return nullptr;

	auto next_path = path.cbegin() + len;
	if (next_path == path.cend())
		return menu;

	return menu_find_path (menu, path.substr(1));
}

static GtkWidget *
menu_find (GtkWidget *menu, const std::string & path, const char label[])
{
	GtkWidget *item = nullptr;

	if (!path.empty())
		menu = menu_find_path (menu, path);
	if (menu)
		item = (GtkWidget *)menu_find_item (menu, label);
	return item;
}

static void
menu_foreach_gui (menu_entry *me, void (*callback) (GtkWidget *, menu_entry *, char *))
{
	GSList *list = sess_list;
	bool tabdone = false;
	session *sess;

	if (!me->is_main)
		return;	/* not main menu */

	while (list)
	{
		sess = static_cast<session*>(list->data);
		/* do it only once for tab sessions, since they share a GUI */
		if (!sess->gui->is_tab || !tabdone)
		{
			callback (sess->gui->menu, me, nullptr);
			if (sess->gui->is_tab)
				tabdone = true;
		}
		list = list->next;
	}
}

static void
menu_update_cb (GtkWidget *menu, menu_entry *me, char *target)
{
	GtkWidget *item;

	item = menu_find (menu, me->path, me->label? me->label->c_str() : nullptr);
	if (item)
	{
		gtk_widget_set_sensitive (item, me->enable);
		/* must do it without triggering the callback */
		auto check_item = GTK_CHECK_MENU_ITEM(item);
		if (check_item)
			gtk_check_menu_item_set_active(check_item, me->state);
	}
}

/* radio state changed via mouse click */
static void
menu_radio_cb (GtkCheckMenuItem *item, menu_entry *me)
{
	me->state = false;
	if (gtk_check_menu_item_get_active(item))
		me->state = true;

	/* update the state, incase this was changed via right-click. */
	/* This will update all other windows and menu bars */
	menu_foreach_gui (me, menu_update_cb);

	if (me->state && me->cmd)
		handle_command (current_sess, &me->cmd.get()[0], false);
}

/* toggle state changed via mouse click */
static void
menu_toggle_cb(GtkCheckMenuItem *item, menu_entry *me)
{
	me->state = false;
	if (gtk_check_menu_item_get_active(item))
		me->state = true;

	/* update the state, incase this was changed via right-click. */
	/* This will update all other windows and menu bars */
	menu_foreach_gui(me, menu_update_cb);

	if (me->state)
		handle_command(current_sess, me->cmd ? &me->cmd.get()[0] : nullptr, false);
	else
		handle_command(current_sess, me->ucmd ? &me->cmd.get()[0] : nullptr, false);
}

static GtkWidget *
menu_radio_item (const char label[], GtkWidget *menu, GCallback callback, void *userdata,
						int state, const char groupname[])
{
	GtkWidget *item;
	GtkMenuItem *parent;
	GSList *grouplist = nullptr;

	parent = menu_find_item (menu, groupname);
	if (parent)
		grouplist = gtk_radio_menu_item_get_group ((GtkRadioMenuItem *)parent);

	item = gtk_radio_menu_item_new_with_label (grouplist, label);
	gtk_check_menu_item_set_active ((GtkCheckMenuItem*)item, state);
	gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
	g_signal_connect (G_OBJECT (item), "activate",
							G_CALLBACK (callback), userdata);
	gtk_widget_show (item);

	return item;
}

static void
menu_reorder (GtkMenu *menu, GtkWidget *item, int pos)
{
	if (pos == 0xffff)	/* outbound.c uses this default */
		return;

	if (pos < 0)	/* position offset from end/bottom */
	{
		GListPtr children{ gtk_container_get_children(GTK_CONTAINER(menu)) };
		gtk_menu_reorder_child(menu, item, (g_list_length(children.get()) + pos) - 1);
	}
	else
		gtk_menu_reorder_child (menu, item, pos);
}

static GtkWidget *
menu_add_radio (GtkWidget *menu, menu_entry *me)
{
	GtkWidget *item = nullptr;
	auto path = me->path.size() > me->root_offset ? me->path.substr(me->root_offset) : std::string();

	if (path.empty())
		menu = menu_find_path (menu, path);
	if (menu)
	{
		item = menu_radio_item (me->label ? me->label->c_str(): nullptr, menu, G_CALLBACK(menu_radio_cb), me, me->state, me->group ? me->group->c_str() : nullptr);
		menu_reorder (GTK_MENU (menu), item, me->pos);
	}
	return item;
}

static GtkWidget *
menu_add_toggle (GtkWidget *menu, menu_entry *me)
{
	GtkWidget *item = nullptr;
	auto path = me->path.size() > me->root_offset ? me->path.substr(me->root_offset) : std::string();

	if (path.empty())
		menu = menu_find_path (menu, path);
	if (menu)
	{
		item = menu_toggle_item (me->label ? me->label->c_str() : nullptr, menu, G_CALLBACK(menu_toggle_cb), me, me->state);
		menu_reorder (GTK_MENU (menu), item, me->pos);
	}
	return item;
}

static GtkWidget *
menu_add_item (GtkWidget *menu, menu_entry *me, char *target)
{
	GtkWidget *item = nullptr;
	auto path = me->path.size() > me->root_offset ? me->path.substr(me->root_offset) : std::string();

	if (!path.empty())
		menu = menu_find_path (menu, path);
	if (menu)
	{
		std::string temp(me->cmd ? me->cmd.get() : std::string());
		item = menu_quick_item(&temp, me->label ? me->label->c_str() : nullptr, menu, me->markup ? XCMENU_MARKUP | XCMENU_MNEMONIC : XCMENU_MNEMONIC, target, me->icon ? me->icon->c_str() : nullptr);
		menu_reorder (GTK_MENU (menu), item, me->pos);
	}
	return item;
}

static GtkWidget *
menu_add_sub (GtkWidget *menu, menu_entry *me)
{
	GtkWidget *item = nullptr;
	auto path = me->path.size() > me->root_offset ? me->path.substr(me->root_offset) : std::string();

	if (path.empty())
		menu = menu_find_path (menu, path);
	if (menu)
	{
		int pos = me->pos;
		if (pos < 0)	/* position offset from end/bottom */
		{
			GListPtr children{ gtk_container_get_children(GTK_CONTAINER(menu)) };
			pos = g_list_length(children.get()) + pos;
		}
		menu_quick_sub (me->label ? me->label->c_str() : nullptr, menu, &item, me->markup ? XCMENU_MARKUP|XCMENU_MNEMONIC : XCMENU_MNEMONIC, pos);
	}
	return item;
}

static void
menu_del_cb (GtkWidget *menu, menu_entry *me, char *target)
{
	auto path = me->path.size() > me->root_offset ? me->path.substr(me->root_offset) : std::string();
	GtkWidget *item = menu_find(menu, path, me->label ? me->label->c_str() : nullptr);
	if (item)
		gtk_widget_destroy (item);
}

static void
menu_add_cb (GtkWidget *menu, menu_entry *me, char *target)
{
	GtkWidget *item;

	if (me->group)	/* have a group name? Must be a radio item */
		item = menu_add_radio (menu, me);
	else if (me->ucmd)	/* have unselect-cmd? Must be a toggle item */
		item = menu_add_toggle (menu, me);
	else if (me->cmd || !me->label)	/* label=nullptr for separators */
		item = menu_add_item (menu, me, target);
	else
		item = menu_add_sub (menu, me);

	if (item)
	{
		gtk_widget_set_sensitive (item, me->enable);
		if (me->key)
		{
			auto accel_group = static_cast<GtkAccelGroup*>(g_object_get_data (G_OBJECT (menu), "accel"));
			if (accel_group)	/* popup menus don't have them */
				gtk_widget_add_accelerator (item, "activate", accel_group, me->key,
													 static_cast<GdkModifierType>(me->modifier), GTK_ACCEL_VISIBLE);
		}
	}
}

char *
fe_menu_add (menu_entry *me)
{
	menu_foreach_gui (me, menu_add_cb);

	if (!me->markup)
		return nullptr;
	
	char *text = nullptr;
	if (!pango_parse_markup(me->label ? me->label->c_str() : nullptr, -1, 0, nullptr, &text, nullptr, nullptr))
		return nullptr;

	/* return the label with markup stripped */
	return text;
}

void
fe_menu_del (menu_entry *me)
{
	menu_foreach_gui (me, menu_del_cb);
}

void
fe_menu_update (menu_entry *me)
{
	menu_foreach_gui (me, menu_update_cb);
}

/* used to add custom menus to the right-click menu */

static void
menu_add_plugin_mainmenu_items (GtkWidget *menu)
{
	/* outbound.c */
	for (auto& me : menu_list)
	{
		if (me->is_main)
			menu_add_cb (menu, me.get(), nullptr);
	}
}

void
menu_add_plugin_items (GtkWidget *menu, char *root, char *target)
{
	/* outbound.c */
	for (auto & me : menu_list)
	{
		if (!me->is_main && !std::strncmp (me->path.c_str(), root + 1, root[0]))
			menu_add_cb (menu, me.get(), target);
	}
}

/* === END STUFF FOR /MENU === */

GtkWidget *
menu_create_main (void *accel_group, bool bar, int away, int toplevel,
						GtkWidget **menu_widgets)
{
	int i = 0;
	GtkWidget *item;
	GtkWidget *menu = nullptr;
	GtkWidget *menu_item = nullptr;
	GtkWidget *menu_bar;
	GtkWidget *usermenu = nullptr;
	GtkWidget *submenu = nullptr;
	int close_mask = STATE_CTRL;
	int away_mask = STATE_ALT;
	char *key_theme = nullptr;
	GtkSettings *settings;
	GSList *group = nullptr;
#ifdef HAVE_GTK_MAC
	int appmenu_offset = 1; /* 0 is for about */
#endif

	if (bar)
	{
		menu_bar = gtk_menu_bar_new ();
#ifdef HAVE_GTK_MAC
		gtkosx_application_set_menu_bar (osx_app, GTK_MENU_SHELL (menu_bar));
#endif
	}
	else
		menu_bar = gtk_menu_new ();

	/* /MENU needs to know this later */
	g_object_set_data (G_OBJECT (menu_bar), "accel", accel_group);

	g_signal_connect (G_OBJECT (menu_bar), "can-activate-accel",
							G_CALLBACK (menu_canacaccel), 0);

	/* set the initial state of toggles */
	mymenu[MENUBAR_OFFSET].state = !prefs.hex_gui_hide_menu;
	mymenu[MENUBAR_OFFSET+1].state = prefs.hex_gui_topicbar;
	mymenu[MENUBAR_OFFSET+2].state = !prefs.hex_gui_ulist_hide;
	mymenu[MENUBAR_OFFSET+3].state = prefs.hex_gui_ulist_buttons;
	mymenu[MENUBAR_OFFSET+4].state = prefs.hex_gui_mode_buttons;

	mymenu[AWAY_OFFSET].state = away;

	switch (prefs.hex_gui_tab_layout)
	{
	case 0:
		mymenu[TABS_OFFSET].state = 1;
		mymenu[TABS_OFFSET+1].state = 0;
		break;
	default:
		mymenu[TABS_OFFSET].state = 0;
		mymenu[TABS_OFFSET+1].state = 1;
	}

	mymenu[METRE_OFFSET].state = 0;
	mymenu[METRE_OFFSET+1].state = 0;
	mymenu[METRE_OFFSET+2].state = 0;
	mymenu[METRE_OFFSET+3].state = 0;
	switch (prefs.hex_gui_lagometer)
	{
	case 0:
		mymenu[METRE_OFFSET].state = 1;
		break;
	case 1:
		mymenu[METRE_OFFSET+1].state = 1;
		break;
	case 2:
		mymenu[METRE_OFFSET+2].state = 1;
		break;
	default:
		mymenu[METRE_OFFSET+3].state = 1;
	}

	/* change Close binding to ctrl-shift-w when using emacs keys */
	settings = gtk_widget_get_settings (menu_bar);
	if (settings)
	{
		g_object_get (settings, "gtk-key-theme-name", &key_theme, nullptr);
		if (key_theme)
		{
			if (!g_ascii_strcasecmp (key_theme, "Emacs"))
			{
				close_mask = STATE_SHIFT | STATE_CTRL;
				mymenu[SEARCH_OFFSET].key = 0;
			}
			g_free (key_theme);
		}
	}

	/* Away binding to ctrl-alt-a if the _Help menu conflicts (FR/PT/IT) */
	{
		const char *help = _("_Help");
		const char *under = strchr (help, '_');
		if (under && (under[1] == 'a' || under[1] == 'A'))
			away_mask = STATE_ALT | STATE_CTRL;
	}

	if (!toplevel)
	{
		mymenu[DETACH_OFFSET].text = N_("_Detach");
		mymenu[CLOSE_OFFSET].text = N_("_Close");
	}
	else
	{
		mymenu[DETACH_OFFSET].text = N_("_Attach");
		mymenu[CLOSE_OFFSET].text = N_("_Close");
	}

	while (1)
	{
		item = nullptr;
		if (mymenu[i].id == MENU_ID_USERMENU && !prefs.hex_gui_usermenu)
		{
			i++;
			continue;
		}

		switch (mymenu[i].type)
		{
		case menu_type::NEWMENU:
			if (menu)
				gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), menu);
			item = menu = gtk_menu_new ();
			if (mymenu[i].id == MENU_ID_USERMENU)
				usermenu = menu;
			menu_item = gtk_menu_item_new_with_mnemonic (_(mymenu[i].text));
			/* record the English name for /menu */
			g_object_set_data (G_OBJECT (menu_item), "name", const_cast<char*>(mymenu[i].text));
#ifdef HAVE_GTK_MAC /* Added to app menu, see below */
			if (!bar || mymenu[i].id != MENU_ID_HEXCHAT)		
#endif
				gtk_menu_shell_append (GTK_MENU_SHELL (menu_bar), menu_item);
			gtk_widget_show (menu_item);
			break;

		case menu_type::PIX:
			item = create_icon_menu (_(mymenu[i].text), mymenu[i].image);
			goto normalitem;

		case menu_type::STOCK:
			item = create_icon_menu_from_stock (_(mymenu[i].text), mymenu[i].image);
			goto normalitem;

		case menu_type::ITEM:
			item = gtk_menu_item_new_with_mnemonic (_(mymenu[i].text));
normalitem:
			if (mymenu[i].key != 0)
				gtk_widget_add_accelerator (item, "activate",static_cast<GtkAccelGroup*>(accel_group),
										mymenu[i].key,
										static_cast<GdkModifierType>(mymenu[i].key == GDK_KEY_F1 ? 0 :
										mymenu[i].key == GDK_KEY_w ? close_mask :
										(g_ascii_isupper (mymenu[i].key)) ?
											STATE_SHIFT | STATE_CTRL :
											STATE_CTRL),
										GTK_ACCEL_VISIBLE);
			if (mymenu[i].callback)
				g_signal_connect (G_OBJECT (item), "activate",
										G_CALLBACK (mymenu[i].callback), 0);
			if (submenu)
				gtk_menu_shell_append (GTK_MENU_SHELL (submenu), item);
			else
				gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
			gtk_widget_show (item);
			break;

		case menu_type::TOG:
			item = gtk_check_menu_item_new_with_mnemonic (_(mymenu[i].text));
togitem:
			/* must avoid callback for Radio buttons */
			//GTK_CHECK_MENU_ITEM (item)->active = mymenu[i].state;
			gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (item),
													 mymenu[i].state);
			if (mymenu[i].key != 0)
				gtk_widget_add_accelerator (item, "activate", static_cast<GtkAccelGroup*>(accel_group),
											mymenu[i].key,
											static_cast<GdkModifierType>(mymenu[i].id == MENU_ID_FULLSCREEN ? 0 :
											mymenu[i].id == MENU_ID_AWAY ? away_mask :
											STATE_CTRL), GTK_ACCEL_VISIBLE);
			if (mymenu[i].callback)
				g_signal_connect (G_OBJECT (item), "toggled",
									G_CALLBACK (mymenu[i].callback), nullptr);

			if (submenu)
				gtk_menu_shell_append (GTK_MENU_SHELL (submenu), item);
			else
				gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
			gtk_widget_show (item);
			gtk_widget_set_sensitive (item, mymenu[i].sensitive);
			break;

		case menu_type::RADIO:
			item = gtk_radio_menu_item_new_with_mnemonic (group, _(mymenu[i].text));
			group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (item));
			goto togitem;

		case menu_type::SEP:
			item = gtk_menu_item_new ();
			gtk_widget_set_sensitive (item, false);
			gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
			gtk_widget_show (item);
			break;

		case menu_type::SUB:
			group = nullptr;
			submenu = gtk_menu_new ();
			item = create_icon_menu_from_stock (_(mymenu[i].text), mymenu[i].image);
			/* record the English name for /menu */
			g_object_set_data (G_OBJECT (item), "name", const_cast<char*>(mymenu[i].text));
			gtk_menu_item_set_submenu (GTK_MENU_ITEM (item), submenu);
			gtk_menu_shell_append (GTK_MENU_SHELL (menu), item);
			gtk_widget_show (item);
			break;

		/*case M_END:*/ default:
			if (!submenu)
			{
				if (menu)
				{
					gtk_menu_item_set_submenu (GTK_MENU_ITEM (menu_item), menu);
					menu_add_plugin_mainmenu_items (menu_bar);
				}
				if (usermenu)
					usermenu_create (usermenu);
				return (menu_bar);
			}
			submenu = nullptr;
		}

		/* record this GtkWidget * so it's state might be changed later */
		if (mymenu[i].id != 0 && menu_widgets)
			/* this ends up in sess->gui->menu_item[MENU_ID_XXX] */
			menu_widgets[mymenu[i].id] = item;

#ifdef HAVE_GTK_MAC
		/* We want HexChat to be the app menu, not including Quit or HexChat itself */
		if (bar && item && i <= CLOSE_OFFSET + 1 && mymenu[i].id != MENU_ID_HEXCHAT)
		{
			if (!submenu || mymenu[i].type == M_MENUSUB)
				gtkosx_application_insert_app_menu_item (osx_app, item, appmenu_offset++);
		}
#endif

		i++;
	}
}
