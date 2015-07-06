/* X-Chat
 * Copyright (C) 2004-2008 Peter Zelezny.
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

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <boost/utility/string_ref.hpp>

#include <gdk/gdkkeysyms.h>

#include "../common/hexchat.hpp"
#include "../common/hexchatc.hpp"
#include "../common/servlist.hpp"
#include "../common/cfgfiles.hpp"
#include "../common/fe.hpp"
#include "../common/server.hpp"
#include "../common/util.hpp"
#include "../common/session.hpp"

#include "fe-gtk.hpp"
#include "gtkutil.hpp"
#include "menu.hpp"
#include "pixmaps.hpp"
#include "fkeys.hpp"
#include "servlistgui.hpp"
#include "gtk_helpers.hpp"

#define SERVLIST_X_PADDING 4			/* horizontal paddig in the network editor */
#define SERVLIST_Y_PADDING 0			/* vertical padding in the network editor */

/* servlistgui.c globals */
static GtkWidget *serverlist_win = nullptr;
static GtkWidget *networks_tree;		/* network TreeView */

static int netlist_win_width = 0;		/* don't hardcode pixels, just use as much as needed by default, save if resized */
static int netlist_win_height = 0;
static int netedit_win_width = 0;
static int netedit_win_height = 0;

static int netedit_active_tab = 0;

/* global user info */
static GtkWidget *entry_nick1;
static GtkWidget *entry_nick2;
static GtkWidget *entry_nick3;
static GtkWidget *entry_guser;
/* static GtkWidget *entry_greal; */

enum {
		SERVER_TREE,
		CHANNEL_TREE,
		CMD_TREE,
		N_TREES
};

/* edit area */
static GtkWidget *edit_win;
static GtkWidget *edit_entry_nick;
static GtkWidget *edit_entry_nick2;
static GtkWidget *edit_entry_user;
static GtkWidget *edit_entry_real;
static GtkWidget *edit_entry_pass;
static GtkWidget *edit_label_nick;
static GtkWidget *edit_label_nick2;
static GtkWidget *edit_label_real;
static GtkWidget *edit_label_user;
static GtkWidget *edit_trees[N_TREES];

static ircnet *selected_net = nullptr;
static ircserver *selected_serv = nullptr;
static commandentry *selected_cmd = nullptr;
static favchannel *selected_chan = nullptr;
static session *servlist_sess;

static void servlist_network_row_cb (GtkTreeSelection *sel, gpointer user_data);
static GtkWidget *servlist_open_edit (GtkWidget *parent, ircnet *net);


static const char *pages[]=
{
	IRC_DEFAULT_CHARSET,
	"IRC (Latin/Unicode Hybrid)",
	"ISO-8859-15 (Western Europe)",
	"ISO-8859-2 (Central Europe)",
	"ISO-8859-7 (Greek)",
	"ISO-8859-8 (Hebrew)",
	"ISO-8859-9 (Turkish)",
	"ISO-2022-JP (Japanese)",
	"SJIS (Japanese)",
	"CP949 (Korean)",
	"KOI8-R (Cyrillic)",
	"CP1251 (Cyrillic)",
	"CP1256 (Arabic)",
	"CP1257 (Baltic)",
	"GB18030 (Chinese)",
	"TIS-620 (Thai)",
	nullptr
};

/* This is our dictionary for authentication types. Keep these in sync with
 * login_types[]! This allows us to re-order the login type dropdown in the
 * network list without breaking config compatibility.
 *
 * Also make sure inbound_nickserv_login() won't break, i.e. if you add a new
 * type that is NickServ-based, add it there as well so that HexChat knows to
 * treat it as such.
 */
static int login_types_conf[] =
{
	LOGIN_DEFAULT,			/* default entry - we don't use this but it makes indexing consistent with login_types[] so it's nice */
	LOGIN_SASL,
#ifdef USE_OPENSSL
	LOGIN_SASLEXTERNAL,
#endif
	LOGIN_PASS,
	LOGIN_MSG_NICKSERV,
	LOGIN_NICKSERV,
#ifdef USE_OPENSSL
	LOGIN_CHALLENGEAUTH,
#endif
	LOGIN_CUSTOM
#if 0
	LOGIN_NS,
	LOGIN_MSG_NS,
	LOGIN_AUTH,
#endif
};

static const char *login_types[]=
{
	"Default",
	"SASL (username + password)",
#ifdef USE_OPENSSL
	"SASL EXTERNAL (cert)",
#endif
	"Server Password (/PASS password)",
	"NickServ (/MSG NickServ + password)",
	"NickServ (/NICKSERV + password)",
#ifdef USE_OPENSSL
	"Challenge Auth (username + password)",
#endif
	"Custom... (connect commands)",
#if 0
	"NickServ (/NS + password)",
	"NickServ (/MSG NS + password)",
	"AUTH (/AUTH nickname password)",
#endif
	nullptr
};

/* poor man's IndexOf() - find the dropdown string index that belongs to the given config value */
static int
servlist_get_login_desc_index (int conf_value)
{
	int i;
	int length = sizeof (login_types_conf) / sizeof (login_types_conf[0]);		/* the number of elements in the conf array */

	for (i = 0; i < length; i++)
	{
		if (login_types_conf[i] == conf_value)
		{
			return i;
		}
	}

	return 0;	/* make the compiler happy */
}

static void
servlist_select_and_show (GtkTreeView *treeview, GtkTreeIter *iter,
								  GtkListStore *store)
{
	GtkTreeSelection *sel;

	sel = gtk_tree_view_get_selection (treeview);

	/* select this network */
	gtk_tree_selection_select_iter (sel, iter);
	/* and make sure it's visible */
	GtkTreePathPtr path{ gtk_tree_model_get_path(GTK_TREE_MODEL(store), iter) };
	if (path)
	{
		gtk_tree_view_scroll_to_cell (treeview, path.get(), nullptr, true, 0.5, 0.5);
		gtk_tree_view_set_cursor (treeview, path.get(), nullptr, false);
	}
}

static void
servlist_channels_populate (ircnet *net, GtkWidget *treeview)
{
	GtkListStore *store;
	GtkTreeIter iter;
	int i;
	favchannel *favchan;
	GSList *list = net->favchanlist;

	store = (GtkListStore *)gtk_tree_view_get_model (GTK_TREE_VIEW (treeview));
	gtk_list_store_clear (store);

	i = 0;
	while (list)
	{
		favchan = static_cast<favchannel *>(list->data);
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (
			store,
			&iter,
			0,
			favchan->name.c_str(),
			1,
			favchan->key ? favchan->key->c_str() : nullptr,
			2,
			true,
			-1);

		if (net->selected == i)
		{
			/* select this server */
			servlist_select_and_show (GTK_TREE_VIEW (treeview), &iter, store);
		}

		i++;
		list = list->next;
	}
}

static void
servlist_servers_populate (ircnet *net, GtkWidget *treeview)
{
	GtkListStore *store;
	GtkTreeIter iter;
	int i;
	ircserver *serv;
	GSList *list = net->servlist;

	store = (GtkListStore *)gtk_tree_view_get_model (GTK_TREE_VIEW (treeview));
	gtk_list_store_clear (store);

	i = 0;
	while (list)
	{
		serv = static_cast<ircserver*>(list->data);
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, 0, serv->hostname, 1, 1, -1);

		if (net->selected == i)
		{
			/* select this server */
			servlist_select_and_show (GTK_TREE_VIEW (treeview), &iter, store);
		}

		i++;
		list = list->next;
	}
}

static void
servlist_commands_populate (ircnet *net, GtkWidget *treeview)
{
	GtkListStore *store;
	GtkTreeIter iter;
	int i;
	commandentry *entry;
	GSList *list = net->commandlist;

	store = (GtkListStore *)gtk_tree_view_get_model (GTK_TREE_VIEW (treeview));
	gtk_list_store_clear (store);

	i = 0;
	while (list)
	{
		entry = static_cast<commandentry*>(list->data);
		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, 0, entry->command.c_str(), 1, 1, -1);

		if (net->selected == i)
		{
			/* select this server */
			servlist_select_and_show (GTK_TREE_VIEW (treeview), &iter, store);
		}

		i++;
		list = list->next;
	}
}

static void
servlist_networks_populate_ (GtkWidget *treeview, GSList *netlist, gboolean favorites)
{
	GtkListStore *store;
	GtkTreeIter iter;
	int i;
	ircnet *net;

	if (!netlist)
	{
		net = servlist_net_add (_("New Network"), "", false);
		servlist_server_add (net, "newserver/6667");
		netlist = network_list;
	}
	store = (GtkListStore *)gtk_tree_view_get_model (GTK_TREE_VIEW (treeview));
	gtk_list_store_clear (store);

	i = 0;
	while (netlist)
	{
		net = static_cast<ircnet*>(netlist->data);
		if (!favorites || (net->flags & FLAG_FAVORITE))
		{
			if (favorites)
				gtk_list_store_insert_with_values (store, &iter, 0x7fffffff, 0, net->name.c_str(), 1, 1, 2, 400, -1);
			else
				gtk_list_store_insert_with_values (store, &iter, 0x7fffffff, 0, net->name.c_str(), 1, 1, 2, (net->flags & FLAG_FAVORITE) ? 800 : 400, -1);
			if (i == prefs.hex_gui_slist_select)
			{
				/* select this network */
				servlist_select_and_show (GTK_TREE_VIEW (treeview), &iter, store);
				selected_net = net;
			}
		}
		i++;
		netlist = netlist->next;
	}
}

static void
servlist_networks_populate (GtkWidget *treeview, GSList *netlist)
{
	servlist_networks_populate_ (treeview, netlist, prefs.hex_gui_slist_fav);
}

static void
servlist_server_row_cb (GtkTreeSelection *sel, gpointer)
{
	if (!selected_net)
		return;

	GtkTreeModel *model;
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected (sel, &model, &iter))
	{
		char *servname;
		int pos;
		gtk_tree_model_get (model, &iter, 0, &servname, -1);
		glib_string servname_ptr{ servname };
		auto serv = servlist_server_find (selected_net, servname, &pos);
		if (serv)
			selected_net->selected = pos;
		selected_serv = serv;
	}
}

static void
servlist_command_row_cb (GtkTreeSelection *sel, gpointer)
{	
	if (!selected_net)
		return;

	GtkTreeModel *model;
	GtkTreeIter iter;

	if (gtk_tree_selection_get_selected (sel, &model, &iter))
	{
		char *cmdname;
		int pos;
		gtk_tree_model_get (model, &iter, 0, &cmdname, -1);
		glib_string cmdname_ptr{ cmdname };
		auto cmd = servlist_command_find (selected_net, cmdname, &pos);
		if (cmd)
			selected_net->selected = pos;
		selected_cmd = cmd;
	}
}

static void
servlist_channel_row_cb (GtkTreeSelection *sel, gpointer)
{
	if (!selected_net)
		return;
	GtkTreeModel *model;
	GtkTreeIter iter;	

	if (gtk_tree_selection_get_selected (sel, &model, &iter))
	{
		char *channame;
		int pos;
		gtk_tree_model_get (model, &iter, 0, &channame, -1);
		glib_string channame_ptr{ channame };
		auto channel = servlist_favchan_find (selected_net, channame, &pos);
		if (channel)
			selected_net->selected = pos;
		selected_chan = channel;
	}
}

static void
servlist_start_editing (GtkTreeView *tree)
{
	GtkTreeModel *model;
	GtkTreeIter iter;

	auto sel = gtk_tree_view_get_selection (tree);

	if (gtk_tree_selection_get_selected (sel, &model, &iter))
	{
		GtkTreePathPtr path{ gtk_tree_model_get_path(GTK_TREE_MODEL(model), &iter) };
		if (path)
		{
			gtk_tree_view_set_cursor (tree, path.get(),
									gtk_tree_view_get_column (tree, 0), true);
		}
	}
}

static void
servlist_addserver (void)
{
	if (!selected_net)
		return;

	auto store = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (edit_trees[SERVER_TREE])));
	servlist_server_add (selected_net, "newserver/6667");

	GtkTreeIter iter;
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 0, "newserver/6667", 1, true, -1);

	/* select this server */
	servlist_select_and_show (GTK_TREE_VIEW (edit_trees[SERVER_TREE]), &iter, store);
	servlist_start_editing (GTK_TREE_VIEW (edit_trees[SERVER_TREE]));

	servlist_server_row_cb (gtk_tree_view_get_selection (GTK_TREE_VIEW (networks_tree)), nullptr);
}

static void
servlist_addcommand (void)
{
	if (!selected_net)
		return;

	auto store = GTK_LIST_STORE (gtk_tree_view_get_model (GTK_TREE_VIEW (edit_trees[CMD_TREE])));
	servlist_command_add (selected_net, "ECHO hello");
	GtkTreeIter iter;
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 0, "ECHO hello", 1, true, -1);

	servlist_select_and_show (GTK_TREE_VIEW (edit_trees[CMD_TREE]), &iter, store);
	servlist_start_editing (GTK_TREE_VIEW (edit_trees[CMD_TREE]));

	servlist_command_row_cb (gtk_tree_view_get_selection (GTK_TREE_VIEW (networks_tree)), nullptr);
}

static void
servlist_addchannel (void)
{
	if (!selected_net)
		return;

	auto store = GTK_LIST_STORE(gtk_tree_view_get_model (GTK_TREE_VIEW (edit_trees[CHANNEL_TREE])));
	servlist_favchan_add (selected_net, "#channel");
	GtkTreeIter iter;
	gtk_list_store_append (store, &iter);
	gtk_list_store_set (store, &iter, 0, "#channel", 1, "", 2, true, -1);

	/* select this server */
	servlist_select_and_show (GTK_TREE_VIEW (edit_trees[CHANNEL_TREE]), &iter, store);
	servlist_start_editing (GTK_TREE_VIEW (edit_trees[CHANNEL_TREE]));

	servlist_channel_row_cb (gtk_tree_view_get_selection (GTK_TREE_VIEW (networks_tree)), nullptr);
}

static void
servlist_addnet_cb (GtkWidget * /*item*/, GtkTreeView *treeview)
{
	auto net = servlist_net_add (_("New Network"), "", true);
	net->encoding = strdup (IRC_DEFAULT_CHARSET);
	servlist_server_add (net, "newserver/6667");

	auto store = (GtkListStore *)gtk_tree_view_get_model (treeview);
	GtkTreeIter iter;
	gtk_list_store_prepend (store, &iter);
	gtk_list_store_set (store, &iter, 0, net->name.c_str(), 1, 1, -1);

	/* select this network */
	servlist_select_and_show (GTK_TREE_VIEW (networks_tree), &iter, store);
	servlist_start_editing (GTK_TREE_VIEW (networks_tree));

	servlist_network_row_cb (gtk_tree_view_get_selection (GTK_TREE_VIEW (networks_tree)), nullptr);
}

static void
servlist_deletenetwork (ircnet *net)
{
	GtkTreeSelection *sel;
	GtkTreeModel *model;
	GtkTreeIter iter;

	/* remove from GUI */
	sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (networks_tree));
	if (gtk_tree_selection_get_selected (sel, &model, &iter))
		gtk_list_store_remove (GTK_LIST_STORE (model), &iter);

	/* remove from list */
	servlist_net_remove (net);

	/* force something to be selected */
	gtk_tree_model_get_iter_first (model, &iter);
	servlist_select_and_show (GTK_TREE_VIEW (networks_tree), &iter,
									  GTK_LIST_STORE (model));
	servlist_network_row_cb (sel, nullptr);
}

static void
servlist_deletenetdialog_cb (GtkDialog *dialog, gint arg1, ircnet *net)
{
	gtk_widget_destroy (GTK_WIDGET (dialog));
	if (arg1 == GTK_RESPONSE_OK)
		servlist_deletenetwork (net);
}

static GSList *
servlist_move_item (GtkTreeView *view, GSList *list, gpointer item, int delta)
{
	GtkTreeIter iter1, iter2;
	/* Keep tree in sync w/ list, there has to be an easier way to get iters */
	auto sel = gtk_tree_view_get_selection (view);
	GtkTreeModel *store;
	gtk_tree_selection_get_selected (sel, &store, &iter1);
	GtkTreePathPtr path{ gtk_tree_model_get_path(store, &iter1) };
	if (delta == 1)
		gtk_tree_path_next (path.get());
	else
		gtk_tree_path_prev (path.get());
	gtk_tree_model_get_iter (store, &iter2, path.get());
	
	auto pos = g_slist_index (list, item);
	if (pos >= 0)
	{
		pos += delta;
		if (pos >= 0)
		{
			list = g_slist_remove (list, item);
			list = g_slist_insert (list, item, pos);

			gtk_list_store_swap (GTK_LIST_STORE (store), &iter1, &iter2);
		}
	}
	
	return list;
}

static gboolean
servlist_net_keypress_cb (GtkWidget * /*wid*/, GdkEventKey *evt, gpointer tree)
{
	gboolean handled = false;
	
	if (!selected_net || prefs.hex_gui_slist_fav)
		return false;

	if (evt->state & STATE_SHIFT)
	{
		if (evt->keyval == GDK_KEY_Up)
		{
			handled = true;
			network_list = servlist_move_item (GTK_TREE_VIEW (tree), network_list, selected_net, -1);
		}
		else if (evt->keyval == GDK_KEY_Down)
		{
			handled = true;
			network_list = servlist_move_item (GTK_TREE_VIEW (tree), network_list, selected_net, +1);
		}
	}

	return handled;
}

static gint
servlist_compare (ircnet *net1, ircnet *net2)
{
	glib_string net1_casefolded(g_utf8_casefold(net1->name.c_str(), net1->name.size()));
	glib_string net2_casefolded(g_utf8_casefold(net2->name.c_str(), net1->name.size()));
	return g_utf8_collate(net1_casefolded.get(), net2_casefolded.get());
}

static void
servlist_sort (GtkWidget * /*button*/, gpointer)
{
	network_list=g_slist_sort(network_list,(GCompareFunc)servlist_compare);
	servlist_networks_populate (networks_tree, network_list);
}

static gboolean
servlist_has_selection (GtkTreeView *tree)
{
	GtkTreeSelection *sel;
	GtkTreeModel *model;
	GtkTreeIter iter;

	/* make sure something is selected */
	sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (tree));
	return gtk_tree_selection_get_selected (sel, &model, &iter);
}

static void
servlist_favor (GtkWidget *, gpointer)
{
	if (!selected_net)
		return;

	GtkTreeModel *model;
	GtkTreeIter iter;
	auto sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (networks_tree));
	if (gtk_tree_selection_get_selected (sel, &model, &iter))
	{
		if (selected_net->flags & FLAG_FAVORITE)
		{
			gtk_list_store_set (GTK_LIST_STORE (model), &iter, 2, 400, -1);
			selected_net->flags &= ~FLAG_FAVORITE;
		}
		else
		{
			gtk_list_store_set (GTK_LIST_STORE (model), &iter, 2, 800, -1);
			selected_net->flags |= FLAG_FAVORITE;
		}
	}
}

static void
servlist_update_from_entry (char *&str, GtkWidget *entry)
{
	if (str)
		free (str);

	if (gtk_entry_get_text (GTK_ENTRY (entry))[0] == 0)
		str = nullptr;
	else
		str = strdup (gtk_entry_get_text (GTK_ENTRY (entry)));
}

static boost::optional<std::string> servlist_update_from_entry(GtkWidget *entry)
{
	const char* text = gtk_entry_get_text(GTK_ENTRY(entry));
	if (!text || text[0] == 0)
		return boost::none;
	else
		return std::string(text);
}

static void
servlist_edit_update (ircnet &net)
{
	net.nick = servlist_update_from_entry (edit_entry_nick);
	net.nick2 = servlist_update_from_entry(edit_entry_nick2);
	servlist_update_from_entry (net.user, edit_entry_user);
	servlist_update_from_entry (net.real, edit_entry_real);
	servlist_update_from_entry (net.pass, edit_entry_pass);
}

static void
servlist_edit_close_cb (GtkWidget * /*button*/, gpointer)
{
	if (selected_net)
		servlist_edit_update (*selected_net);

	gtk_widget_destroy (edit_win);
	edit_win = nullptr;
}

static gint
servlist_editwin_delete_cb (GtkWidget * /*win*/, GdkEventAny *, gpointer)
{
	servlist_edit_close_cb (nullptr, nullptr);
	return false;
}

static gboolean
servlist_configure_cb (GtkWindow *win, GdkEventConfigure *, gpointer)
{
	/* remember the window size */
	gtk_window_get_size (win, &netlist_win_width, &netlist_win_height);
	return false;
}

static gboolean
servlist_edit_configure_cb (GtkWindow *win, GdkEventConfigure *, gpointer)
{
	/* remember the window size */
	gtk_window_get_size (win, &netedit_win_width, &netedit_win_height);
	return false;
}

static void
servlist_edit_cb (GtkWidget * /*but*/, gpointer)
{
	if (!servlist_has_selection (GTK_TREE_VIEW (networks_tree)))
		return;

	edit_win = servlist_open_edit (serverlist_win, selected_net);
	gtkutil_set_icon (edit_win);
	servlist_servers_populate (selected_net, edit_trees[SERVER_TREE]);
	servlist_channels_populate (selected_net, edit_trees[CHANNEL_TREE]);
	servlist_commands_populate (selected_net, edit_trees[CMD_TREE]);
	g_signal_connect (G_OBJECT (edit_win), "delete_event",
							G_CALLBACK (servlist_editwin_delete_cb), 0);
	g_signal_connect (G_OBJECT (edit_win), "configure_event",
							G_CALLBACK (servlist_edit_configure_cb), 0);
	gtk_widget_show (edit_win);
}

static void
servlist_deletenet_cb (GtkWidget * /*item*/, ircnet *net)
{
	if (!servlist_has_selection (GTK_TREE_VIEW (networks_tree)))
		return;

	net = selected_net;
	if (!net)
		return;
	auto dialog = gtk_message_dialog_new (GTK_WINDOW (serverlist_win),
												static_cast<GtkDialogFlags>(GTK_DIALOG_DESTROY_WITH_PARENT |
												GTK_DIALOG_MODAL),
												GTK_MESSAGE_QUESTION,
												GTK_BUTTONS_OK_CANCEL,
							_("Really remove network \"%s\" and all its servers?"),
												net->name.c_str());
	g_signal_connect (dialog, "response",
							G_CALLBACK (servlist_deletenetdialog_cb), net);
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);
	gtk_widget_show (dialog);
}

static void
servlist_deleteserver (ircserver *serv, GtkTreeModel *model)
{
	GtkTreeSelection *sel;
	GtkTreeIter iter;

	/* don't remove the last server */
	if (selected_net && g_slist_length (selected_net->servlist) < 2)
		return;

	/* remove from GUI */
	sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (edit_trees[SERVER_TREE]));
	if (gtk_tree_selection_get_selected (sel, &model, &iter))
		gtk_list_store_remove (GTK_LIST_STORE (model), &iter);

	/* remove from list */
	if (selected_net)
		servlist_server_remove (selected_net, serv);
}

static void
servlist_editbutton_cb (GtkWidget * /*item*/, GtkNotebook *notebook)
{
	servlist_start_editing (GTK_TREE_VIEW (edit_trees[gtk_notebook_get_current_page(notebook)]));
}

static void
servlist_deleteserver_cb (void)
{
	GtkTreeIter iter;

	/* find the selected item in the GUI */
	auto model = gtk_tree_view_get_model (GTK_TREE_VIEW (edit_trees[SERVER_TREE]));
	auto sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (edit_trees[SERVER_TREE]));

	if (gtk_tree_selection_get_selected (sel, &model, &iter))
	{
		char *servname;
		gtk_tree_model_get (model, &iter, 0, &servname, -1);
		glib_string servname_ptr{ servname };
		auto serv = servlist_server_find (selected_net, servname, nullptr);
		if (serv)
		{
			servlist_deleteserver (serv, model);
		}
	}
}

static void
servlist_deletecommand (commandentry *entry, GtkTreeModel *model)
{
	GtkTreeSelection *sel;
	GtkTreeIter iter;

	/* remove from GUI */
	sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (edit_trees[CMD_TREE]));
	if (gtk_tree_selection_get_selected (sel, &model, &iter))
	{
		gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
	}

	/* remove from list */
	if (selected_net)
	{
		servlist_command_remove (selected_net, entry);
	}
}

static void
servlist_deletecommand_cb (void)
{
	GtkTreeIter iter;

	/* find the selected item in the GUI */
	auto model = gtk_tree_view_get_model (GTK_TREE_VIEW (edit_trees[CMD_TREE]));
	auto sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (edit_trees[CMD_TREE]));

	if (gtk_tree_selection_get_selected (sel, &model, &iter))
	{
		char *command;
		gtk_tree_model_get (model, &iter, 0, &command, -1);			/* query the content of the selection */
		glib_string command_ptr{ command };
		auto entry = servlist_command_find (selected_net, command, nullptr);
		if (entry)
		{
			servlist_deletecommand (entry, model);
		}
	}
}

static void
servlist_deletechannel (favchannel *favchan, GtkTreeModel *model)
{
	GtkTreeIter iter;

	/* remove from GUI */
	auto sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (edit_trees[CHANNEL_TREE]));
	if (gtk_tree_selection_get_selected (sel, &model, &iter))
	{
		gtk_list_store_remove (GTK_LIST_STORE (model), &iter);
	}

	/* remove from list */
	if (selected_net)
	{
		servlist_favchan_remove (selected_net, favchan);
	}
}

static void
servlist_deletechannel_cb (void)
{
	GtkTreeIter iter;

	/* find the selected item in the GUI */
	auto model = gtk_tree_view_get_model (GTK_TREE_VIEW (edit_trees[CHANNEL_TREE]));
	auto sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (edit_trees[CHANNEL_TREE]));

	if (gtk_tree_selection_get_selected (sel, &model, &iter))
	{
		char *name;
		char *key;
		gtk_tree_model_get (model, &iter, 0, &name, 1, &key, -1);			/* query the content of the selection */
		glib_string name_ptr{ name };
		auto favchan = servlist_favchan_find (selected_net, name, nullptr);
		if (favchan)
		{
			servlist_deletechannel (favchan, model);
		}
	}
}

static ircnet *
servlist_find_selected_net (GtkTreeSelection *sel)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	
	ircnet *net = nullptr;

	if (gtk_tree_selection_get_selected (sel, &model, &iter))
	{
		char *netname;
		gtk_tree_model_get (model, &iter, 0, &netname, -1);
		glib_string netname_ptr{ netname };
		int pos;
		net = servlist_net_find (netname, &pos, strcmp);
		if (net)
			prefs.hex_gui_slist_select = pos;
	}

	return net;
}

static void
servlist_network_row_cb (GtkTreeSelection *sel, gpointer)
{
	ircnet *net;

	selected_net = nullptr;

	net = servlist_find_selected_net (sel);
	if (net)
		selected_net = net;
}

static int
servlist_savegui (void)
{
	char *sp;
	const char *nick1, *nick2;

	/* check for blank username, ircd will not allow this */
	if (gtk_entry_get_text (GTK_ENTRY (entry_guser))[0] == 0)
		return 1;

	/* if (gtk_entry_get_text (GTK_ENTRY (entry_greal))[0] == 0)
		return 1; */

	nick1 = gtk_entry_get_text (GTK_ENTRY (entry_nick1));
	nick2 = gtk_entry_get_text (GTK_ENTRY (entry_nick2));

	/* ensure unique nicknames */
	if (!rfc_casecmp (nick1, nick2))
		return 2;

	safe_strcpy (prefs.hex_irc_nick1, nick1);
	safe_strcpy (prefs.hex_irc_nick2, nick2);
	safe_strcpy (prefs.hex_irc_nick3, gtk_entry_get_text (GTK_ENTRY (entry_nick3)));
	safe_strcpy (prefs.hex_irc_user_name, gtk_entry_get_text (GTK_ENTRY (entry_guser)));
	sp = strchr (prefs.hex_irc_user_name, ' ');
	if (sp)
		sp[0] = 0;	/* spaces will break the login */
	/* strcpy (prefs.hex_irc_real_name, gtk_entry_get_text (GTK_ENTRY (entry_greal))); */
	servlist_save ();
	save_config (); /* For nicks stored in hexchat.conf */

	return 0;
}

static gboolean
servlist_get_iter_from_name (GtkTreeModel *model, gchar *name, GtkTreeIter *iter)
{
	GtkTreePathPtr path{ gtk_tree_path_new_from_string(name) };
	return gtk_tree_model_get_iter(model, iter, path.get());
}

static void
servlist_addbutton_cb (GtkWidget * /*item*/, GtkNotebook *notebook)
{
	switch (gtk_notebook_get_current_page (notebook))
	{
	case SERVER_TREE:
			servlist_addserver ();
			break;
	case CHANNEL_TREE:
			servlist_addchannel ();
			break;
	case CMD_TREE:
			servlist_addcommand ();
			break;
	default:
			break;
	}
}

static void
servlist_deletebutton_cb (GtkWidget * /*item*/, GtkNotebook *notebook)
{
	switch (gtk_notebook_get_current_page (notebook))
	{
	case SERVER_TREE:
			servlist_deleteserver_cb ();
			break;
	case CHANNEL_TREE:
			servlist_deletechannel_cb ();
			break;
	case CMD_TREE:
			servlist_deletecommand_cb ();
			break;
	default:
			break;
	}
}

static gboolean
servlist_keypress_cb (GtkWidget * /*wid*/, GdkEventKey *evt, GtkNotebook *notebook)
{
	bool handled = false;
	int delta = 0;
	
	if (!selected_net)
		return false;

	if (evt->state & STATE_SHIFT)
	{
		if (evt->keyval == GDK_KEY_Up)
		{
			handled = true;
			delta = -1;
		}
		else if (evt->keyval == GDK_KEY_Down)
		{
			handled = true;
			delta = +1;
		}
	}
	
	if (handled)
	{
		switch (gtk_notebook_get_current_page (notebook))
		{
			case SERVER_TREE:
				if (selected_serv)
					selected_net->servlist = servlist_move_item (GTK_TREE_VIEW (edit_trees[SERVER_TREE]), 
																selected_net->servlist, selected_serv, delta);
				break;
			case CHANNEL_TREE:
				if (selected_chan)
					selected_net->favchanlist = servlist_move_item (GTK_TREE_VIEW (edit_trees[CHANNEL_TREE]), 
																	selected_net->favchanlist, selected_chan, delta);
				break;
			case CMD_TREE:
				if (selected_cmd)
					selected_net->commandlist = servlist_move_item (GTK_TREE_VIEW (edit_trees[CMD_TREE]), 
																	selected_net->commandlist, selected_cmd, delta);
				break;
		}
	}
	
	return handled;
}

void
servlist_autojoinedit (ircnet *net, char *channel, gboolean add)
{
	if (add)
	{
		servlist_favchan_add (net, channel);
		servlist_save ();
	}
	else
	{
		auto fav = servlist_favchan_find (net, channel, nullptr);
		if (fav)
		{
			servlist_favchan_remove (net, fav);
			servlist_save ();
		}
	}
}

static void
servlist_toggle_global_user (gboolean sensitive)
{
	gtk_widget_set_sensitive (edit_entry_nick, sensitive);
	gtk_widget_set_sensitive (edit_label_nick, sensitive);

	gtk_widget_set_sensitive (edit_entry_nick2, sensitive);
	gtk_widget_set_sensitive (edit_label_nick2, sensitive);

	gtk_widget_set_sensitive (edit_entry_user, sensitive);
	gtk_widget_set_sensitive (edit_label_user, sensitive);

	gtk_widget_set_sensitive (edit_entry_real, sensitive);
	gtk_widget_set_sensitive (edit_label_real, sensitive);
}

static void
servlist_connect_cb (GtkWidget * /*button*/, gpointer)
{
	if (!selected_net)
		return;

	int servlist_err = servlist_savegui ();
	if (servlist_err == 1)
	{
		fe_message (_("User name cannot be left blank."), FE_MSG_ERROR);
		return;
	}

	if (!is_session (servlist_sess))
		servlist_sess = nullptr;	/* open a new one */

	session *chosen = servlist_sess;

	servlist_sess = nullptr;	/* open a new one */

	for (auto list = sess_list; list; list = g_slist_next(list))
	{
		auto sess = static_cast<session*>(list->data);
		if (sess->server->network == selected_net)
		{
			servlist_sess = sess;
			if (sess->server->connected)
				servlist_sess = nullptr;	/* open a new one */
			break;
		}
	}

	/* use the chosen one, if it's empty */
	if (!servlist_sess &&
			chosen &&
			!chosen->server->connected &&
			chosen->server->server_session->channel[0] == 0)
	{
		servlist_sess = chosen;
	}

	servlist_connect (servlist_sess, *selected_net, true);

	gtk_widget_destroy (serverlist_win);
	serverlist_win = nullptr;
	selected_net = nullptr;
}

static void
servlist_celledit_cb (GtkCellRendererText *, gchar *arg1, gchar *arg2,
							 gpointer user_data)
{
	if (!arg1 || !arg2)
		return;

	GtkTreePathPtr path( gtk_tree_path_new_from_string (arg1));
	if (!path)
		return;

	GtkTreeModel *model = static_cast<GtkTreeModel *>(user_data);
	GtkTreeIter iter;

	if (!gtk_tree_model_get_iter (model, &iter, path.get()))
	{
		return;
	}
	char *netname;
	gtk_tree_model_get (model, &iter, 0, &netname, -1);
	glib_string netname_ptr{ netname };
	auto net = servlist_net_find (netname, nullptr, strcmp);
	if (net)
	{
		/* delete empty item */
		if (arg2[0] == 0)
		{
			servlist_deletenetwork (net);
			return;
		}

		net->name = arg2;
		gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, net->name.c_str(), -1);
	}
}

static void
servlist_check_cb (GtkWidget *but, gpointer num_p)
{
	if (!selected_net)
		return;

	int num = GPOINTER_TO_INT(num_p);
	if ((1 << num) == FLAG_CYCLE || (1 << num) == FLAG_USE_PROXY)
	{
		/* these ones are reversed, so it's compat with 2.0.x */
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (but)))
			selected_net->flags &= ~(1 << num);
		else
			selected_net->flags |= (1 << num);
	} else
	{
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (but)))
			selected_net->flags |= (1 << num);
		else
			selected_net->flags &= ~(1 << num);
	}

	if ((1 << num) == FLAG_USE_GLOBAL)
	{
		servlist_toggle_global_user (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (but)));
	}
}

static GtkWidget *
servlist_create_check (int num, int state, GtkWidget *table, int row, int col, char *labeltext)
{
	auto but = gtk_check_button_new_with_label (labeltext);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (but), state);
	g_signal_connect (G_OBJECT (but), "toggled",
							G_CALLBACK (servlist_check_cb), GINT_TO_POINTER (num));
	gtk_table_attach(GTK_TABLE(table), but, col, col + 2, row, row + 1, GTK_FILL | GTK_EXPAND, GtkAttachOptions(), SERVLIST_X_PADDING, SERVLIST_Y_PADDING);
	gtk_widget_show (but);

	return but;
}

static GtkWidget *servlist_create_entry(GtkWidget *table, const char *labeltext,
					int row, const char *def,
					GtkWidget **label_ret, char *tip)
{
	GtkWidget *label, *entry;

	label = gtk_label_new_with_mnemonic(labeltext);
	if (label_ret)
		*label_ret = label;
	gtk_widget_show(label);
	gtk_table_attach(GTK_TABLE(table), label, 0, 1, row, row + 1, GTK_FILL,
			 GtkAttachOptions(), SERVLIST_X_PADDING,
			 SERVLIST_Y_PADDING);
	gtk_misc_set_alignment(GTK_MISC(label), 0, 0.5);

	entry = gtk_entry_new();
	gtk_widget_set_tooltip_text(entry, tip);
	gtk_widget_show(entry);
	gtk_entry_set_text(GTK_ENTRY(entry), def ? def : "");
	gtk_label_set_mnemonic_widget(GTK_LABEL(label), entry);

	gtk_table_attach(GTK_TABLE(table), entry, 1, 2, row, row + 1,
			 GTK_FILL | GTK_EXPAND, GtkAttachOptions(),
			 SERVLIST_X_PADDING, SERVLIST_Y_PADDING);

	return entry;
}

static gint
servlist_delete_cb (GtkWidget * /*win*/, GdkEventAny *, gpointer)
{
	servlist_savegui ();
	serverlist_win = nullptr;
	selected_net = nullptr;

	if (sess_list == nullptr)
		hexchat_exit ();

	return false;
}

static void
servlist_close_cb (GtkWidget * /*button*/, gpointer)
{
	servlist_savegui ();
	gtk_widget_destroy (serverlist_win);
	serverlist_win = nullptr;
	selected_net = nullptr;

	if (sess_list == nullptr)
		hexchat_exit ();
}

/* convert "host:port" format to "host/port" */

static char *
servlist_sanitize_hostname (const char host[])
{
	char *ret, *c, *e;

	ret = strdup (host);

	c = strchr  (ret, ':');
	e = strrchr (ret, ':');

	/* if only one colon exists it's probably not IPv6 */
	if (c && c == e)
		*c = '/';

	return ret;
}

/* remove leading slash */
static std::string
servlist_sanitize_command (const std::string& cmd)
{
	if (cmd[0] == '/')
	{
		return cmd.substr(1);
	}
	else
	{
		return cmd;
	}
}

static void
servlist_editserver_cb (GtkCellRendererText *, gchar *name, gchar *newval, gpointer user_data)
{
	if (!selected_net)
	{
		return;
	}

	GtkTreeIter iter;
	GtkTreeModel *model = static_cast<GtkTreeModel *>(user_data);
	if (!servlist_get_iter_from_name (model, name, &iter))
	{
		return;
	}
	char *servname;
	gtk_tree_model_get (model, &iter, 0, &servname, -1);
	glib_string servname_ptr{ servname };
	auto serv = servlist_server_find (selected_net, servname_ptr.get(), nullptr);

	if (serv)
	{
		/* delete empty item */
		if (newval[0] == 0)
		{
			servlist_deleteserver (serv, model);
			return;
		}

		servname_ptr.reset(serv->hostname);
		serv->hostname = servlist_sanitize_hostname (newval);
		gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, serv->hostname, -1);
	}
}

static void
servlist_editcommand_cb (GtkCellRendererText *, gchar *name, gchar *newval, gpointer user_data)
{
	GtkTreeModel *model = static_cast<GtkTreeModel *>(user_data);
	GtkTreeIter iter;
	char *cmd;
	commandentry *entry;

	if (!selected_net)
	{
		return;
	}

	if (!servlist_get_iter_from_name (model, name, &iter))
	{
		return;
	}

	gtk_tree_model_get (model, &iter, 0, &cmd, -1);
	entry = servlist_command_find (selected_net, cmd, nullptr);
	g_free (cmd);

	if (entry)
	{
		/* delete empty item */
		if (newval[0] == 0)
		{
			servlist_deletecommand (entry, model);
			return;
		}

		entry->command = servlist_sanitize_command (newval);
		gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, entry->command.c_str(), -1);
	}
}

static void
servlist_editchannel_cb (GtkCellRendererText *, gchar *name, gchar *newval, gpointer user_data)
{
	GtkTreeModel *model = (GtkTreeModel *)user_data;
	GtkTreeIter iter;
	char *chan;
	char *key;
	favchannel *favchan;

	if (!selected_net)
	{
		return;
	}

	if (!servlist_get_iter_from_name (model, name, &iter))
	{
		return;
	}

	gtk_tree_model_get (model, &iter, 0, &chan, 1, &key, -1);
	favchan = servlist_favchan_find (selected_net, chan, nullptr);
	g_free (chan);

	if (favchan)
	{
		/* delete empty item */
		if (newval[0] == 0)
		{
			servlist_deletechannel (favchan, model);
			return;
		}

		favchan->name = newval;
		gtk_list_store_set (GTK_LIST_STORE (model), &iter, 0, favchan->name.c_str(), -1);
	}
}

static void
servlist_editkey_cb (GtkCellRendererText *, gchar *name, gchar *newval, gpointer user_data)
{
	GtkTreeModel *model = (GtkTreeModel *)user_data;
	GtkTreeIter iter;
	char *chan;
	char *key;
	favchannel *favchan;

	if (!selected_net)
	{
		return;
	}

	if (!servlist_get_iter_from_name (model, name, &iter))
	{
		return;
	}

	gtk_tree_model_get (model, &iter, 0, &chan, 1, &key, -1);
	favchan = servlist_favchan_find (selected_net, chan, nullptr);
	g_free (chan);

	if (favchan)
	{
		if (strlen (newval))	/* check key length, the field can be empty in order to delete the key! */
		{
			favchan->key = newval;
		}
		else					/* if key's empty, make sure we actually remove the key */
		{
			favchan->key = boost::none;
		}

		gtk_list_store_set (GTK_LIST_STORE (model), &iter, 1, favchan->key ? favchan->key->c_str() : nullptr, -1);
	}
}

static gboolean
servlist_edit_tabswitch_cb (GtkNotebook *, gpointer * /*newtab*/, guint newindex, gpointer /*user_data*/)
{
	/* remember the active tab */
	netedit_active_tab = newindex;

	return false;
}

static void
servlist_combo_cb (GtkEntry *entry, gpointer)
{
	if (!selected_net)
		return;

	if (selected_net->encoding)
		free (selected_net->encoding);
	selected_net->encoding = strdup (gtk_entry_get_text (entry));
}

/* Fills up the network's authentication type so that it's guaranteed to be either nullptr or a valid value. */
static void
servlist_logintypecombo_cb (GtkComboBox *cb, gpointer *userdata)
{
	int index;

	if (!selected_net)
	{
		return;
	}

	index = gtk_combo_box_get_active (cb);	/* starts at 0, returns -1 for invalid selections */

	if (index == -1)
		return; /* Invalid */

	/* The selection is valid. It can be 0, which is the default type, but we need to allow
	 * that so that you can revert from other types. servlist_save() will dump 0 anyway.
	 */
	selected_net->logintype = login_types_conf[index];

	if (login_types_conf[index] == LOGIN_CUSTOM)
	{
		gtk_notebook_set_current_page (GTK_NOTEBOOK (userdata), 2);		/* FIXME avoid hardcoding? */
	}
	
	/* EXTERNAL uses a cert, not a pass */
	if (login_types_conf[index] == LOGIN_SASLEXTERNAL)
		gtk_widget_set_sensitive (edit_entry_pass, false);
	else
		gtk_widget_set_sensitive (edit_entry_pass, true);
}

static void
servlist_username_changed_cb (GtkEntry *entry, gpointer userdata)
{
	GtkWidget *connect_btn = GTK_WIDGET (userdata);

	if (gtk_entry_get_text (entry)[0] == 0)
	{
		gtk_entry_set_icon_from_stock (entry, GTK_ENTRY_ICON_SECONDARY, GTK_STOCK_DIALOG_ERROR);
		gtk_entry_set_icon_tooltip_text (entry, GTK_ENTRY_ICON_SECONDARY,
										_("User name cannot be left blank."));
		gtk_widget_set_sensitive (connect_btn, false);
	}
	else
	{
		gtk_entry_set_icon_from_stock (entry, GTK_ENTRY_ICON_SECONDARY, nullptr);
		gtk_widget_set_sensitive (connect_btn, true);
	}
}

static void
servlist_nick_changed_cb (GtkEntry *entry, gpointer userdata)
{
	GtkWidget *connect_btn = GTK_WIDGET (userdata);
	const gchar *nick1 = gtk_entry_get_text (GTK_ENTRY (entry_nick1));
	const gchar *nick2 = gtk_entry_get_text (GTK_ENTRY (entry_nick2));

	if (!rfc_casecmp (nick1, nick2))
	{
		gtk_entry_set_icon_from_stock (entry, GTK_ENTRY_ICON_SECONDARY, GTK_STOCK_DIALOG_ERROR);
		gtk_entry_set_icon_tooltip_text (entry, GTK_ENTRY_ICON_SECONDARY,
										_("You must have two unique nick names."));
		gtk_widget_set_sensitive (connect_btn, false);
	}
	else
	{
		gtk_entry_set_icon_from_stock (GTK_ENTRY(entry_nick1), GTK_ENTRY_ICON_SECONDARY, nullptr);
		gtk_entry_set_icon_from_stock (GTK_ENTRY(entry_nick2), GTK_ENTRY_ICON_SECONDARY, nullptr);
		gtk_widget_set_sensitive (connect_btn, true);
	}
}

static GtkWidget *
servlist_create_charsetcombo (void)
{
	GtkWidget *cb;
	int i;

	cb = gtk_combo_box_text_new_with_entry ();
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (cb), "System default");
	i = 0;
	while (pages[i])
	{
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (cb), (char *)pages[i]);
		i++;
	}

	gtk_entry_set_text (GTK_ENTRY (gtk_bin_get_child (GTK_BIN(cb))), selected_net->encoding ? selected_net->encoding : "System default");
	
	g_signal_connect (G_OBJECT (gtk_bin_get_child (GTK_BIN (cb))), "changed",
							G_CALLBACK (servlist_combo_cb), nullptr);

	return cb;
}

static GtkWidget *
servlist_create_logintypecombo (GtkWidget *data)
{
	GtkWidget *cb;
	int i;

	cb = gtk_combo_box_text_new ();

	i = 0;

	while (login_types[i])
	{
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (cb), (char *)login_types[i]);
		i++;
	}

	gtk_combo_box_set_active (GTK_COMBO_BOX (cb), servlist_get_login_desc_index (selected_net->logintype));

	gtk_widget_set_tooltip_text (cb, _("The way you identify yourself to the server. For custom login methods use connect commands."));
	g_signal_connect (G_OBJECT (GTK_BIN (cb)), "changed", G_CALLBACK (servlist_logintypecombo_cb), data);

	return cb;
}

static void
no_servlist (GtkWidget * igad, gpointer)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (igad)))
		prefs.hex_gui_slist_skip = true;
	else
		prefs.hex_gui_slist_skip = false;
}

static void
fav_servlist (GtkWidget * igad, gpointer)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (igad)))
		prefs.hex_gui_slist_fav = true;
	else
		prefs.hex_gui_slist_fav = false;

	servlist_networks_populate (networks_tree, network_list);
}

static GtkWidget *
bold_label (char *text)
{
	char buf[128];
	GtkWidget *label;

	snprintf (buf, sizeof (buf), "<b>%s</b>", text);
	label = gtk_label_new (buf);
	gtk_label_set_use_markup (GTK_LABEL (label), true);
	gtk_misc_set_alignment (GTK_MISC (label), 0, 0.5);
	gtk_widget_show (label);

	return label;
}

static GtkWidget *
servlist_open_edit (GtkWidget *parent, ircnet *net)
{
	GtkWidget *editwindow;
	GtkWidget *vbox5;
	GtkWidget *table3;
	GtkWidget *label34;
	GtkWidget *label_logintype;
	GtkWidget *comboboxentry_charset;
	GtkWidget *combobox_logintypes;
	GtkWidget *hbox1;
	GtkWidget *scrolledwindow2;
	GtkWidget *scrolledwindow4;
	GtkWidget *scrolledwindow5;
	GtkWidget *treeview_servers;
	GtkWidget *treeview_channels;
	GtkWidget *treeview_commands;
	GtkWidget *vbuttonbox1;
	GtkWidget *buttonadd;
	GtkWidget *buttonremove;
	GtkWidget *buttonedit;
	GtkWidget *hseparator2;
	GtkWidget *hbuttonbox4;
	GtkWidget *button10;
	GtkWidget *check;
	GtkWidget *notebook;
	GtkTreeModel *model;
	GtkListStore *store;
	GtkCellRenderer *renderer;
	char buf[128];

	editwindow = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_container_set_border_width (GTK_CONTAINER (editwindow), 4);
	snprintf (buf, sizeof (buf), _(DISPLAY_NAME": Edit %s"), net->name.c_str());
	gtk_window_set_title (GTK_WINDOW (editwindow), buf);
	gtk_window_set_default_size (GTK_WINDOW (editwindow), netedit_win_width, netedit_win_height);
	gtk_window_set_transient_for (GTK_WINDOW (editwindow), GTK_WINDOW (parent));
	gtk_window_set_modal (GTK_WINDOW (editwindow), true);
	gtk_window_set_type_hint (GTK_WINDOW (editwindow), GDK_WINDOW_TYPE_HINT_DIALOG);
	gtk_window_set_role (GTK_WINDOW (editwindow), "editserv");

	vbox5 = gtk_vbox_new (false, 0);
	gtk_container_add (GTK_CONTAINER (editwindow), vbox5);


	/* Tabs and buttons */
	hbox1 = gtk_hbox_new (false, 0);
	gtk_box_pack_start (GTK_BOX (vbox5), hbox1, true, true, 4);

	scrolledwindow2 = gtk_scrolled_window_new (nullptr, nullptr);
	scrolledwindow4 = gtk_scrolled_window_new (nullptr, nullptr);
	scrolledwindow5 = gtk_scrolled_window_new (nullptr, nullptr);

	notebook = gtk_notebook_new ();
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), scrolledwindow2, gtk_label_new (_("Servers")));
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), scrolledwindow4, gtk_label_new (_("Autojoin channels")));
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), scrolledwindow5, gtk_label_new (_("Connect commands")));
	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_BOTTOM);
	gtk_box_pack_start (GTK_BOX (hbox1), notebook, true, true, SERVLIST_X_PADDING);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow2), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolledwindow2), GTK_SHADOW_IN);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow4), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolledwindow4),	GTK_SHADOW_IN);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow5), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolledwindow5), GTK_SHADOW_IN);
	gtk_widget_set_tooltip_text (scrolledwindow5, _("%n=Nick name\n%p=Password\n%r=Real name\n%u=User name"));


	/* Server Tree */
	store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_BOOLEAN);
	model = GTK_TREE_MODEL (store);

	edit_trees[SERVER_TREE] = treeview_servers = gtk_tree_view_new_with_model (model);
	g_signal_connect (G_OBJECT (treeview_servers), "key_press_event",
							G_CALLBACK (servlist_keypress_cb), notebook);
	g_signal_connect (G_OBJECT (gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview_servers))),
							"changed", G_CALLBACK (servlist_server_row_cb), nullptr);
	g_object_unref (model);
	gtk_container_add (GTK_CONTAINER (scrolledwindow2), treeview_servers);
	gtk_widget_set_size_request (treeview_servers, -1, 80);
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (treeview_servers),
												  false);

	renderer = gtk_cell_renderer_text_new ();
	g_signal_connect (G_OBJECT (renderer), "edited",
							G_CALLBACK (servlist_editserver_cb), model);
	gtk_tree_view_insert_column_with_attributes (
								GTK_TREE_VIEW (treeview_servers), -1,
								0, renderer,
								"text", 0,
								"editable", 1,
								nullptr);

	/* Channel Tree */
	store = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN);
	model = GTK_TREE_MODEL (store);

	edit_trees[CHANNEL_TREE] = treeview_channels = gtk_tree_view_new_with_model (model);
	g_signal_connect (G_OBJECT (treeview_channels), "key_press_event",
							G_CALLBACK (servlist_keypress_cb), notebook);
	g_signal_connect (G_OBJECT (gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview_channels))),
							"changed", G_CALLBACK (servlist_channel_row_cb), nullptr);
	g_object_unref (model);
	gtk_container_add (GTK_CONTAINER (scrolledwindow4), treeview_channels);
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (treeview_channels), true);

	renderer = gtk_cell_renderer_text_new ();
	g_signal_connect (G_OBJECT (renderer), "edited",
							G_CALLBACK (servlist_editchannel_cb), model);
	gtk_tree_view_insert_column_with_attributes (
								GTK_TREE_VIEW (treeview_channels), -1,
								_("Channel"), renderer,
								"text", 0,
								"editable", 2,
								nullptr);

	renderer = gtk_cell_renderer_text_new ();
	g_signal_connect (G_OBJECT (renderer), "edited",
							G_CALLBACK (servlist_editkey_cb), model);
	gtk_tree_view_insert_column_with_attributes (
								GTK_TREE_VIEW (treeview_channels), -1,
								_("Key (Password)"), renderer,
								"text", 1,
								"editable", 2,
								nullptr);

	gtk_tree_view_column_set_expand (gtk_tree_view_get_column (GTK_TREE_VIEW (treeview_channels), 0), true);
	gtk_tree_view_column_set_expand (gtk_tree_view_get_column (GTK_TREE_VIEW (treeview_channels), 1), true);


	/* Command Tree */
	store = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_BOOLEAN);
	model = GTK_TREE_MODEL (store);

	edit_trees[CMD_TREE] = treeview_commands = gtk_tree_view_new_with_model (model);
	g_signal_connect (G_OBJECT (treeview_commands), "key_press_event",
							G_CALLBACK (servlist_keypress_cb), notebook);
	g_signal_connect (G_OBJECT (gtk_tree_view_get_selection (GTK_TREE_VIEW (treeview_commands))),
							"changed", G_CALLBACK (servlist_command_row_cb), nullptr);
	g_object_unref (model);
	gtk_container_add (GTK_CONTAINER (scrolledwindow5), treeview_commands);
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (treeview_commands),
												  false);

	renderer = gtk_cell_renderer_text_new ();
	g_signal_connect (G_OBJECT (renderer), "edited",
							G_CALLBACK (servlist_editcommand_cb), model);
	gtk_tree_view_insert_column_with_attributes (
								GTK_TREE_VIEW (treeview_commands), -1,
								0, renderer,
								"text", 0,
								"editable", 1,
								nullptr);


	/* Button Box */
	vbuttonbox1 = gtk_vbutton_box_new ();
	gtk_box_set_spacing (GTK_BOX (vbuttonbox1), 3);
	gtk_button_box_set_layout (GTK_BUTTON_BOX (vbuttonbox1), GTK_BUTTONBOX_START);
	gtk_box_pack_start (GTK_BOX (hbox1), vbuttonbox1, false, false, 3);

	buttonadd = gtk_button_new_from_stock ("gtk-add");
	g_signal_connect (G_OBJECT (buttonadd), "clicked",
							G_CALLBACK (servlist_addbutton_cb), notebook);
	gtk_container_add (GTK_CONTAINER (vbuttonbox1), buttonadd);
	gtk_widget_set_can_default (buttonadd, true);

	buttonremove = gtk_button_new_from_stock ("gtk-remove");
	g_signal_connect (G_OBJECT (buttonremove), "clicked",
							G_CALLBACK (servlist_deletebutton_cb), notebook);
	gtk_container_add (GTK_CONTAINER (vbuttonbox1), buttonremove);
	gtk_widget_set_can_default (buttonremove, true);

	buttonedit = gtk_button_new_with_mnemonic (_("_Edit"));
	g_signal_connect (G_OBJECT (buttonedit), "clicked",
							G_CALLBACK (servlist_editbutton_cb), notebook);
	gtk_container_add (GTK_CONTAINER (vbuttonbox1), buttonedit);
	gtk_widget_set_can_default (buttonedit, true);


	/* Checkboxes and entries */
	table3 = gtk_table_new (13, 2, false);
	gtk_box_pack_start (GTK_BOX (vbox5), table3, false, false, 0);
	gtk_table_set_row_spacings (GTK_TABLE (table3), 2);
	gtk_table_set_col_spacings (GTK_TABLE (table3), 8);

	check = servlist_create_check (0, !(net->flags & FLAG_CYCLE), table3, 0, 0, _("Connect to selected server only"));
	gtk_widget_set_tooltip_text (check, _("Don't cycle through all the servers when the connection fails."));
	servlist_create_check (3, net->flags & FLAG_AUTO_CONNECT, table3, 1, 0, _("Connect to this network automatically"));
	servlist_create_check (4, !(net->flags & FLAG_USE_PROXY), table3, 2, 0, _("Bypass proxy server"));
	check = servlist_create_check (2, net->flags & FLAG_USE_SSL, table3, 3, 0, _("Use SSL for all the servers on this network"));
#ifndef USE_OPENSSL
	gtk_widget_set_sensitive (check, false);
#endif
	check = servlist_create_check (5, net->flags & FLAG_ALLOW_INVALID, table3, 4, 0, _("Accept invalid SSL certificates"));
#ifndef USE_OPENSSL
	gtk_widget_set_sensitive (check, false);
#endif
	servlist_create_check (1, net->flags & FLAG_USE_GLOBAL, table3, 5, 0, _("Use global user information"));

	edit_entry_nick = servlist_create_entry (table3, _("_Nick name:"), 6, net->nick ? net->nick->c_str() : nullptr, &edit_label_nick, 0);
	edit_entry_nick2 = servlist_create_entry (table3, _("Second choice:"), 7, net->nick2 ? net->nick2->c_str() : nullptr, &edit_label_nick2, 0);
	edit_entry_real = servlist_create_entry (table3, _("Rea_l name:"), 8, net->real, &edit_label_real, 0);
	edit_entry_user = servlist_create_entry (table3, _("_User name:"), 9, net->user, &edit_label_user, 0);

	label_logintype = gtk_label_new (_("Login method:"));
	gtk_table_attach (GTK_TABLE (table3), label_logintype, 0, 1, 10, 11, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), SERVLIST_X_PADDING, SERVLIST_Y_PADDING);
	gtk_misc_set_alignment (GTK_MISC (label_logintype), 0, 0.5);
	combobox_logintypes = servlist_create_logintypecombo (notebook);
	gtk_table_attach (GTK_TABLE (table3), combobox_logintypes, 1, 2, 10, 11, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (GTK_FILL), 4, 2);

	edit_entry_pass = servlist_create_entry (table3, _("Password:"), 11, net->pass, 0, _("Password used for login. If in doubt, leave blank."));
	gtk_entry_set_visibility (GTK_ENTRY (edit_entry_pass), false);
	if (selected_net && selected_net->logintype == LOGIN_SASLEXTERNAL)
		gtk_widget_set_sensitive (edit_entry_pass, false);

	label34 = gtk_label_new (_("Character set:"));
	gtk_table_attach (GTK_TABLE (table3), label34, 0, 1, 12, 13, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), SERVLIST_X_PADDING, SERVLIST_Y_PADDING);
	gtk_misc_set_alignment (GTK_MISC (label34), 0, 0.5);
	comboboxentry_charset = servlist_create_charsetcombo ();
	gtk_table_attach (GTK_TABLE (table3), comboboxentry_charset, 1, 2, 12, 13, (GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (GTK_FILL), 4, 2);


	/* Rule and Close button */
	hseparator2 = gtk_hseparator_new ();
	gtk_box_pack_start (GTK_BOX (vbox5), hseparator2, false, false, 8);

	hbuttonbox4 = gtk_hbutton_box_new ();
	gtk_box_pack_start (GTK_BOX (vbox5), hbuttonbox4, false, false, 0);
	gtk_button_box_set_layout (GTK_BUTTON_BOX (hbuttonbox4), GTK_BUTTONBOX_END);

	button10 = gtk_button_new_from_stock ("gtk-close");
	g_signal_connect (G_OBJECT (button10), "clicked",
							G_CALLBACK (servlist_edit_close_cb), 0);
	gtk_container_add (GTK_CONTAINER (hbuttonbox4), button10);
	gtk_widget_set_can_default (button10, true);

	if (net->flags & FLAG_USE_GLOBAL)
	{
		servlist_toggle_global_user (false);
	}

	gtk_widget_grab_focus (button10);
	gtk_widget_grab_default (button10);

	gtk_widget_show_all (editwindow);

	/* We can't set the active tab without child elements being shown, so this must be *after* gtk_widget_show()s! */
	gtk_notebook_set_current_page (GTK_NOTEBOOK (notebook), netedit_active_tab);

	/* We need to connect this *after* setting the active tab so that the value doesn't get overriden. */
	g_signal_connect (G_OBJECT (notebook), "switch-page", G_CALLBACK (servlist_edit_tabswitch_cb), notebook);

	return editwindow;
}

static GtkWidget *
servlist_open_networks (void)
{
	GtkWidget *servlist;
	GtkWidget *vbox1;
	GtkWidget *label2;
	GtkWidget *table1;
	GtkWidget *label3;
	GtkWidget *label4;
	GtkWidget *label5;
	GtkWidget *label6;
	/* GtkWidget *label7; */
	GtkWidget *entry1;
	GtkWidget *entry2;
	GtkWidget *entry3;
	GtkWidget *entry4;
	/* GtkWidget *entry5; */
	GtkWidget *vbox2;
	GtkWidget *label1;
	GtkWidget *table4;
	GtkWidget *scrolledwindow3;
	GtkWidget *treeview_networks;
	GtkWidget *checkbutton_skip;
	GtkWidget *checkbutton_fav;
	GtkWidget *hbox;
	GtkWidget *vbuttonbox2;
	GtkWidget *button_add;
	GtkWidget *button_remove;
	GtkWidget *button_edit;
	GtkWidget *button_sort;
	GtkWidget *hseparator1;
	GtkWidget *hbuttonbox1;
	GtkWidget *button_connect;
	GtkWidget *button_close;
	GtkTreeModel *model;
	GtkListStore *store;
	GtkCellRenderer *renderer;

	servlist = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_container_set_border_width (GTK_CONTAINER (servlist), 4);
	gtk_window_set_title (GTK_WINDOW (servlist), _(DISPLAY_NAME": Network List"));
	gtk_window_set_default_size (GTK_WINDOW (servlist), netlist_win_width, netlist_win_height);
	gtk_window_set_role (GTK_WINDOW (servlist), "servlist");
	gtk_window_set_type_hint (GTK_WINDOW (servlist), GDK_WINDOW_TYPE_HINT_DIALOG);
	if (current_sess)
		gtk_window_set_transient_for (GTK_WINDOW (servlist), GTK_WINDOW (current_sess->gui->window));

	vbox1 = gtk_vbox_new (false, 0);
	gtk_widget_show (vbox1);
	gtk_container_add (GTK_CONTAINER (servlist), vbox1);

	label2 = bold_label (_("User Information"));
	gtk_box_pack_start (GTK_BOX (vbox1), label2, false, false, 0);

	table1 = gtk_table_new (5, 2, false);
	gtk_widget_show (table1);
	gtk_box_pack_start (GTK_BOX (vbox1), table1, false, false, 0);
	gtk_container_set_border_width (GTK_CONTAINER (table1), 8);
	gtk_table_set_row_spacings (GTK_TABLE (table1), 2);
	gtk_table_set_col_spacings (GTK_TABLE (table1), 4);

	label3 = gtk_label_new_with_mnemonic (_("_Nick name:"));
	gtk_widget_show (label3);
	gtk_table_attach (GTK_TABLE (table1), label3, 0, 1, 0, 1,
							(GtkAttachOptions) (GTK_FILL),
							(GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment (GTK_MISC (label3), 0, 0.5);

	label4 = gtk_label_new (_("Second choice:"));
	gtk_widget_show (label4);
	gtk_table_attach (GTK_TABLE (table1), label4, 0, 1, 1, 2,
							(GtkAttachOptions) (GTK_FILL),
							(GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment (GTK_MISC (label4), 0, 0.5);

	label5 = gtk_label_new (_("Third choice:"));
	gtk_widget_show (label5);
	gtk_table_attach (GTK_TABLE (table1), label5, 0, 1, 2, 3,
							(GtkAttachOptions) (GTK_FILL),
							(GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment (GTK_MISC (label5), 0, 0.5);

	label6 = gtk_label_new_with_mnemonic (_("_User name:"));
	gtk_widget_show (label6);
	gtk_table_attach (GTK_TABLE (table1), label6, 0, 1, 3, 4,
							(GtkAttachOptions) (GTK_FILL),
							(GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment (GTK_MISC (label6), 0, 0.5);

	/* label7 = gtk_label_new_with_mnemonic (_("Rea_l name:"));
	gtk_widget_show (label7);
	gtk_table_attach (GTK_TABLE (table1), label7, 0, 1, 4, 5,
							(GtkAttachOptions) (GTK_FILL),
							(GtkAttachOptions) (0), 0, 0);
	gtk_misc_set_alignment (GTK_MISC (label7), 0, 0.5);*/

	entry_nick1 = entry1 = gtk_entry_new ();
	gtk_entry_set_text (GTK_ENTRY (entry1), prefs.hex_irc_nick1);
	gtk_widget_show (entry1);
	gtk_table_attach (GTK_TABLE (table1), entry1, 1, 2, 0, 1,
							(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
							(GtkAttachOptions) (0), 0, 0);

	entry_nick2 = entry2 = gtk_entry_new ();
	gtk_entry_set_text (GTK_ENTRY (entry2), prefs.hex_irc_nick2);
	gtk_widget_show (entry2);
	gtk_table_attach (GTK_TABLE (table1), entry2, 1, 2, 1, 2,
							(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
							(GtkAttachOptions) (0), 0, 0);

	entry_nick3 = entry3 = gtk_entry_new ();
	gtk_entry_set_text (GTK_ENTRY (entry3), prefs.hex_irc_nick3);
	gtk_widget_show (entry3);
	gtk_table_attach (GTK_TABLE (table1), entry3, 1, 2, 2, 3,
							(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
							(GtkAttachOptions) (0), 0, 0);

	entry_guser = entry4 = gtk_entry_new ();
	gtk_entry_set_text (GTK_ENTRY (entry4), prefs.hex_irc_user_name);
	gtk_widget_show (entry4);
	gtk_table_attach (GTK_TABLE (table1), entry4, 1, 2, 3, 4,
							(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
							(GtkAttachOptions) (0), 0, 0);

	/* entry_greal = entry5 = gtk_entry_new ();
	gtk_entry_set_text (GTK_ENTRY (entry5), prefs.hex_irc_real_name);
	gtk_widget_show (entry5);
	gtk_table_attach (GTK_TABLE (table1), entry5, 1, 2, 4, 5,
							(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
							(GtkAttachOptions) (0), 0, 0); */

	vbox2 = gtk_vbox_new (false, 0);
	gtk_widget_show (vbox2);
	gtk_box_pack_start (GTK_BOX (vbox1), vbox2, true, true, 0);

	label1 = bold_label (_("Networks"));
	gtk_box_pack_start (GTK_BOX (vbox2), label1, false, false, 0);

	table4 = gtk_table_new (2, 2, false);
	gtk_widget_show (table4);
	gtk_box_pack_start (GTK_BOX (vbox2), table4, true, true, 0);
	gtk_container_set_border_width (GTK_CONTAINER (table4), 8);
	gtk_table_set_row_spacings (GTK_TABLE (table4), 2);
	gtk_table_set_col_spacings (GTK_TABLE (table4), 3);

	scrolledwindow3 = gtk_scrolled_window_new (nullptr, nullptr);
	gtk_widget_show (scrolledwindow3);
	gtk_table_attach (GTK_TABLE (table4), scrolledwindow3, 0, 1, 0, 1,
							(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
							(GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 0, 0);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow3),
											  GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolledwindow3),
													 GTK_SHADOW_IN);

	store = gtk_list_store_new (3, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_INT);
	model = GTK_TREE_MODEL (store);

	networks_tree = treeview_networks = gtk_tree_view_new_with_model (model);
	g_object_unref (model);
	gtk_widget_show (treeview_networks);
	gtk_container_add (GTK_CONTAINER (scrolledwindow3), treeview_networks);
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (treeview_networks),
												  false);

	renderer = gtk_cell_renderer_text_new ();
	g_signal_connect (G_OBJECT (renderer), "edited",
							G_CALLBACK (servlist_celledit_cb), model);
	gtk_tree_view_insert_column_with_attributes (
								GTK_TREE_VIEW (treeview_networks), -1,
								0, renderer,
								"text", 0,
								"editable", 1,
								"weight", 2,
								nullptr);

	hbox = gtk_hbox_new (0, false);
	gtk_table_attach (GTK_TABLE (table4), hbox, 0, 2, 1, 2,
							(GtkAttachOptions) (GTK_FILL),
							(GtkAttachOptions) (0), 0, 0);
	gtk_widget_show (hbox);

	checkbutton_skip =
		gtk_check_button_new_with_mnemonic (_("Skip network list on startup"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (checkbutton_skip),
											prefs.hex_gui_slist_skip);
	gtk_container_add (GTK_CONTAINER (hbox), checkbutton_skip);
	g_signal_connect (G_OBJECT (checkbutton_skip), "toggled",
							G_CALLBACK (no_servlist), 0);
	gtk_widget_show (checkbutton_skip);

	checkbutton_fav =
		gtk_check_button_new_with_mnemonic (_("Show favorites only"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (checkbutton_fav),
											prefs.hex_gui_slist_fav);
	gtk_container_add (GTK_CONTAINER (hbox), checkbutton_fav);
	g_signal_connect (G_OBJECT (checkbutton_fav), "toggled",
							G_CALLBACK (fav_servlist), 0);
	gtk_widget_show (checkbutton_fav);

	vbuttonbox2 = gtk_vbutton_box_new ();
	gtk_box_set_spacing (GTK_BOX (vbuttonbox2), 3);
	gtk_button_box_set_layout (GTK_BUTTON_BOX (vbuttonbox2), GTK_BUTTONBOX_START);
	gtk_widget_show (vbuttonbox2);
	gtk_table_attach (GTK_TABLE (table4), vbuttonbox2, 1, 2, 0, 1,
							(GtkAttachOptions) (GTK_FILL),
							(GtkAttachOptions) (GTK_FILL), 0, 0);

	button_add = gtk_button_new_from_stock ("gtk-add");
	g_signal_connect (G_OBJECT (button_add), "clicked",
							G_CALLBACK (servlist_addnet_cb), networks_tree);
	gtk_widget_show (button_add);
	gtk_container_add (GTK_CONTAINER (vbuttonbox2), button_add);
	gtk_widget_set_can_default (button_add, true);

	button_remove = gtk_button_new_from_stock ("gtk-remove");
	g_signal_connect (G_OBJECT (button_remove), "clicked",
							G_CALLBACK (servlist_deletenet_cb), 0);
	gtk_widget_show (button_remove);
	gtk_container_add (GTK_CONTAINER (vbuttonbox2), button_remove);
	gtk_widget_set_can_default (button_remove, true);

	button_edit = gtk_button_new_with_mnemonic (_("_Edit..."));
	g_signal_connect (G_OBJECT (button_edit), "clicked",
							G_CALLBACK (servlist_edit_cb), 0);
	gtk_widget_show (button_edit);
	gtk_container_add (GTK_CONTAINER (vbuttonbox2), button_edit);
	gtk_widget_set_can_default (button_edit, true);

	button_sort = gtk_button_new_with_mnemonic (_("_Sort"));
	gtk_widget_set_tooltip_text (button_sort, _("Sorts the network list in alphabetical order. "
				"Use SHIFT-UP and SHIFT-DOWN keys to move a row."));
	g_signal_connect (G_OBJECT (button_sort), "clicked",
							G_CALLBACK (servlist_sort), 0);
	gtk_widget_show (button_sort);
	gtk_container_add (GTK_CONTAINER (vbuttonbox2), button_sort);
	gtk_widget_set_can_default (button_sort, true);

	button_sort = gtk_button_new_with_mnemonic (_("_Favor"));
	gtk_widget_set_tooltip_text (button_sort, _("Mark or unmark this network as a favorite."));
	g_signal_connect (G_OBJECT (button_sort), "clicked",
							G_CALLBACK (servlist_favor), 0);
	gtk_widget_show (button_sort);
	gtk_container_add (GTK_CONTAINER (vbuttonbox2), button_sort);
	gtk_widget_set_can_default (button_sort, true);

	hseparator1 = gtk_hseparator_new ();
	gtk_widget_show (hseparator1);
	gtk_box_pack_start (GTK_BOX (vbox1), hseparator1, false, true, 4);

	hbuttonbox1 = gtk_hbutton_box_new ();
	gtk_widget_show (hbuttonbox1);
	gtk_box_pack_start (GTK_BOX (vbox1), hbuttonbox1, false, true, 0);
	gtk_container_set_border_width (GTK_CONTAINER (hbuttonbox1), 8);

	button_close = gtk_button_new_from_stock ("gtk-close");
	gtk_widget_show (button_close);
	g_signal_connect (G_OBJECT (button_close), "clicked",
							G_CALLBACK (servlist_close_cb), 0);
	gtk_container_add (GTK_CONTAINER (hbuttonbox1), button_close);
	gtk_widget_set_can_default (button_close, true);

	button_connect = gtkutil_button (hbuttonbox1, GTK_STOCK_CONNECT, nullptr,
		G_CALLBACK(servlist_connect_cb), nullptr, _("C_onnect"));
	gtk_widget_set_can_default (button_connect, true);

	g_signal_connect (G_OBJECT (entry_guser), "changed", 
					G_CALLBACK(servlist_username_changed_cb), button_connect);
	g_signal_connect (G_OBJECT (entry_nick1), "changed",
					G_CALLBACK(servlist_nick_changed_cb), button_connect);
	g_signal_connect (G_OBJECT (entry_nick2), "changed",
					G_CALLBACK(servlist_nick_changed_cb), button_connect);

	gtk_label_set_mnemonic_widget (GTK_LABEL (label3), entry1);
	gtk_label_set_mnemonic_widget (GTK_LABEL (label6), entry4);
	/* gtk_label_set_mnemonic_widget (GTK_LABEL (label7), entry5); */

	gtk_widget_grab_focus (networks_tree);
	gtk_widget_grab_default (button_close);
	return servlist;
}

void
fe_serverlist_open (session *sess)
{
	if (serverlist_win)
	{
		gtk_window_present (GTK_WINDOW (serverlist_win));
		return;
	}

	servlist_sess = sess;

	serverlist_win = servlist_open_networks ();
	gtkutil_set_icon (serverlist_win);

	servlist_networks_populate (networks_tree, network_list);

	g_signal_connect (G_OBJECT (serverlist_win), "delete_event",
							G_CALLBACK (servlist_delete_cb), 0);
	g_signal_connect (G_OBJECT (serverlist_win), "configure_event",
							G_CALLBACK (servlist_configure_cb), 0);
	g_signal_connect (G_OBJECT (gtk_tree_view_get_selection (GTK_TREE_VIEW (networks_tree))),
							"changed", G_CALLBACK (servlist_network_row_cb), nullptr);
	g_signal_connect (G_OBJECT (networks_tree), "key_press_event",
							G_CALLBACK (servlist_net_keypress_cb), networks_tree);

	gtk_widget_show (serverlist_win);
}
