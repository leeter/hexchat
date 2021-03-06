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

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <ctime>
#include <boost/utility/string_ref.hpp>

#include "fe-gtk.hpp"

#include "../common/hexchat.hpp"
#include "../common/notify.hpp"
#include "../common/cfgfiles.hpp"
#include "../common/fe.hpp"
#include "../common/server.hpp"
#include "../common/util.hpp"
#include "../common/userlist.hpp"
#include "../common/outbound.hpp"
#include "gtkutil.hpp"
#include "maingui.hpp"
#include "palette.hpp"
#include "notifygui.hpp"
#include "gtk_helpers.hpp"

namespace{
/* model for the notify treeview */
enum
{
	USER_COLUMN,
	STATUS_COLUMN,
	SERVER_COLUMN,
	SEEN_COLUMN,
	COLOUR_COLUMN,
	NPS_COLUMN, 	/* struct notify_per_server * */
	N_COLUMNS
};


static GtkWidget *notify_window = 0;
static GtkWidget *notify_button_opendialog;
static GtkWidget *notify_button_remove;


static void
notify_closegui (void)
{
	notify_window = 0;
	notify_save ();
}

/* Need this to be able to set the foreground colour property of a row
 * from a GdkColor * in the model  -Vince
 */
static void
notify_treecell_property_mapper (GtkTreeViewColumn *col, GtkCellRenderer *cell,
								 GtkTreeModel *model, GtkTreeIter *iter,
								 gpointer data)
{
	gchar *text;
	GdkColor *colour;
	int model_column = GPOINTER_TO_INT (data);

	gtk_tree_model_get (GTK_TREE_MODEL (model), iter, 
						COLOUR_COLUMN, &colour,
						model_column, &text, -1);
	g_object_set (G_OBJECT (cell), "text", text, nullptr);
	g_object_set (G_OBJECT (cell), "foreground-gdk", colour, nullptr);
	g_free (text);
}

static void
notify_row_cb (GtkTreeSelection *sel, GtkTreeView *view)
{
	GtkTreeIter iter;
	struct notify_per_server *servnot;

	if (gtkutil_treeview_get_selected (view, &iter, NPS_COLUMN, &servnot, -1))
	{
		gtk_widget_set_sensitive (notify_button_opendialog, servnot ? servnot->ison : 0);
		gtk_widget_set_sensitive (notify_button_remove, true);
		return;
	}

	gtk_widget_set_sensitive (notify_button_opendialog, false);
	gtk_widget_set_sensitive (notify_button_remove, false);
}

static GtkWidget *
notify_treeview_new (GtkWidget *box)
{
	auto store = gtk_list_store_new (N_COLUMNS,
								G_TYPE_STRING,
								G_TYPE_STRING,
								G_TYPE_STRING,
								G_TYPE_STRING,
								G_TYPE_POINTER,	/* can't specify colour! */
										 G_TYPE_POINTER
							   );
	g_return_val_if_fail (store != nullptr, nullptr);

	auto view = gtkutil_treeview_new (box, GTK_TREE_MODEL (store),
								 notify_treecell_property_mapper,
								 USER_COLUMN, _("Name"),
								 STATUS_COLUMN, _("Status"),
								 SERVER_COLUMN, _("Network"),
								 SEEN_COLUMN, _("Last Seen"), -1);
	gtk_tree_view_column_set_expand (gtk_tree_view_get_column (GTK_TREE_VIEW (view), 0), true);

	GtkTreeViewColumn *col;
	for (int col_id=0; (col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), col_id));
		 col_id++)
			gtk_tree_view_column_set_alignment (col, 0.5);

	g_signal_connect (G_OBJECT (gtk_tree_view_get_selection (GTK_TREE_VIEW (view))),
							"changed", G_CALLBACK (notify_row_cb), view);

	gtk_widget_show (view);
	return view;
}

static void
notify_add_clicked(GtkWidget * igad)
{
	::hexchat::fe::notify::fe_notify_ask("", nullptr);
}

static void
notify_opendialog_clicked(GtkWidget * igad)
{
	GtkTreeIter iter;
	struct notify_per_server *servnot;

	auto view = static_cast<GtkTreeView*>(g_object_get_data(G_OBJECT(notify_window), "view"));
	if (gtkutil_treeview_get_selected(view, &iter, NPS_COLUMN, &servnot, -1))
	{
		if (servnot)
			open_query(*servnot->server, servnot->notify->name.c_str(), true);
	}
}

static void
notify_remove_clicked(GtkWidget * igad)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	bool found = false;
	char *name;

	auto view = static_cast<GtkTreeView*>(g_object_get_data(G_OBJECT(notify_window), "view"));
	if (gtkutil_treeview_get_selected(view, &iter, USER_COLUMN, &name, -1))
	{
		model = gtk_tree_view_get_model(view);
		found = (*name != 0);
		GtkTreePathPtr path;
		while (!found)	/* the real nick is some previous node */
		{
			g_free(name); /* it's useless to us */
			if (!path)
				path.reset(gtk_tree_model_get_path(model, &iter));
			if (!gtk_tree_path_prev(path.get()))	/* arrgh! no previous node! */
			{
				g_warning("notify list state is invalid\n");
				break;
			}
			if (!gtk_tree_model_get_iter(model, &iter, path.get()))
				break;
			gtk_tree_model_get(model, &iter, USER_COLUMN, &name, -1);
			found = (*name != 0);
		}
		if (!found)
			return;

		/* ok, now we can remove it */
		gtk_list_store_remove(GTK_LIST_STORE(model), &iter);
		notify_deluser(name);
		g_free(name);
	}
}

static void
notifygui_add_cb(GtkDialog *dialog, gint response, gpointer entry)
{
	auto text = gtk_entry_get_text(GTK_ENTRY(entry));
	if (text[0] && response == GTK_RESPONSE_ACCEPT)
	{
		auto networks = gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(entry), "net")));
		if (g_ascii_strcasecmp(networks, "ALL") == 0 || networks[0] == 0)
			notify_adduser(text, nullptr);
		else
			notify_adduser(text, networks);
	}

	gtk_widget_destroy(GTK_WIDGET(dialog));
}

static void
notifygui_add_enter(GtkWidget *entry, GtkWidget *dialog)
{
	gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
}

}

namespace hexchat{
namespace fe{
namespace notify{
	void
		fe_notify_ask(char *nick, char *networks)
	{
		const char *msg = _("Enter nickname to add:");
		char buf[256];

		auto dialog = gtk_dialog_new_with_buttons(msg, nullptr, GtkDialogFlags(),
			GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
			GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
			nullptr);
		if (parent_window)
			gtk_window_set_transient_for(GTK_WINDOW(dialog), GTK_WINDOW(parent_window));
		gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_MOUSE);

		auto table = gtk_table_new(2, 3, false);
		gtk_container_set_border_width(GTK_CONTAINER(table), 12);
		gtk_table_set_row_spacings(GTK_TABLE(table), 3);
		gtk_table_set_col_spacings(GTK_TABLE(table), 8);
		gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), table);

		auto label = gtk_label_new(msg);
		gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 0, 1);

		auto entry = gtk_entry_new();
		gtk_entry_set_text(GTK_ENTRY(entry), nick);
		g_signal_connect(G_OBJECT(entry), "activate",
			G_CALLBACK(notifygui_add_enter), dialog);
		gtk_table_attach_defaults(GTK_TABLE(table), entry, 1, 2, 0, 1);

		g_signal_connect(G_OBJECT(dialog), "response",
			G_CALLBACK(notifygui_add_cb), entry);

		label = gtk_label_new(_("Notify on these networks:"));
		gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, 2, 3);

		auto wid = gtk_entry_new();
		g_object_set_data(G_OBJECT(entry), "net", wid);
		g_signal_connect(G_OBJECT(wid), "activate",
			G_CALLBACK(notifygui_add_enter), dialog);
		gtk_entry_set_text(GTK_ENTRY(wid), networks ? networks : "ALL");
		gtk_table_attach_defaults(GTK_TABLE(table), wid, 1, 2, 2, 3);

		label = gtk_label_new(nullptr);
		snprintf(buf, sizeof(buf), "<i><span size=\"smaller\">%s</span></i>", _("Comma separated list of networks is accepted."));
		gtk_label_set_markup(GTK_LABEL(label), buf);
		gtk_table_attach_defaults(GTK_TABLE(table), label, 1, 2, 3, 4);

		gtk_widget_show_all(dialog);
	}
} // ::hexchat::fe::notify
} // ::hexchat::fe
namespace gui{
namespace notify{

void
notify_gui_update (void)
{
	if (!notify_window)
		return;

	auto view = static_cast<GtkTreeView*>(g_object_get_data (G_OBJECT (notify_window), "view"));
	auto store = GTK_LIST_STORE (gtk_tree_view_get_model (view));

	GtkTreeIter iter;
	/* true if we don't need to append a new tree row */
	auto valid = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store), &iter);

	GSList *list = notify_list;
	while (list)
	{
		auto notify = (struct notify *) list->data;
		auto name = notify->name.c_str();
		const char* status = _("Offline");
		boost::string_ref server{ "", 0 };

		bool online = false;
		std::time_t lastseen = 0;
		/* First see if they're online on any servers */
		auto slist = notify->server_list;
		while (slist)
		{
			auto servnot = (struct notify_per_server *) slist->data;
			if (servnot->ison)
				online = true;
			if (servnot->lastseen > lastseen)
				lastseen = servnot->lastseen;
			slist = slist->next;
		}
		const gchar *seen = nullptr;
		char agobuf[128];
		if (!online)				  /* Offline on all servers */
		{
			if (!lastseen)
				seen = _("Never");
			else
			{
				int lastseenminutes = (int)(time (0) - lastseen) / 60;
				if (lastseenminutes < 60) 
					snprintf (agobuf, sizeof (agobuf), _("%d minutes ago"), lastseenminutes);
				else if (lastseenminutes < 120)
					snprintf (agobuf, sizeof (agobuf), _("An hour ago"));
				else
					snprintf (agobuf, sizeof (agobuf), _("%d hours ago"), lastseenminutes / 60);
				seen = agobuf;
			}
			if (!valid)	/* create new tree row if required */
				gtk_list_store_append (store, &iter);
			gtk_list_store_set (store, &iter, 0, name, 1, status,
								2, server.data(), 3, seen, 4, &colors[4], 5, nullptr, -1);
			if (valid)
				valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &iter);

		} else
		{
			/* Online - add one line per server */
			int servcount = 0;
			slist = notify->server_list;
			status = _("Online");
			while (slist)
			{
				auto servnot = (struct notify_per_server *) slist->data;
				if (servnot->ison)
				{
					if (servcount > 0)
						name = "";
					server = servnot->server->get_network(true);

					snprintf (agobuf, sizeof (agobuf), _("%d minutes ago"), (int)(time (0) - lastseen) / 60);
					seen = agobuf;

					if (!valid)	/* create new tree row if required */
						gtk_list_store_append (store, &iter);
					gtk_list_store_set (store, &iter, 0, name, 1, status,
										2, server.data(), 3, seen, 4, &colors[3], 5, servnot, -1);
					if (valid)
						valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &iter);

					servcount++;
				}
				slist = slist->next;
			}
		}
		
		list = list->next;
	}

	while (valid)
	{
		GtkTreeIter old = iter;
		/* get next iter now because removing invalidates old one */
		valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (store),
									  &iter);
		gtk_list_store_remove (store, &old);
	}

	notify_row_cb (gtk_tree_view_get_selection (view), view);
}

void
notify_opengui (void)
{
	if (notify_window)
	{
		mg_bring_tofront (notify_window);
		return;
	}

	GtkWidget *vbox = nullptr;
	notify_window =
		mg_create_generic_tab ("Notify", _(DISPLAY_NAME": Friends List"), false, true,
							   notify_closegui, nullptr, 400, 250, &vbox, 0);
	gtkutil_destroy_on_esc (notify_window);

	auto view = notify_treeview_new (vbox);
	g_object_set_data (G_OBJECT (notify_window), "view", view);
  
	auto bbox = gtk_hbutton_box_new ();
	gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_SPREAD);
	gtk_container_set_border_width (GTK_CONTAINER (bbox), 5);
	gtk_box_pack_end (GTK_BOX (vbox), bbox, 0, 0, 0);
	gtk_widget_show (bbox);

	gtkutil_button(bbox, GTK_STOCK_NEW, 0, G_CALLBACK(notify_add_clicked), 0,
					_("Add..."));

	notify_button_remove =
		gtkutil_button(bbox, GTK_STOCK_DELETE, 0, G_CALLBACK(notify_remove_clicked), 0,
					_("Remove"));

	notify_button_opendialog =
		gtkutil_button(bbox, nullptr, 0, G_CALLBACK(notify_opendialog_clicked), 0,
					_("Open Dialog"));

	gtk_widget_set_sensitive (notify_button_opendialog, false);
	gtk_widget_set_sensitive (notify_button_remove, false);

	notify_gui_update ();

	gtk_widget_show (notify_window);
}

} // ::hexchat::gui::notify
} // ::hexchat::gui
} // ::hexchat
