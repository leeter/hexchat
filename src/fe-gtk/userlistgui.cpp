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

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <utility>

#include "fe-gtk.hpp"

#include <gdk/gdkkeysyms.h>

#include "../common/hexchat.hpp"
#include "../common/util.hpp"
#include "../common/userlist.hpp"
#include "../common/modes.hpp"
#include "../common/text.hpp"
#include "../common/notify.hpp"
#include "../common/hexchatc.hpp"
#include "../common/server.hpp"
#include "../common/fe.hpp"
#include "../common/session.hpp"
#include "gtkutil.hpp"
#include "palette.hpp"
#include "maingui.hpp"
#include "menu.hpp"
#include "pixmaps.hpp"
#include "userlistgui.hpp"
#include "fkeys.hpp"
#include "gtk_helpers.hpp"

enum
{
	COL_PIX=0,		// GdkPixbuf *
	COL_NICK=1,		// char *
	COL_HOST=2,		// char *
	COL_USER=3,		// struct User *
	COL_GDKCOLOR=4	// GdkColor *
};


GdkPixbuf *
get_user_icon (server *serv, struct User *user)
{
	int level;

	if (!user)
		return NULL;

	/* these ones are hardcoded */
	switch (user->prefix[0])
	{
		case 0: return NULL;
		case '+': return pix_ulist_voice;
		case '%': return pix_ulist_halfop;
		case '@': return pix_ulist_op;
	}

	/* find out how many levels above Op this user is */
	auto pre = serv->nick_prefixes.find_first_of('@');
	if (pre != std::string::npos && pre)
	{
		pre--;
		level = 0;
		while (1)
		{
			if (serv->nick_prefixes[pre] == user->prefix[0])
			{
				switch (level)
				{
					case 0: return pix_ulist_owner;		/* 1 level above op */
					case 1: return pix_ulist_founder;	/* 2 levels above op */
					case 2: return pix_ulist_netop;		/* 3 levels above op */
				}
				break;	/* 4+, no icons */
			}
			level++;
			if (!pre)
				break;
			pre--;
		}
	}

	return NULL;
}

void
fe_userlist_numbers (session &sess)
{
	char tbuf[256];

	if (&sess == current_tab || !sess.gui->is_tab)
	{
		if (sess.total)
		{
			snprintf (tbuf, sizeof (tbuf), _("%d ops, %d total"), sess.ops, sess.total);
			tbuf[sizeof (tbuf) - 1] = 0;
			gtk_label_set_text (GTK_LABEL (sess.gui->namelistinfo), tbuf);
		} else
		{
			gtk_label_set_text (GTK_LABEL (sess.gui->namelistinfo), NULL);
		}

		if (sess.type == session::SESS_CHANNEL && prefs.hex_gui_win_ucount)
			fe_set_title (sess);
	}
}

static void
scroll_to_iter (GtkTreeIter *iter, GtkTreeView *treeview, GtkTreeModel *model)
{
	GtkTreePath *path = gtk_tree_model_get_path (model, iter);
	if (path)
	{
		gtk_tree_view_scroll_to_cell (treeview, path, NULL, TRUE, 0.5, 0.5);
		gtk_tree_path_free (path);
	}
}

/* select a row in the userlist by nick-name */

void
userlist_select (session *sess, char *name)
{
	GtkTreeIter iter;
	GtkTreeView *treeview = GTK_TREE_VIEW (sess->gui->user_tree);
	GtkTreeModel *model = gtk_tree_view_get_model (treeview);
	GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);
	struct User *row_user;

	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		do
		{
			gtk_tree_model_get (model, &iter, COL_USER, &row_user, -1);
			if (sess->server->p_cmp (row_user->nick, name) == 0)
			{
				if (gtk_tree_selection_iter_is_selected (selection, &iter))
					gtk_tree_selection_unselect_iter (selection, &iter);
				else
					gtk_tree_selection_select_iter (selection, &iter);

				/* and make sure it's visible */
				scroll_to_iter (&iter, treeview, model);
				return;
			}
		}
		while (gtk_tree_model_iter_next (model, &iter));
	}
}

std::vector<std::string>
userlist_selection_list (GtkWidget *widget)
{
	GtkTreeIter iter;
	GtkTreeView *treeview = (GtkTreeView *) widget;
	GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);
	GtkTreeModel *model = gtk_tree_view_get_model (treeview);
	struct User *user;
	int num_sel;
	std::vector<std::string> nicks;

	/* first, count the number of selections */
	num_sel = 0;
	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		do
		{
			if (gtk_tree_selection_iter_is_selected (selection, &iter))
				num_sel++;
		}
		while (gtk_tree_model_iter_next (model, &iter));
	}

	if (num_sel < 1)
		return nicks;

	gtk_tree_model_get_iter_first (model, &iter);
	do
	{
		if (gtk_tree_selection_iter_is_selected (selection, &iter))
		{
			gtk_tree_model_get (model, &iter, COL_USER, &user, -1);
			nicks.emplace_back(user->nick);
		}
	}
	while (gtk_tree_model_iter_next (model, &iter));

	return nicks;
}

void
fe_userlist_set_selected (struct session *sess)
{
	GtkListStore *store = static_cast<GtkListStore *>( sess->res->user_model);
	GtkTreeSelection *selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (sess->gui->user_tree));
	GtkTreeIter iter;
	struct User *user;

	/* if it's not front-most tab it doesn't own the GtkTreeView! */
	if (store != (GtkListStore*) gtk_tree_view_get_model (GTK_TREE_VIEW (sess->gui->user_tree)))
		return;

	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (store), &iter))
	{
		do
		{
			gtk_tree_model_get (GTK_TREE_MODEL (store), &iter, COL_USER, &user, -1);

			if (gtk_tree_selection_iter_is_selected (selection, &iter))
				user->selected = 1;
			else
				user->selected = 0;
				
		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (store), &iter));
	}
}

static std::pair<GtkTreeIterPtr, bool> find_row (GtkTreeView *treeview, GtkTreeModel *model, const struct User *user)
{
	GtkTreeIter iter;
	User *row_user;

	bool selected = false;
	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		do
		{
			gtk_tree_model_get (model, &iter, COL_USER, &row_user, -1);
			if (row_user == user)
			{
				if (gtk_tree_view_get_model (treeview) == model)
				{
					if (gtk_tree_selection_iter_is_selected(gtk_tree_view_get_selection(treeview), &iter))
					{
						selected = true;
					}
				}
				;
				return std::make_pair(GtkTreeIterPtr(gtk_tree_iter_copy(&iter)), selected);
			}
		}
		while (gtk_tree_model_iter_next (model, &iter));
	}

	return std::pair<GtkTreeIterPtr, bool>(nullptr, selected); //  std::make_pair<GtkTreeIter*, bool>(nullptr, selected);
}

void
userlist_set_value (GtkWidget *treeview, gfloat val)
{
	gtk_adjustment_set_value (
			gtk_tree_view_get_vadjustment (GTK_TREE_VIEW (treeview)), val);
}

gfloat
userlist_get_value (GtkWidget *treeview)
{
	return gtk_adjustment_get_value (gtk_tree_view_get_vadjustment (GTK_TREE_VIEW (treeview)));
}

bool
fe_userlist_remove(session *sess, struct User const *user)
{
/*	GtkAdjustment *adj;
	gfloat val, end;*/

	auto result = find_row (GTK_TREE_VIEW (sess->gui->user_tree),
						  static_cast<GtkTreeModel*>(sess->res->user_model), user);
	if (!result.first)
		return false;

/*	adj = gtk_tree_view_get_vadjustment (GTK_TREE_VIEW (sess->gui->user_tree));
	val = adj->value;*/

	gtk_list_store_remove (static_cast<GtkListStore*>(sess->res->user_model), result.first.get());

	/* is it the front-most tab? */
/*	if (gtk_tree_view_get_model (GTK_TREE_VIEW (sess->gui->user_tree))
		 == sess->res->user_model)
	{
		end = adj->upper - adj->lower - adj->page_size;
		if (val > end)
			val = end;
		gtk_adjustment_set_value (adj, val);
	}*/

	return result.second;
}

void
fe_userlist_rehash (session *sess, struct User const *user)
{
	int nick_color = 0;

	auto result = find_row (GTK_TREE_VIEW (sess->gui->user_tree),
			static_cast<GtkTreeModel*>(sess->res->user_model), user);
	if (!result.first)
		return;

	if (prefs.hex_away_track && user->away)
		nick_color = COL_AWAY;
	else if (prefs.hex_gui_ulist_color)
		nick_color = text_color_of(user->nick);

	gtk_list_store_set (GTK_LIST_STORE (sess->res->user_model), result.first.get(),
							  COL_HOST, user->hostname ? user->hostname->c_str() : nullptr,
							  COL_GDKCOLOR, nick_color ? &colors[nick_color] : NULL,
							  -1);
}

void
fe_userlist_insert (session *sess, struct User *newuser, int row, bool sel)
{
	GtkTreeModel *model = static_cast<GtkTreeModel*>(sess->res->user_model);
	GdkPixbuf *pix = get_user_icon (sess->server, newuser);
	GtkTreeIter iter;
	int nick_color = 0;

	if (prefs.hex_away_track && newuser->away)
		nick_color = COL_AWAY;
	else if (prefs.hex_gui_ulist_color)
		nick_color = text_color_of(newuser->nick);

	std::string nick(newuser->nick);
	if (!prefs.hex_gui_ulist_icons)
	{		
		if (newuser->prefix[0] || newuser->prefix[0] != ' ')
			nick.insert(nick.begin(), newuser->prefix[0]);
		pix = NULL;
	}

	gtk_list_store_insert_with_values (GTK_LIST_STORE (model), &iter, row,
									COL_PIX, pix,
									COL_NICK, nick.c_str(),
									COL_HOST, newuser->hostname ? newuser->hostname->c_str() : nullptr,
									COL_USER, newuser,
									COL_GDKCOLOR, nick_color ? &colors[nick_color] : NULL,
								  -1);

	/* is it me? */
	if (newuser->me && sess->gui->nick_box)
	{
		if (!sess->gui->is_tab || sess == current_tab)
			mg_set_access_icon (sess->gui, pix, sess->server->is_away);
	}

#if 0
	if (prefs.hilitenotify && notify_isnotify (sess, newuser->nick))
	{
		gtk_clist_set_foreground ((GtkCList *) sess->gui->user_clist, row,
										  &colors[prefs.nu_color]);
	}
#endif

	/* is it the front-most tab? */
	if (gtk_tree_view_get_model (GTK_TREE_VIEW (sess->gui->user_tree))
		 == model)
	{
		if (sel)
			gtk_tree_selection_select_iter (gtk_tree_view_get_selection
										(GTK_TREE_VIEW (sess->gui->user_tree)), &iter);
	}
}

void
fe_userlist_move (session *sess, struct User *user, int new_row)
{
	fe_userlist_insert (sess, user, new_row, fe_userlist_remove (sess, user));
}

void
fe_userlist_clear (session &sess)
{
	gtk_list_store_clear (static_cast<GtkListStore*>(sess.res->user_model));
}

static void
userlist_dnd_drop (GtkTreeView *widget, GdkDragContext *context,
						 gint x, gint y, GtkSelectionData *selection_data,
						 guint info, guint ttime, gpointer userdata)
{
	struct User *user;
	gchar *data;
	GtkTreePath *path;
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (!gtk_tree_view_get_path_at_pos (widget, x, y, &path, NULL, NULL, NULL))
		return;

	model = gtk_tree_view_get_model (widget);
	if (!gtk_tree_model_get_iter (model, &iter, path))
		return;
	gtk_tree_model_get (model, &iter, COL_USER, &user, -1);

	data = (char *)gtk_selection_data_get_data (selection_data);

	if (data)
		mg_dnd_drop_file (current_sess, user->nick, data);
}

static gboolean
userlist_dnd_motion (GtkTreeView *widget, GdkDragContext *context, gint x,
							gint y, guint ttime, gpointer tree)
{
	GtkTreePath *path;
	GtkTreeSelection *sel;

	if (!tree)
		return FALSE;

	if (gtk_tree_view_get_path_at_pos (widget, x, y, &path, NULL, NULL, NULL))
	{
		sel = gtk_tree_view_get_selection (widget);
		gtk_tree_selection_unselect_all (sel);
		gtk_tree_selection_select_path (sel, path);
	}

	return FALSE;
}

static gboolean
userlist_dnd_leave (GtkTreeView *widget, GdkDragContext *context, guint ttime)
{
	gtk_tree_selection_unselect_all (gtk_tree_view_get_selection (widget));
	return TRUE;
}

void *
userlist_create_model (void)
{
	return gtk_list_store_new (5, GDK_TYPE_PIXBUF, G_TYPE_STRING, G_TYPE_STRING,
										G_TYPE_POINTER, GDK_TYPE_COLOR);
}

static void
userlist_add_columns (GtkTreeView * treeview)
{
	GtkCellRenderer *renderer;

	/* icon column */
	renderer = gtk_cell_renderer_pixbuf_new ();
	if (prefs.hex_gui_compact)
		g_object_set (G_OBJECT (renderer), "ypad", 0, NULL);
	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (treeview),
																-1, NULL, renderer,
																"pixbuf", 0, NULL);

	/* nick column */
	renderer = gtk_cell_renderer_text_new ();
	if (prefs.hex_gui_compact)
		g_object_set (G_OBJECT (renderer), "ypad", 0, NULL);
	gtk_cell_renderer_text_set_fixed_height_from_font (GTK_CELL_RENDERER_TEXT (renderer), 1);
	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (treeview),
																-1, NULL, renderer,
													"text", 1, "foreground-gdk", 4, NULL);

	if (prefs.hex_gui_ulist_show_hosts)
	{
		/* hostname column */
		renderer = gtk_cell_renderer_text_new ();
		if (prefs.hex_gui_compact)
			g_object_set (G_OBJECT (renderer), "ypad", 0, NULL);
		gtk_cell_renderer_text_set_fixed_height_from_font (GTK_CELL_RENDERER_TEXT (renderer), 1);
		gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (treeview),
																	-1, NULL, renderer,
																	"text", 2, NULL);
	}
}

static gint
userlist_click_cb (GtkWidget *widget, GdkEventButton *event, gpointer userdata)
{
	GtkTreeSelection *sel;
	GtkTreePath *path;

	if (!event)
		return FALSE;

	if (!(event->state & STATE_CTRL) &&
		event->type == GDK_2BUTTON_PRESS && prefs.hex_gui_ulist_doubleclick[0])
	{
		auto nicks = userlist_selection_list (widget);
		if (!nicks.empty())
		{
			nick_command_parse (current_sess, prefs.hex_gui_ulist_doubleclick, nicks[0],
									  nicks[0]);
		}
		return TRUE;
	}

	if (event->button == 3)
	{
		/* do we have a multi-selection? */
		auto nicks = userlist_selection_list (widget);
		if (!nicks.empty())
		{
			menu_nickmenu (current_sess, event, nicks[0], nicks.size());
			return TRUE;
		}

		sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
		if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (widget),
			 event->x, event->y, &path, 0, 0, 0))
		{
			std::unique_ptr<GtkTreePath, decltype(&gtk_tree_path_free)> raii_path(path, gtk_tree_path_free);
			gtk_tree_selection_unselect_all (sel);
			gtk_tree_selection_select_path (sel, path);
			nicks = userlist_selection_list (widget);
			if (!nicks.empty())
			{
				menu_nickmenu (current_sess, event, nicks[0], nicks.size());
			}
		} else
		{
			gtk_tree_selection_unselect_all (sel);
		}

		return TRUE;
	}

	return FALSE;
}

static gboolean
userlist_key_cb (GtkWidget *wid, GdkEventKey *evt, gpointer userdata)
{
	if (evt->keyval >= GDK_KEY_asterisk && evt->keyval <= GDK_KEY_z)
	{
		/* dirty trick to avoid auto-selection */
		SPELL_ENTRY_SET_EDITABLE (current_sess->gui->input_box, FALSE);
		gtk_widget_grab_focus (current_sess->gui->input_box);
		SPELL_ENTRY_SET_EDITABLE (current_sess->gui->input_box, TRUE);
		gtk_widget_event (current_sess->gui->input_box, (GdkEvent *)evt);
		return TRUE;
	}

	return FALSE;
}

GtkWidget *
userlist_create (GtkWidget *box)
{
	GtkWidget *sw, *treeview;
	static const GtkTargetEntry dnd_dest_targets[] =
	{
		{"text/uri-list", 0, 1},
		{"HEXCHAT_CHANVIEW", GTK_TARGET_SAME_APP, 75 }
	};
	static const GtkTargetEntry dnd_src_target[] =
	{
		{"HEXCHAT_USERLIST", GTK_TARGET_SAME_APP, 75 }
	};

	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (sw),
													 GTK_SHADOW_ETCHED_IN);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw),
											  prefs.hex_gui_ulist_show_hosts ?
												GTK_POLICY_AUTOMATIC :
												GTK_POLICY_NEVER,
											  GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start (GTK_BOX (box), sw, TRUE, TRUE, 0);
	gtk_widget_show (sw);

	treeview = gtk_tree_view_new ();
	gtk_widget_set_name (treeview, "hexchat-userlist");
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (treeview), FALSE);
	gtk_tree_selection_set_mode (gtk_tree_view_get_selection
										  (GTK_TREE_VIEW (treeview)),
										  GTK_SELECTION_MULTIPLE);

	/* set up drops */
	gtk_drag_dest_set (treeview, GTK_DEST_DEFAULT_ALL, dnd_dest_targets, 2,
							 static_cast<GdkDragAction>(GDK_ACTION_MOVE | GDK_ACTION_COPY | GDK_ACTION_LINK));
	gtk_drag_source_set (treeview, GDK_BUTTON1_MASK, dnd_src_target, 1, GDK_ACTION_MOVE);

	/* file DND (for DCC) */
	g_signal_connect (G_OBJECT (treeview), "drag_motion",
							G_CALLBACK (userlist_dnd_motion), treeview);
	g_signal_connect (G_OBJECT (treeview), "drag_leave",
							G_CALLBACK (userlist_dnd_leave), 0);
	g_signal_connect (G_OBJECT (treeview), "drag_data_received",
							G_CALLBACK (userlist_dnd_drop), treeview);

	g_signal_connect (G_OBJECT (treeview), "button_press_event",
							G_CALLBACK (userlist_click_cb), 0);
	g_signal_connect (G_OBJECT (treeview), "key_press_event",
							G_CALLBACK (userlist_key_cb), 0);

	/* tree/chanview DND */
	g_signal_connect (G_OBJECT (treeview), "drag_begin",
							G_CALLBACK (mg_drag_begin_cb), NULL);
	g_signal_connect (G_OBJECT (treeview), "drag_drop",
							G_CALLBACK (mg_drag_drop_cb), NULL);
	g_signal_connect (G_OBJECT (treeview), "drag_motion",
							G_CALLBACK (mg_drag_motion_cb), NULL);
	g_signal_connect (G_OBJECT (treeview), "drag_end",
							G_CALLBACK (mg_drag_end_cb), NULL);

	userlist_add_columns (GTK_TREE_VIEW (treeview));

	gtk_container_add (GTK_CONTAINER (sw), treeview);
	gtk_widget_show (treeview);

	return treeview;
}

void
userlist_show (session *sess)
{
	gtk_tree_view_set_model (GTK_TREE_VIEW (sess->gui->user_tree),
									 static_cast<GtkTreeModel*>(sess->res->user_model));
}

void
fe_uselect (session *sess, char *word[], int do_clear, int scroll_to)
{
	int thisname;
	char *name;
	GtkTreeIter iter;
	GtkTreeView *treeview = GTK_TREE_VIEW (sess->gui->user_tree);
	GtkTreeModel *model = gtk_tree_view_get_model (treeview);
	GtkTreeSelection *selection = gtk_tree_view_get_selection (treeview);
	struct User *row_user;

	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		if (do_clear)
			gtk_tree_selection_unselect_all (selection);

		do
		{
			if (*word[0])
			{
				gtk_tree_model_get (model, &iter, COL_USER, &row_user, -1);
				thisname = 0;
				while ( *(name = word[thisname++]) )
				{
					if (sess->server->p_cmp (row_user->nick, name) == 0)
					{
						gtk_tree_selection_select_iter (selection, &iter);
						if (scroll_to)
							scroll_to_iter (&iter, treeview, model);
						break;
					}
				}
			}

		}
		while (gtk_tree_model_iter_next (model, &iter));
	}
}
