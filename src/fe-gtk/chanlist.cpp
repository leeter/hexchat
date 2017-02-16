/* X-Chat
 * Copyright (C) 1998-2006 Peter Zelezny.
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

#include "precompile.hpp"

#include "fe-gtk.hpp"

#include "../common/hexchat.hpp"
#include "../common/hexchatc.hpp"
#include "../common/cfgfiles.hpp"
#include "../common/outbound.hpp"
#include "../common/util.hpp"
#include "../common/fe.hpp"
#include "../common/server.hpp"
#include "gtkutil.hpp"
#include "maingui.hpp"
#include "menu.hpp"
#include "chanlist.hpp"
#include "custom-list.hpp"
#include "gtk_helpers.hpp"
namespace
{

enum
{
	COL_CHANNEL,
	COL_USERS,
	COL_TOPIC,
	N_COLUMNS
};

#define GET_MODEL(xserv) (gtk_tree_view_get_model(GTK_TREE_VIEW(xserv->gui->chanlist_list)))

static GtkTreeModel*
get_model(server_gui & gui)
{
	return gtk_tree_view_get_model(GTK_TREE_VIEW(gui.chanlist_list));
}

static bool
chanlist_match (const server_gui &gui, const std::string& str)
{
	switch (gui.chanlist_search_type)
	{
	case 1:
		return match (gtk_entry_get_text (GTK_ENTRY (gui.chanlist_wild)), str.c_str());
	case 2:
		if (!gui.have_regex)
			return false;

		return g_regex_match (gui.chanlist_match_regex, str.c_str(), static_cast<GRegexMatchFlags>(0), NULL) ? true : false;
	default:	/* case 0: */
		return nocasestrstr (str.c_str(), gtk_entry_get_text (GTK_ENTRY (gui.chanlist_wild))) ? true : false;
	}
}

/**
 * Updates the caption to reflect the number of users and channels
 */
static void
chanlist_update_caption (server_gui &gui)
{
	gchar tbuf[256];

	snprintf (tbuf, sizeof tbuf,
				 _("Displaying %d/%d users on %d/%d channels."),
				 gui.chanlist_users_shown_count,
				 gui.chanlist_users_found_count,
				 gui.chanlist_channels_shown_count,
				 gui.chanlist_channels_found_count);

	gtk_label_set_text (GTK_LABEL (gui.chanlist_label), tbuf);
	gui.chanlist_caption_is_stale = false;
}

static void
chanlist_update_buttons (server_gui & gui)
{
	if (gui.chanlist_channels_shown_count)
	{
		gtk_widget_set_sensitive (gui.chanlist_join, TRUE);
		gtk_widget_set_sensitive (gui.chanlist_savelist, TRUE);
	}
	else
	{
		gtk_widget_set_sensitive (gui.chanlist_join, FALSE);
		gtk_widget_set_sensitive (gui.chanlist_savelist, FALSE);
	}
}

static void
chanlist_reset_counters (server_gui &gui)
{
	gui.chanlist_users_found_count = 0;
	gui.chanlist_users_shown_count = 0;
	gui.chanlist_channels_found_count = 0;
	gui.chanlist_channels_shown_count = 0;

	chanlist_update_caption (gui);
	chanlist_update_buttons (gui);
}

/* free up our entire linked list and all the nodes */

static void
chanlist_data_free (server_gui &gui)
{
	if (gui.chanlist_data_stored_rows)
	{
		for (GSList * rows = gui.chanlist_data_stored_rows; rows != NULL;
			  rows = rows->next)
		{
			chanlistrow * data = static_cast<chanlistrow*>(rows->data);
			delete data;
		}

		g_slist_free (gui.chanlist_data_stored_rows);
		gui.chanlist_data_stored_rows = NULL;
	}

	g_slist_free (gui.chanlist_pending_rows);
	gui.chanlist_pending_rows = NULL;
}

/* add any rows we received from the server in the last 0.25s to the GUI */

static void
chanlist_flush_pending (server_gui &gui)
{
	GSList *list = gui.chanlist_pending_rows;
	GtkTreeModel *model;
	chanlistrow *row;

	if (!list)
	{
		if (gui.chanlist_caption_is_stale)
			chanlist_update_caption (gui);
		return;
	}
	model = get_model (gui);

	while (list)
	{
		row = static_cast<chanlistrow*>(list->data);
		custom_list_append (CUSTOM_LIST (model), row);
		list = list->next;
	}

	g_slist_free (gui.chanlist_pending_rows);
	gui.chanlist_pending_rows = NULL;
	chanlist_update_caption (gui);
}

static gboolean
chanlist_timeout (server *serv)
{
	chanlist_flush_pending (*serv->gui);
	return TRUE;
}

/**
 * Places a data row into the gui GtkTreeView, if and only if the row matches
 * the user and regex/search requirements.
 */
static void
chanlist_place_row_in_gui (server_gui &gui, chanlistrow *next_row, gboolean force)
{
	GtkTreeModel *model;

	/* First, update the 'found' counter values */
	gui.chanlist_users_found_count += next_row->users;
	gui.chanlist_channels_found_count++;

	if (gui.chanlist_channels_shown_count == 1)
		/* join & save buttons become live */
		chanlist_update_buttons (gui);

	if (next_row->users < gui.chanlist_minusers)
	{
		gui.chanlist_caption_is_stale = true;
		return;
	}

	if (next_row->users > gui.chanlist_maxusers
		 && gui.chanlist_maxusers > 0)
	{
		gui.chanlist_caption_is_stale = true;
		return;
	}

	if (gtk_entry_get_text (GTK_ENTRY (gui.chanlist_wild))[0])
	{
		/* Check what the user wants to match. If both buttons or _neither_
		 * button is checked, look for match in both by default. 
		 */
		if (gui.chanlist_match_wants_channel ==
			 gui.chanlist_match_wants_topic)
		{
			if (!chanlist_match (gui, next_row->chan)
				&& !chanlist_match(gui, next_row->topic))
			{
				gui.chanlist_caption_is_stale = true;
				return;
			}
		}

		else if (gui.chanlist_match_wants_channel)
		{
			if (!chanlist_match(gui, next_row->chan))
			{
				gui.chanlist_caption_is_stale = true;
				return;
			}
		}

		else if (gui.chanlist_match_wants_topic)
		{
			if (!chanlist_match(gui, next_row->topic))
			{
				gui.chanlist_caption_is_stale = true;
				return;
			}
		}
	}

	if (force || gui.chanlist_channels_shown_count < 20)
	{
		model = get_model(gui);
		/* makes it appear fast :) */
		custom_list_append (CUSTOM_LIST (model), next_row);
		chanlist_update_caption (gui);
	}
	else
		/* add it to GUI at the next update interval */
		gui.chanlist_pending_rows = g_slist_prepend (gui.chanlist_pending_rows, next_row);

	/* Update the 'shown' counter values */
	gui.chanlist_users_shown_count += next_row->users;
	gui.chanlist_channels_shown_count++;
}

/* Performs the LIST download from the IRC server. */

static void
chanlist_do_refresh (server *serv)
{
	if (serv->gui->chanlist_flash_tag)
	{
		g_source_remove (serv->gui->chanlist_flash_tag);
		serv->gui->chanlist_flash_tag = 0;
	}

	if (!serv->connected)
	{
		fe_message (_("Not connected."), FE_MSG_ERROR);
		return;
	}

	custom_list_clear ((CustomList *)GET_MODEL (serv));
	gtk_widget_set_sensitive (serv->gui->chanlist_refresh, FALSE);

	chanlist_data_free (*serv->gui);
	chanlist_reset_counters (*serv->gui);

	/* can we request a list with minusers arg? */
	if (serv->use_listargs)
	{
		/* yes - it will download faster */
		serv->p_list_channels ("", serv->gui->chanlist_minusers);
		/* don't allow the spin button below this value from now on */
		serv->gui->chanlist_minusers_downloaded = serv->gui->chanlist_minusers;
	}
	else
	{
		/* download all, filter minusers locally only */
		serv->p_list_channels ("", 1);
		serv->gui->chanlist_minusers_downloaded = 1;
	}

/*	gtk_spin_button_set_range ((GtkSpinButton *)serv->gui->chanlist_min_spin,
										serv->gui->chanlist_minusers_downloaded, 999999);*/
}

static void
chanlist_refresh (GtkWidget *, server *serv)
{
	chanlist_do_refresh (serv);
}

/**
 * Fills the gui GtkTreeView with stored items from the GSList.
 */
static void
chanlist_build_gui_list (server *serv)
{
	GSList *rows;

	/* first check if the list is present */
	if (serv->gui->chanlist_data_stored_rows == NULL)
	{
		/* start a download */
		chanlist_do_refresh (serv);
		return;
	}

	custom_list_clear ((CustomList *)GET_MODEL (serv));

	/* discard pending rows FIXME: free the structs? */
	g_slist_free (serv->gui->chanlist_pending_rows);
	serv->gui->chanlist_pending_rows = NULL;

	/* Reset the counters */
	chanlist_reset_counters (*serv->gui);

	/* Refill the list */
	for (rows = serv->gui->chanlist_data_stored_rows; rows != NULL;
		  rows = rows->next)
	{
		chanlist_place_row_in_gui(*serv->gui, static_cast<chanlistrow*>(rows->data), TRUE);
	}

	custom_list_resort ((CustomList *)GET_MODEL (serv));
}
}// end anonymous namespace

/**
 * Accepts incoming channel data from inbound.c, allocates new space for a
 * chanlistrow, adds it to our linked list and calls chanlist_place_row_in_gui.
 */
void
fe_add_chan_list (server *serv, char *chan, char *users, char *topic)
{
	auto len = strlen (chan) + 1;

	chanlistrow * next_row = new chanlistrow;
	next_row->topic = strip_color(topic, STRIP_ALL);
	glib_string collation_key(g_utf8_collate_key(chan, len - 1));
	next_row->collation_key = collation_key ? collation_key.get() : chan;
	next_row->users = atoi (users);

	/* add this row to the data */
	serv->gui->chanlist_data_stored_rows =
		g_slist_prepend (serv->gui->chanlist_data_stored_rows, next_row);

	/* _possibly_ add the row to the gui */
	chanlist_place_row_in_gui (*serv->gui, next_row, FALSE);
}

void
fe_chan_list_end (server *serv)
{
	/* download complete */
	chanlist_flush_pending (*serv->gui);
	gtk_widget_set_sensitive (serv->gui->chanlist_refresh, TRUE);
	custom_list_resort ((CustomList *)GET_MODEL (serv));
}

namespace
{

static void
chanlist_search_pressed (GtkButton *, server *serv)
{
	chanlist_build_gui_list (serv);
}

static void
chanlist_find_cb (GtkWidget * wid, server *serv)
{
	const char *pattern = gtk_entry_get_text (GTK_ENTRY (wid));

	/* recompile the regular expression. */
	if (serv->gui->have_regex)
	{
		serv->gui->have_regex = false;
		g_regex_unref (serv->gui->chanlist_match_regex);
	}

	serv->gui->chanlist_match_regex = g_regex_new(pattern, static_cast<GRegexCompileFlags>(G_REGEX_CASELESS | G_REGEX_EXTENDED),
												G_REGEX_MATCH_NOTBOL, NULL);

	if (serv->gui->chanlist_match_regex)
		serv->gui->have_regex = true;
}

static void
chanlist_match_channel_button_toggled (GtkWidget * wid, server *serv)
{
	serv->gui->chanlist_match_wants_channel = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (wid));
}

static void
chanlist_match_topic_button_toggled (GtkWidget * wid, server *serv)
{
	serv->gui->chanlist_match_wants_topic = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (wid));
}

static char *
chanlist_get_selected (server *serv, bool get_topic)
{
	char *chan;
	GtkTreeSelection *sel = gtk_tree_view_get_selection (GTK_TREE_VIEW (serv->gui->chanlist_list));
	GtkTreeModel *model;
	GtkTreeIter iter;

	if (!gtk_tree_selection_get_selected (sel, &model, &iter))
		return NULL;

	gtk_tree_model_get (model, &iter, get_topic ? COL_TOPIC : COL_CHANNEL, &chan, -1);
	return chan;
}

static void
chanlist_join (GtkWidget *, server *serv)
{
	char tbuf[CHANLEN + 6];
	char *chan = chanlist_get_selected (serv, false);
	if (chan)
	{
		if (serv->connected && (strcmp (chan, "*") != 0))
		{
			snprintf (tbuf, sizeof (tbuf), "join %s", chan);
			handle_command (serv->server_session, tbuf, FALSE);
		}
		else
			;//gdk_beep ();
		g_free (chan);
	}
}

static void
chanlist_filereq_done (server *serv, char *file)
{
	time_t t = time (0);
	int fh, users;
	char *chan, *topic;
	char buf[1024];
	GtkTreeModel *model = GET_MODEL (serv);
	GtkTreeIter iter;

	if (!file)
		return;

	fh = hexchat_open_file (file, O_TRUNC | O_WRONLY | O_CREAT, 0600,
								 XOF_DOMODE | XOF_FULLPATH);
	if (fh == -1)
		return;

	snprintf (buf, sizeof buf, "HexChat Channel List: %s - %s\n",
				 serv->servername, ctime (&t));
	write (fh, buf, strlen (buf));

	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		do
		{
			gtk_tree_model_get (model, &iter,
									  COL_CHANNEL, &chan,
									  COL_USERS, &users,
									  COL_TOPIC, &topic, -1);
			snprintf (buf, sizeof buf, "%-16s %-5d%s\n", chan, users, topic);
			g_free (chan);
			g_free (topic);
			write (fh, buf, strlen (buf));
		}
		while (gtk_tree_model_iter_next (model, &iter));
	}

	close (fh);
}

static void
chanlist_save (GtkWidget *, server *serv)
{
	GtkTreeIter iter;
	GtkTreeModel *model = GET_MODEL (serv);

	if (gtk_tree_model_get_iter_first (model, &iter))
		gtkutil_file_req (_("Select an output filename"), (filereqcallback) chanlist_filereq_done,
								serv, NULL, NULL, FRF_WRITE);
}

static gboolean
chanlist_flash (server *serv)
{
	if (gtk_widget_get_state (serv->gui->chanlist_refresh) != GTK_STATE_ACTIVE)
		gtk_widget_set_state (serv->gui->chanlist_refresh, GTK_STATE_ACTIVE);
	else
		gtk_widget_set_state (serv->gui->chanlist_refresh, GTK_STATE_PRELIGHT);

	return TRUE;
}

static void
chanlist_minusers (GtkSpinButton *wid, server *serv)
{
	serv->gui->chanlist_minusers = gtk_spin_button_get_value_as_int (wid);
	prefs.hex_gui_chanlist_minusers = serv->gui->chanlist_minusers;
	save_config();

	if (serv->gui->chanlist_minusers < serv->gui->chanlist_minusers_downloaded)
	{
		if (serv->gui->chanlist_flash_tag == 0)
			serv->gui->chanlist_flash_tag = g_timeout_add (500, (GSourceFunc)chanlist_flash, serv);
	}
	else
	{
		if (serv->gui->chanlist_flash_tag)
		{
			g_source_remove (serv->gui->chanlist_flash_tag);
			serv->gui->chanlist_flash_tag = 0;
		}
	}
}

static void
chanlist_maxusers (GtkSpinButton *wid, server *serv)
{
	serv->gui->chanlist_maxusers = gtk_spin_button_get_value_as_int (wid);
	prefs.hex_gui_chanlist_maxusers = serv->gui->chanlist_maxusers;
	save_config();
}

static void
chanlist_dclick_cb (GtkTreeView *, GtkTreePath *,
						  GtkTreeViewColumn *, gpointer data)
{
	chanlist_join (0, (server *) data);	/* double clicked a row */
}

static void
chanlist_menu_destroy (GtkWidget *menu, gpointer)
{
	gtk_widget_destroy (menu);
	g_object_unref (menu);
}

static void
chanlist_copychannel (GtkWidget *item, server *serv)
{
	char *chan = chanlist_get_selected (serv, false);
	if (chan)
	{
		gtkutil_copy_to_clipboard (item, NULL, chan);
		g_free (chan);
	}
}

static void
chanlist_copytopic (GtkWidget *item, server *serv)
{
	glib_string topic(chanlist_get_selected (serv, true));
	if (topic)
	{
		gtkutil_copy_to_clipboard (item, NULL, topic.get());
	}
}

static gboolean
chanlist_button_cb (GtkTreeView *tree, GdkEventButton *event, server *serv)
{
	if (event->button != 3)
		return FALSE;

	GtkTreePath *path;
	if (!gtk_tree_view_get_path_at_pos (tree, event->x, event->y, &path, 0, 0, 0))
		return FALSE;
	GtkTreePathPtr path_ptr(path);
	/* select what they right-clicked on */
	auto sel = gtk_tree_view_get_selection (tree);
	gtk_tree_selection_unselect_all (sel);
	gtk_tree_selection_select_path (sel, path);

	auto menu = gtk_menu_new ();
	if (event->window)
		gtk_menu_set_screen (GTK_MENU (menu), gdk_window_get_screen (event->window));
	g_object_ref (menu);
	g_object_ref_sink (menu);
	g_object_unref (menu);
	g_signal_connect (G_OBJECT (menu), "selection-done",
							G_CALLBACK (chanlist_menu_destroy), NULL);
	mg_create_icon_item (_("_Join Channel"), "go-jump", menu,
		G_CALLBACK(chanlist_join), serv);
	mg_create_icon_item (_("_Copy Channel Name"), "edit-copy", menu,
		G_CALLBACK(chanlist_copychannel), serv);
	mg_create_icon_item (_("Copy _Topic Text"), "edit-copy", menu,
		G_CALLBACK(chanlist_copytopic), serv);

	glib_string chan (chanlist_get_selected (serv, false));
	menu_addfavoritemenu (serv, menu, chan.get(), false);

	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 0, event->time);

	return TRUE;
}

static void
chanlist_destroy_widget (GtkWidget *, server_gui *gui)
{
	if (!gui)
		throw std::invalid_argument("invalid server_gui reference");
	custom_list_clear ((CustomList *)get_model(*gui));
	chanlist_data_free (*gui);

	if (gui->chanlist_flash_tag)
	{
		g_source_remove (gui->chanlist_flash_tag);
		gui->chanlist_flash_tag = 0;
	}

	if (gui->chanlist_tag)
	{
		g_source_remove (gui->chanlist_tag);
		gui->chanlist_tag = 0;
	}

	if (gui->have_regex)
	{
		g_regex_unref (gui->chanlist_match_regex);
		gui->have_regex = false;
	}
}

static void
chanlist_closegui (GtkWidget *, server *serv)
{
	if (is_server (serv))
		serv->gui->chanlist_window = NULL;
}

static void
chanlist_add_column (GtkWidget *tree, int textcol, int size, char *title, gboolean right_justified)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *col;

	renderer = gtk_cell_renderer_text_new ();
	if (right_justified)
		g_object_set (G_OBJECT (renderer), "xalign", (gfloat) 1.0, NULL);
	g_object_set (G_OBJECT (renderer), "ypad", (gint) 0, NULL);
	gtk_tree_view_insert_column_with_attributes (GTK_TREE_VIEW (tree), -1, title,
																renderer, "text", textcol, NULL);
	gtk_cell_renderer_text_set_fixed_height_from_font (GTK_CELL_RENDERER_TEXT (renderer), 1);

	col = gtk_tree_view_get_column (GTK_TREE_VIEW (tree), textcol);
	gtk_tree_view_column_set_sort_column_id (col, textcol);
	gtk_tree_view_column_set_resizable (col, TRUE);
	if (textcol == COL_CHANNEL)
	{
		gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);
		gtk_tree_view_column_set_fixed_width (col, size);
	}
	else if (textcol == COL_USERS)
	{
		gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
		gtk_tree_view_column_set_resizable (col, FALSE);
	}
}

static void
chanlist_combo_cb (GtkWidget *combo, server *serv)
{
	serv->gui->chanlist_search_type = gtk_combo_box_get_active (GTK_COMBO_BOX (combo));
}

} // end anonymous namespace

void
chanlist_opengui (server *serv, bool do_refresh)
{
	GtkWidget *vbox, *hbox, *table, *wid, *view;
	char tbuf[256];
	GtkListStore *store;

	if (serv->gui->chanlist_window)
	{
		mg_bring_tofront (serv->gui->chanlist_window);
		return;
	}

	snprintf (tbuf, sizeof tbuf, _(DISPLAY_NAME": Channel List (%s)"),
				 serv->get_network (true).data());

	serv->gui->chanlist_pending_rows = NULL;
	serv->gui->chanlist_tag = 0;
	serv->gui->chanlist_flash_tag = 0;
	serv->gui->chanlist_data_stored_rows = NULL;

	if (!serv->gui->chanlist_minusers)
	{
		if (prefs.hex_gui_chanlist_minusers < 1 || prefs.hex_gui_chanlist_minusers)
		{
			prefs.hex_gui_chanlist_minusers = 5;
			save_config();
		}

		serv->gui->chanlist_minusers = prefs.hex_gui_chanlist_minusers;
	}

	if (!serv->gui->chanlist_maxusers)
	{
		if (prefs.hex_gui_chanlist_maxusers < 1 || prefs.hex_gui_chanlist_maxusers > 999999)
		{
			prefs.hex_gui_chanlist_maxusers = 9999;
			save_config();
		}

		serv->gui->chanlist_maxusers = prefs.hex_gui_chanlist_maxusers;
	}

	serv->gui->chanlist_window =
		mg_create_generic_tab("ChanList", tbuf, FALSE, TRUE, G_CALLBACK(chanlist_closegui),
								serv, 640, 480, &vbox, serv);
	gtkutil_destroy_on_esc (serv->gui->chanlist_window);

	gtk_container_set_border_width (GTK_CONTAINER (vbox), 6);
	gtk_box_set_spacing (GTK_BOX (vbox), 12);

	/* make a label to store the user/channel info */
	wid = gtk_label_new (NULL);
	gtk_box_pack_start (GTK_BOX (vbox), wid, 0, 0, 0);
	gtk_widget_show (wid);
	serv->gui->chanlist_label = wid;

	/* ============================================================= */

	store = (GtkListStore *) custom_list_new();
	view = gtkutil_treeview_new (vbox, GTK_TREE_MODEL (store), NULL, -1);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (gtk_widget_get_parent (view)),
													 GTK_SHADOW_IN);
	serv->gui->chanlist_list = view;

	g_signal_connect (G_OBJECT (view), "row_activated",
							G_CALLBACK (chanlist_dclick_cb), serv);
	g_signal_connect (G_OBJECT (view), "button-press-event",
							G_CALLBACK (chanlist_button_cb), serv);

	chanlist_add_column (view, COL_CHANNEL, 96, _("Channel"), FALSE);
	chanlist_add_column (view, COL_USERS,   50, _("Users"),   TRUE);
	chanlist_add_column (view, COL_TOPIC,   50, _("Topic"),   FALSE);
	gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (view), TRUE);
	/* this is a speed up, but no horizontal scrollbar :( */
	/*gtk_tree_view_set_fixed_height_mode (GTK_TREE_VIEW (view), TRUE);*/
	gtk_widget_show (view);
	
	/* ============================================================= */
#if GTK_CHECK_VERSION(3, 0, 0)
	table = gtk_grid_new();
	auto grid = GTK_GRID(table);
	for(int i = 0; i < 4; ++i)
	{
		gtk_grid_insert_row(grid, 0);
		gtk_grid_insert_column(grid, 0);
	}
#else
	table = gtk_table_new (4, 4, FALSE);
	gtk_table_set_col_spacings (GTK_TABLE (table), 12);
	gtk_table_set_row_spacings (GTK_TABLE (table), 3);
#endif
	gtk_box_pack_start (GTK_BOX (vbox), table, 0, 1, 0);
	gtk_widget_show (table);

	wid = gtkutil_button(NULL, "edit-find", 0, G_CALLBACK(chanlist_search_pressed), serv,
								 _("_Search"));
	serv->gui->chanlist_search = wid;
#if GTK_CHECK_VERSION(3, 0, 0)
	gtk_grid_attach(grid, wid, 3, 4, 3, 4);
#else
	gtk_table_attach (GTK_TABLE (table), wid, 3, 4, 3, 4, GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
#endif

	wid = gtkutil_button(NULL, "view-refresh", 0, G_CALLBACK(chanlist_refresh), serv,
								 _("_Download List"));
	serv->gui->chanlist_refresh = wid;
#if GTK_CHECK_VERSION(3, 0, 0)
	gtk_grid_attach(grid, wid, 3, 4, 2, 4);
#else
	gtk_table_attach (GTK_TABLE (table), wid, 3, 4, 2, 3, GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
#endif

	wid = gtkutil_button(NULL, "document-save-as", 0, G_CALLBACK(chanlist_save), serv,
								 _("Save _List..."));
	serv->gui->chanlist_savelist = wid;
#if GTK_CHECK_VERSION(3, 0, 0)
	gtk_grid_attach(grid, wid, 3, 4, 1, 4);
#else
	gtk_table_attach (GTK_TABLE (table), wid, 3, 4, 1, 2, GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
#endif

	wid = gtkutil_button(NULL, "go-jump", 0, G_CALLBACK(chanlist_join), serv,
						 _("_Join Channel"));
	serv->gui->chanlist_join = wid;
#if GTK_CHECK_VERSION(3, 0, 0)
	gtk_grid_attach(grid, wid, 3, 4, 0, 4);
#else
	gtk_table_attach (GTK_TABLE (table), wid, 3, 4, 0, 1, GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
#endif

	/* ============================================================= */

	wid = gtk_label_new (_("Show only:"));
	gtk_misc_set_alignment (GTK_MISC (wid), 0, 0.5);
#if GTK_CHECK_VERSION(3, 0, 0)
	gtk_grid_attach(grid, wid, 0, 1, 3, 4);
#else
	gtk_table_attach (GTK_TABLE (table), wid, 0, 1, 3, 4, GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
#endif
	gtk_widget_show (wid);

#if GTK_CHECK_VERSION(3, 0, 0)
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
#else
	hbox = gtk_hbox_new(0, 0);
#endif
	gtk_box_set_spacing (GTK_BOX (hbox), 9);
#if GTK_CHECK_VERSION(3, 0, 0)
	gtk_grid_attach(grid, wid, 1, 2, 3, 4);
#else
	gtk_table_attach (GTK_TABLE (table), hbox, 1, 2, 3, 4,
							GTK_FILL, GTK_FILL, 0, 0);
#endif
	gtk_widget_show (hbox);

	wid = gtk_label_new (_("channels with"));
	gtk_box_pack_start (GTK_BOX (hbox), wid, 0, 0, 0);
	gtk_widget_show (wid);

	wid = gtk_spin_button_new_with_range (1, 999999, 1);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (wid),
										serv->gui->chanlist_minusers);
	g_signal_connect (G_OBJECT (wid), "value_changed",
							G_CALLBACK (chanlist_minusers), serv);
	gtk_box_pack_start (GTK_BOX (hbox), wid, 0, 0, 0);
	gtk_widget_show (wid);
	serv->gui->chanlist_min_spin = wid;

	wid = gtk_label_new (_("to"));
	gtk_box_pack_start (GTK_BOX (hbox), wid, 0, 0, 0);
	gtk_widget_show (wid);

	wid = gtk_spin_button_new_with_range (1, 999999, 1);
	gtk_spin_button_set_value (GTK_SPIN_BUTTON (wid),
										serv->gui->chanlist_maxusers);
	g_signal_connect (G_OBJECT (wid), "value_changed",
							G_CALLBACK (chanlist_maxusers), serv);
	gtk_box_pack_start (GTK_BOX (hbox), wid, 0, 0, 0);
	gtk_widget_show (wid);

	wid = gtk_label_new (_("users."));
	gtk_box_pack_start (GTK_BOX (hbox), wid, 0, 0, 0);
	gtk_widget_show (wid);

	/* ============================================================= */

	wid = gtk_label_new (_("Look in:"));
	gtk_misc_set_alignment (GTK_MISC (wid), 0, 0.5);
#if GTK_CHECK_VERSION(3, 0, 0)
	gtk_grid_attach(grid, wid, 0, 1, 2, 3);
#else
	gtk_table_attach (GTK_TABLE (table), wid, 0, 1, 2, 3, GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
#endif
	gtk_widget_show (wid);

#if GTK_CHECK_VERSION(3, 0, 0)
	hbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
#else
	hbox = gtk_hbox_new (0, 0);
#endif
	gtk_box_set_spacing (GTK_BOX (hbox), 12);
#if GTK_CHECK_VERSION(3, 0, 0)
	gtk_grid_attach(grid, wid, 1, 2, 2, 3);
#else
	gtk_table_attach (GTK_TABLE (table), hbox, 1, 2, 2, 3,
							GTK_FILL, GTK_FILL, 0, 0);
#endif
	gtk_widget_show (hbox);

	wid = gtk_check_button_new_with_label (_("Channel name"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (wid), TRUE);
	g_signal_connect (G_OBJECT (wid), "toggled",
							  G_CALLBACK(chanlist_match_channel_button_toggled), serv);
	gtk_box_pack_start (GTK_BOX (hbox), wid, 0, 0, 0);
	gtk_widget_show (wid);

	wid = gtk_check_button_new_with_label (_("Topic"));
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (wid), TRUE);
	g_signal_connect (G_OBJECT (wid), "toggled",
							  G_CALLBACK (chanlist_match_topic_button_toggled),
							  serv);
	gtk_box_pack_start (GTK_BOX (hbox), wid, 0, 0, 0);
	gtk_widget_show (wid);

	serv->gui->chanlist_match_wants_channel = 1;
	serv->gui->chanlist_match_wants_topic = 1;

	/* ============================================================= */

	wid = gtk_label_new (_("Search type:"));
	gtk_misc_set_alignment (GTK_MISC (wid), 0, 0.5);
#if GTK_CHECK_VERSION(3, 0, 0)
	gtk_grid_attach(grid, wid, 0, 1, 1, 2);
#else
	gtk_table_attach (GTK_TABLE (table), wid, 0, 1, 1, 2, GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
#endif
	gtk_widget_show (wid);

	wid = gtk_combo_box_text_new ();
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (wid), _("Simple Search"));
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (wid), _("Pattern Match (Wildcards)"));
	gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (wid), _("Regular Expression"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (wid), serv->gui->chanlist_search_type);
#if GTK_CHECK_VERSION(3, 0, 0)
	gtk_grid_attach(grid, wid, 1, 2, 1, 2);
#else
	gtk_table_attach (GTK_TABLE (table), wid, 1, 2, 1, 2, GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
#endif
	g_signal_connect (G_OBJECT (wid), "changed",
							G_CALLBACK (chanlist_combo_cb), serv);
	gtk_widget_show (wid);

	/* ============================================================= */

	wid = gtk_label_new (_("Find:"));
	gtk_misc_set_alignment (GTK_MISC (wid), 0, 0.5);
#if GTK_CHECK_VERSION(3, 0, 0)
	gtk_grid_attach(grid, wid, 0, 1, 0, 1);
#else
	gtk_table_attach (GTK_TABLE (table), wid, 0, 1, 0, 1, GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
#endif
	gtk_widget_show (wid);

	wid = gtk_entry_new ();
	gtk_entry_set_max_length (GTK_ENTRY(wid), 255);
	g_signal_connect (G_OBJECT (wid), "changed",
							  G_CALLBACK (chanlist_find_cb), serv);
	g_signal_connect (G_OBJECT (wid), "activate",
							  G_CALLBACK (chanlist_search_pressed),
							  (gpointer) serv);
#if GTK_CHECK_VERSION(3, 0, 0)
	gtk_grid_attach(grid, wid, 1, 2, 0, 1);
#else
	gtk_table_attach (GTK_TABLE (table), wid, 1, 2, 0, 1, GTK_EXPAND | GTK_FILL, GtkAttachOptions(), 0, 0);
#endif
	gtk_widget_show (wid);
	serv->gui->chanlist_wild = wid;

	chanlist_find_cb (wid, serv);

	/* ============================================================= */

	
#if GTK_CHECK_VERSION(3, 0, 0)
	wid = gtk_separator_new(GTK_ORIENTATION_VERTICAL);
	gtk_grid_attach(grid, wid, 2, 3, 0, 5);
#else
	wid = gtk_vseparator_new ();
	gtk_table_attach (GTK_TABLE (table), wid, 2, 3, 0, 5, GTK_SHRINK | GTK_FILL, GTK_SHRINK | GTK_FILL, 0, 0);
#endif
	gtk_widget_show (wid);

	g_signal_connect (G_OBJECT (serv->gui->chanlist_window), "destroy",
							G_CALLBACK (chanlist_destroy_widget), serv->gui);

	/* reset the counters. */
	chanlist_reset_counters (*serv->gui);

	serv->gui->chanlist_tag = g_timeout_add (250, (GSourceFunc)chanlist_timeout, serv);

	if (do_refresh)
		chanlist_do_refresh (serv);

	chanlist_update_buttons (*serv->gui);
	gtk_widget_show (serv->gui->chanlist_window);
	gtk_widget_grab_focus (serv->gui->chanlist_refresh);
}
