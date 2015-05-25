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
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <array>
#include <functional>
#include <numeric>
#include <string>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <ctime>
#include <iomanip>
#include <boost/utility/string_ref.hpp>

#ifndef WIN32
#include <unistd.h>
#endif

#include "fe-gtk.hpp"

#include "../common/hexchat.hpp"
#include "../common/fe.hpp"
#include "../common/modes.hpp"
#include "../common/outbound.hpp"
#include "../common/hexchatc.hpp"
#include "../common/server.hpp"
#include "../common/userlist.hpp"
#include "../common/session.hpp"
#include "gtkutil.hpp"
#include "gtk_helpers.hpp"
#include "maingui.hpp"
#include "banlist.hpp"

namespace {
struct mode_info {
	std::string name;		/* Checkbox name, e.g. "Bans" */
	std::string type;		/* Type for type column, e.g. "Ban" */
	char letter;	/* /mode-command letter, e.g. 'b' for MODE_BAN */
	int code;		/* rfc RPL_foo code, e.g. 367 for RPL_BANLIST */
	int endcode;	/* rfc RPL_ENDOFfoo code, e.g. 368 for RPL_ENDOFBANLIST */
	int bit;			/* Mask bit, e.g., 1<<MODE_BAN  */
	/* Function returns true to set bit into checkable */
	std::function<void(banlist_info&, int)> tester;
};
/*
 * These supports_* routines set capable, readable, writable bits */
static void supports_bans (banlist_info &, int);
static void supports_exempt (banlist_info &, int);
static void supports_invite (banlist_info &, int);
static void supports_quiet (banlist_info &, int);

static const std::array<mode_info, MODE_CT> modes = { {
	{
		N_("Bans"),
		N_("Ban"),
		'b',
		RPL_BANLIST,
		RPL_ENDOFBANLIST,
		1 << MODE_BAN,
		supports_bans
	}
	, {
		N_("Exempts"),
		N_("Exempt"),
		'e',
		RPL_EXCEPTLIST,
		RPL_ENDOFEXCEPTLIST,
		1 << MODE_EXEMPT,
		supports_exempt
	}
	, {
		N_("Invites"),
		N_("Invite"),
		'I',
		RPL_INVITELIST,
		RPL_ENDOFINVITELIST,
		1 << MODE_INVITE,
		supports_invite
	}
	, {
		N_("Quiets"),
		N_("Quiet"),
		'q',
		RPL_QUIETLIST,
		RPL_ENDOFQUIETLIST,
		1 << MODE_QUIET,
		supports_quiet
	}
	} };

/* model for the banlist tree */
enum
{
	TYPE_COLUMN,
	MASK_COLUMN,
	FROM_COLUMN,
	DATE_COLUMN,
	N_COLUMNS
};

static GtkTreeView *
get_view (struct session *sess)
{
	return GTK_TREE_VIEW (sess->res->banlist->treeview);
}

static GtkListStore *
get_store (struct session *sess)
{
	return GTK_LIST_STORE (gtk_tree_view_get_model (get_view (sess)));
}

static void
supports_bans (banlist_info &banl, int i)
{
	int bit = 1<<i;

	banl.capable |= bit;
	banl.readable |= bit;
	banl.writeable |= bit;
	return;
}

static void
supports_exempt (banlist_info &banl, int i)
{
	server *serv = banl.sess->server;
	int bit = 1<<i;

	if (serv->have_except)
		goto yes;

	for(char cm : serv->chanmodes)
	{
		if (cm == ',')
			break;
		if (cm == 'e')
			goto yes;
	}
	return;

yes:
	banl.capable |= bit;
	banl.writeable |= bit;
}

static void
supports_invite (banlist_info &banl, int i)
{
	server *serv = banl.sess->server;
	int bit = 1<<i;

	if (serv->have_invite)
		goto yes;

	for (char cm : serv->chanmodes)
	{
		if (cm == ',')
			break;
		if (cm == 'I')
			goto yes;
	}
	return;

yes:
	banl.capable |= bit;
	banl.writeable |= bit;
}

static void
supports_quiet (banlist_info &banl, int i)
{
	server *serv = banl.sess->server;
	int bit = 1<<i;

	for (char cm : serv->chanmodes)
	{
		if (cm == ',')
			break;
		if (cm == modes[i].letter)
			goto yes;
	}
	return;

yes:
	banl.capable |= bit;
	banl.readable |= bit;
	banl.writeable |= bit;
}
}

/* fe_add_ban_list() and fe_ban_list_end() return true if consumed, false otherwise */
gboolean
fe_add_ban_list (struct session *sess, char *mask, char *who, char *when, int rplcode)
{
	banlist_info *banl = sess->res->banlist;

	if (!banl)
		return false;
	int i;
	GtkListStore *store;
	GtkTreeIter iter;

	for (i = 0; i < MODE_CT; i++)
		if (modes[i].code == rplcode)
			break;
	if (i == MODE_CT)
	{
		/* printf ("Unexpected value in fe_add_ban_list:  %d\n", rplcode); */
		return false;
	}
	if (banl->pending & 1<<i)
	{
		store = get_store (sess);
		gtk_list_store_append (store, &iter);

		gtk_list_store_set (store, &iter, TYPE_COLUMN, _(modes[i].type.c_str()), MASK_COLUMN, mask,
						FROM_COLUMN, who, DATE_COLUMN, when, -1);

		banl->line_ct++;
		return true;
	}
	else return false;
}

/* Sensitize checkboxes and buttons as appropriate for the moment  */
static void
banlist_sensitize (banlist_info *banl)
{
	if (banl->sess->me == nullptr)
		return;

	int checkable;
	bool is_op = false;
	/* FIXME: More access levels than these can unban */
	if (banl->sess->me->op || banl->sess->me->hop)
		is_op = true;

	/* CHECKBOXES -- */
	checkable = is_op? banl->writeable: banl->readable;
	for (int i = 0; i < MODE_CT; i++)
	{
		if (banl->checkboxes[i] == nullptr)
			continue;
		if ((checkable & 1<<i) == 0)
		/* Checkbox is not checkable.  Grey it and uncheck it. */
		{
			gtk_widget_set_sensitive (banl->checkboxes[i], false);
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (banl->checkboxes[i]), false);
		}
		else
		/* Checkbox is checkable.  Be sure it's sensitive. */
		{
			gtk_widget_set_sensitive (banl->checkboxes[i], true);
		}
	}

	/* BUTTONS --- */
	if (!is_op || banl->line_ct == 0)
	{
		/* If user is not op or list is empty, buttons should be all greyed */
		gtk_widget_set_sensitive (banl->but_clear, false);
		gtk_widget_set_sensitive (banl->but_crop, false);
		gtk_widget_set_sensitive (banl->but_remove, false);
	}
	else
	{
		/* If no lines are selected, only the CLEAR button should be sensitive */
		if (banl->select_ct == 0)
		{
			gtk_widget_set_sensitive (banl->but_clear, true);
			gtk_widget_set_sensitive (banl->but_crop, false);
			gtk_widget_set_sensitive (banl->but_remove, false);
		}
		/* If any lines are selected, only the REMOVE and CROP buttons should be sensitive */
		else
		{
			gtk_widget_set_sensitive (banl->but_clear, false);
			gtk_widget_set_sensitive (banl->but_crop, banl->line_ct == banl->select_ct? false: true);
			gtk_widget_set_sensitive (banl->but_remove, true);
		}
	}

	/* Set "Refresh" sensitvity */
	gtk_widget_set_sensitive (banl->but_refresh, banl->pending? false: banl->checked? true: false);
}
/* fe_ban_list_end() returns true if consumed, false otherwise */
gboolean
fe_ban_list_end (struct session *sess, int rplcode)
{
	banlist_info *banl = sess->res->banlist;

	if (!banl)
		return false;

	int i;
	for (i = 0; i < MODE_CT; i++)
		if (modes[i].endcode == rplcode)
			break;
	if (i == MODE_CT)
	{
		/* printf ("Unexpected rplcode value in fe_ban_list_end:  %d\n", rplcode); */
		return false;
	}
	if (banl->pending & modes[i].bit)
	{
		banl->pending &= ~modes[i].bit;
		if (!banl->pending)
		{
			gtk_widget_set_sensitive (banl->but_refresh, true);
			banlist_sensitize (banl);
		}
		return true;
	}
	else return false;
}

static void
banlist_copyentry (GtkWidget *menuitem, GtkTreeView *view)
{
	GtkTreeModel *model;
	GtkTreeSelection *sel;
	GtkTreeIter iter;
	GValue mask = { 0 };
	GValue from = { 0 };
	GValue date = { 0 };
	
	/* get selection (which should have been set on click)
	 * and temporarily switch to single mode to get selected iter */
	sel = gtk_tree_view_get_selection (view);
	gtk_tree_selection_set_mode (sel, GTK_SELECTION_SINGLE);
	if (gtk_tree_selection_get_selected (sel, &model, &iter))
	{
		gtk_tree_model_get_value (model, &iter, MASK_COLUMN, &mask);
		gtk_tree_model_get_value (model, &iter, FROM_COLUMN, &from);
		gtk_tree_model_get_value (model, &iter, DATE_COLUMN, &date);

		/* poor way to get which is selected but it works */
		glib_string str = std::strcmp(_("Copy mask"), gtk_menu_item_get_label(GTK_MENU_ITEM(menuitem))) == 0
			? glib_string(g_value_dup_string(&mask))
			: glib_string(g_strdup_printf(_("%s on %s by %s"), g_value_get_string(&mask),
			g_value_get_string(&date), g_value_get_string(&from)));

		if (str[0] != 0)
			gtkutil_copy_to_clipboard (menuitem, nullptr, str.get());
			
		g_value_unset (&mask);
		g_value_unset (&from);
		g_value_unset (&date);
	}
	gtk_tree_selection_set_mode (sel, GTK_SELECTION_MULTIPLE);
}

static gboolean
banlist_button_pressed (GtkWidget *wid, GdkEventButton *event, gpointer)
{
	/* Check for right click */
	if (event->type == GDK_BUTTON_PRESS && event->button == 3)
	{
		GtkTreePath *path;
		if (gtk_tree_view_get_path_at_pos (GTK_TREE_VIEW (wid), event->x, event->y,
												&path, nullptr, nullptr, nullptr))
		{
			GtkTreePathPtr path_ptr(path);
			/* Must set the row active for use in callback */
			gtk_tree_view_set_cursor (GTK_TREE_VIEW(wid), path, nullptr, false);
			
			auto menu = gtk_menu_new ();
			auto maskitem = gtk_menu_item_new_with_label (_("Copy mask"));
			auto allitem = gtk_menu_item_new_with_label (_("Copy entry"));
			g_signal_connect (maskitem, "activate", G_CALLBACK(banlist_copyentry), wid);
			g_signal_connect (allitem, "activate", G_CALLBACK(banlist_copyentry), wid);
			gtk_menu_shell_append (GTK_MENU_SHELL(menu), maskitem);
			gtk_menu_shell_append (GTK_MENU_SHELL(menu), allitem);
			gtk_widget_show_all (menu);
			
			gtk_menu_popup (GTK_MENU(menu), nullptr, nullptr, nullptr, nullptr, 
							event->button, gtk_get_current_event_time ());
		}
		
		return true;
	}
	
	return false;
}

static void
banlist_select_changed (GtkWidget *item, banlist_info *banl)
{
	if (banl->line_ct == 0)
		banl->select_ct = 0;
	else
	{
		auto list = gtk_tree_selection_get_selected_rows (GTK_TREE_SELECTION (item), nullptr);
		banl->select_ct = g_list_length (list);
		g_list_foreach (list, (GFunc) gtk_tree_path_free, nullptr);
		g_list_free (list);
	}
	banlist_sensitize (banl);
}

/**
 *  * Performs the actual refresh operations.
 *  */
static void
banlist_do_refresh (banlist_info *banl)
{
	session *sess = banl->sess;
	

	banlist_sensitize (banl);

	if (sess->server->connected)
	{
		GtkListStore *store;
		std::ostringstream stream(DISPLAY_NAME": Ban List (", std::ios::ate);
		stream << sess->channel << ", " << sess->server->servername << ")";
		mg_set_title (banl->window, stream.str().c_str());

		store = get_store (sess);
		gtk_list_store_clear (store);
		banl->line_ct = 0;
		banl->pending = banl->checked;
		if (banl->pending)
		{
			for (int i = 0; i < MODE_CT; i++)
				if (banl->pending & 1<<i)
				{
					char tbuf[256];
					g_snprintf (tbuf, sizeof tbuf, "quote mode %s +%c", sess->channel, modes[i].letter);
					handle_command (sess, tbuf, false);
				}
		}
	}
	else
	{
		fe_message (_("Not connected."), FE_MSG_ERROR);
	}
}

static void
banlist_refresh (GtkWidget *, banlist_info *banl)
{
	/* JG NOTE: Didn't see actual use of wid here, so just forwarding
	   *          * this to chanlist_do_refresh because I use it without any widget
	   *          * param in chanlist_build_gui_list when the user presses enter
	   *          * or apply for the first time if the list has not yet been
	   *          * received.
	   *          */
	banlist_do_refresh (banl);
}

static int
banlist_unban_inner (gpointer, banlist_info *banl, const mode_info & mode)
{
	session *sess = banl->sess;
	/* grab the list of selected items */
	auto model = GTK_TREE_MODEL (get_store (sess));
	GtkTreeIter iter;
	if (!gtk_tree_model_get_iter_first (model, &iter))
		return 0;

	auto sel = gtk_tree_view_get_selection(get_view(sess));
	std::vector<std::string> masks;
	masks.reserve(banl->line_ct);
	do
	{
		if (gtk_tree_selection_iter_is_selected (sel, &iter))
		{
			char *mask, *type;
			/* Get the mask part of this selected line */
			gtk_tree_model_get (model, &iter, TYPE_COLUMN, &type, MASK_COLUMN, &mask, -1);
			glib_string mask_ptr(mask);
			glib_string type_ptr(type);
			/* If it's the wrong type of mask, just continue */
			if (std::strcmp (_(mode.type.c_str()), type) != 0)
				continue;

			/* Otherwise add it to our array of mask pointers */
			masks.emplace_back(mask);
		}
	}
	while (gtk_tree_model_iter_next (model, &iter));

	/* and send to server */
	if (!masks.empty())
		send_channel_modes (sess, masks, 0, masks.size(), '-', mode.letter, 0);

	return masks.size();
}

static void
banlist_unban (GtkWidget * wid, banlist_info *banl)
{
	auto num = std::accumulate(
		std::begin(modes),
		std::end(modes),
		0,
		[wid, banl](int init, const mode_info& mode)
		{
			return init + banlist_unban_inner(wid, banl, mode);
		});

	/* This really should not occur with the redesign */
	if (num < 1)
	{
		fe_message (_("You must select some bans."), FE_MSG_ERROR);
		return;
	}

	banlist_do_refresh (banl);
}

static void
banlist_clear_cb (GtkDialog *dialog, gint response, gpointer data)
{
	banlist_info *banl = static_cast<banlist_info*>(data);
	GtkTreeSelection *sel;

	gtk_widget_destroy (GTK_WIDGET (dialog));

	if (response == GTK_RESPONSE_OK)
	{
		sel = gtk_tree_view_get_selection (get_view (banl->sess));
		gtk_tree_selection_select_all (sel);
		banlist_unban (nullptr, banl);
	}
}

static void
banlist_clear (GtkWidget *, banlist_info *banl)
{
	auto dialog = gtk_message_dialog_new (nullptr, static_cast<GtkDialogFlags>(0),
								GTK_MESSAGE_QUESTION, GTK_BUTTONS_OK_CANCEL,
					_("Are you sure you want to remove all listed items in %s?"), banl->sess->channel);

	g_signal_connect (G_OBJECT (dialog), "response",
							G_CALLBACK (banlist_clear_cb), banl);
	gtk_window_set_position (GTK_WINDOW (dialog), GTK_WIN_POS_MOUSE);
	gtk_widget_show (dialog);
}

static void
banlist_add_selected_cb (GtkTreeModel *, GtkTreePath *, GtkTreeIter *iter, gpointer data)
{
	GSList **lp = static_cast<GSList**>(data);
	GSList *list = nullptr;
	
	if (!lp) return;
	list = *lp;
	
	GtkTreeIter *copy = gtk_tree_iter_copy(iter);

	list = g_slist_append (list, copy);
	*(GSList **)data = list;
}

static void
banlist_crop (GtkWidget *, banlist_info *banl)
{
	session *sess = banl->sess;
	GtkTreeSelection *select;
	GSList *list = nullptr, *node;
	int num_sel;

	/* remember which bans are selected */
	select = gtk_tree_view_get_selection (get_view (sess));
	/* gtk_tree_selected_get_selected_rows() isn't present in gtk 2.0.x */
	gtk_tree_selection_selected_foreach (select, banlist_add_selected_cb,
										 &list);

	num_sel = g_slist_length (list);
	/* select all, then unselect those that we remembered */
	if (num_sel)
	{
		gtk_tree_selection_select_all (select);

		for (node = list; node; node = node->next)
			gtk_tree_selection_unselect_iter(select, static_cast<GtkTreeIter*>(node->data));

		g_slist_foreach (list, (GFunc)g_free, nullptr);
		g_slist_free (list);

		banlist_unban (nullptr, banl);
	} else
		fe_message (_("You must select some bans."), FE_MSG_ERROR);
}

static void
banlist_toggle (GtkWidget *item, gpointer data)
{
	banlist_info *banl = static_cast<banlist_info*>(data);
	int bit = 0;

	for (int i = 0; i < MODE_CT; i++)
		if (banl->checkboxes[i] == item)
		{
			bit = 1<<i;
			break;
		}

	if (bit)		/* Should be gassert() */
	{
		banl->checked &= ~bit;
		banl->checked |= (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (item)))? bit: 0;
		banlist_do_refresh (banl);
	}
}

/* NOTICE:  The official strptime() is not available on all platforms so
 * I've implemented a special version here.  The official version is
 * vastly more general than this:  it uses locales for weekday and month
 * names and its second arg is a format character-string.  This special
 * version depends on the format returned by ctime(3) whose manpage
 * says it returns:
 *     "a null-terminated string of the form "Wed Jun 30 21:49:08 1993\n"
 *
 * If the real strpftime() comes available, use this format string:
 *		#define DATE_FORMAT "%a %b %d %T %Y"
 */
//static void
//banlist_strptime (char *ti, struct tm *tm)
//{
//	/* Expect something like "Sat Mar 16 21:24:27 2013" */
//	static char *mon[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
//								  "Jul", "Aug", "Sep", "Oct", "Nov", "Dec", nullptr };
//	int M = -1, d = -1, h = -1, m = -1, s = -1, y = -1;
//
//	if (*ti == 0)
//	{
//		memset (tm, 0, sizeof *tm);
//		return;
//	}
//	/* No need to supply tm->tm_wday; mktime() doesn't read it */
//	ti += 4;
//	while ((mon[++M]))
//		if (strncmp (ti, mon[M], 3) == 0)
//			break;
//	ti += 4;
//
//	d = strtol (ti, &ti, 10);
//	h = strtol (++ti, &ti, 10);
//	m = strtol (++ti, &ti, 10);
//	s = strtol (++ti, &ti, 10);
//	y = strtol (++ti, nullptr, 10) - 1900;
//
//	tm->tm_sec = s;
//	tm->tm_min = m;
//	tm->tm_hour = h;
//	tm->tm_mday = d;
//	tm->tm_mon = M;
//	tm->tm_year = y;
//}

namespace banlist{
static time_t
get_time(const std::string& timestr)
{
	const char DATE_FORMAT[] = "%a %b %d %T %Y";
	std::tm t{};
#if defined(__GNUC__) && (__GNUC__ <= 4 && __GNUC_MINOR__ < 10)
	strptime(timestr.c_str(), DATE_FORMAT, &t);
#else
	std::istringstream buffer(timestr);
	buffer >> std::get_time(&t, DATE_FORMAT);
#endif
	return std::mktime(&t);
}
}
gint
banlist_date_sort (GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b, gpointer)
{
	/*struct tm tm1, tm2;*/
	char *time1, *time2;

	gtk_tree_model_get(model, a, DATE_COLUMN, &time1, -1);
	gtk_tree_model_get(model, b, DATE_COLUMN, &time2, -1);
	/*banlist_strptime (time1, &tm1);
	banlist_strptime (time2, &tm2);*/
	auto t1 = banlist::get_time(time1);
	auto t2 = banlist::get_time(time2);

	if (t1 < t2) return 1;
	if (t1 == t2) return 0;
	return -1;
}

static GtkWidget *
banlist_treeview_new (GtkWidget *box, banlist_info *banl)
{
	GtkListStore *store;
	GtkWidget *view;
	GtkTreeSelection *select;
	GtkTreeViewColumn *col;
	GtkTreeSortable *sortable;

	store = gtk_list_store_new (N_COLUMNS, G_TYPE_STRING, G_TYPE_STRING,
										 G_TYPE_STRING, G_TYPE_STRING);
	g_return_val_if_fail (store != nullptr, nullptr);

	sortable = GTK_TREE_SORTABLE (store);
	gtk_tree_sortable_set_sort_func (sortable, DATE_COLUMN, banlist_date_sort, GINT_TO_POINTER (DATE_COLUMN), nullptr);

	view = gtkutil_treeview_new (box, GTK_TREE_MODEL (store), nullptr,
										  TYPE_COLUMN, _("Type"),
										  MASK_COLUMN, _("Mask"),
										  FROM_COLUMN, _("From"),
										  DATE_COLUMN, _("Date"), -1);
	g_signal_connect (G_OBJECT (view), "button-press-event", G_CALLBACK (banlist_button_pressed), nullptr);

	col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), MASK_COLUMN);
	gtk_tree_view_column_set_alignment (col, 0.5);
	gtk_tree_view_column_set_min_width (col, 100);
	gtk_tree_view_column_set_sort_column_id (col, MASK_COLUMN);
	gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable (col, true);

	col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), TYPE_COLUMN);
	gtk_tree_view_column_set_alignment (col, 0.5);
	gtk_tree_view_column_set_sort_column_id (col, TYPE_COLUMN);

	col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), FROM_COLUMN);
	gtk_tree_view_column_set_alignment (col, 0.5);
	gtk_tree_view_column_set_sort_column_id (col, FROM_COLUMN);
	gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable (col, true);

	col = gtk_tree_view_get_column (GTK_TREE_VIEW (view), DATE_COLUMN);
	gtk_tree_view_column_set_alignment (col, 0.5);
	gtk_tree_view_column_set_sort_column_id (col, DATE_COLUMN);
	gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_AUTOSIZE);
	gtk_tree_view_column_set_resizable (col, true);

	select = gtk_tree_view_get_selection (GTK_TREE_VIEW (view));
	g_signal_connect (G_OBJECT (select), "changed", G_CALLBACK (banlist_select_changed), banl);
	gtk_tree_selection_set_mode (select, GTK_SELECTION_MULTIPLE);

	gtk_widget_show (view);
	return view;
}

static void
banlist_closegui (GtkWidget *, banlist_info *banl)
{
	session *sess = banl->sess;

	if (sess->res->banlist == banl)
	{
		g_free (banl);
		sess->res->banlist = nullptr;
	}
}

void
banlist_opengui (struct session *sess)
{
	banlist_info *banl;
	GtkWidget *table, *vbox, *bbox;
	char tbuf[256];

	if (sess->type != session::SESS_CHANNEL || sess->channel[0] == 0)
	{
		fe_message (_("You can only open the Ban List window while in a channel tab."), FE_MSG_ERROR);
		return;
	}

	if (!sess->res->banlist)
	{
		sess->res->banlist = static_cast<banlist_info*>(g_malloc0 (sizeof (banlist_info)));
		if (!sess->res->banlist)
		{
			fe_message (_("Banlist initialization failed."), FE_MSG_ERROR);
			return;
		}
	}
	banl = sess->res->banlist;
	if (banl->window)
	{
		mg_bring_tofront (banl->window);
		return;
	}

	/* New banlist for this session -- Initialize it */
	banl->sess = sess;
	/* For each mode set its bit in capable/readable/writeable */
	for (int i = 0; i < MODE_CT; i++)
		modes[i].tester (*banl, i);
	/* Force on the checkmark in the "Bans" box */
	banl->checked = 1<<MODE_BAN;

	g_snprintf (tbuf, sizeof tbuf, _(DISPLAY_NAME ": Ban List (%s)"),
					sess->server->servername);

	banl->window = mg_create_generic_tab ("BanList", tbuf, false,
		true, G_CALLBACK(banlist_closegui), banl, 550, 200, &vbox, sess->server);
	gtkutil_destroy_on_esc (banl->window);

	gtk_container_set_border_width (GTK_CONTAINER (banl->window), 3);
	gtk_box_set_spacing (GTK_BOX (vbox), 3);

	/* create banlist view */
	banl->treeview = banlist_treeview_new (vbox, banl);

	table = gtk_table_new (1, MODE_CT, false);
	gtk_table_set_col_spacings (GTK_TABLE (table), 16);
	gtk_box_pack_start (GTK_BOX (vbox), table, 0, 0, 0);

	for (int i = 0; i < MODE_CT; i++)
	{
		if (!(banl->capable & 1<<i))
			continue;
		banl->checkboxes[i] = gtk_check_button_new_with_label (_(modes[i].name.c_str()));
		gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (banl->checkboxes[i]), (banl->checked & 1<<i? true: false));
		g_signal_connect (G_OBJECT (banl->checkboxes[i]), "toggled",
								G_CALLBACK (banlist_toggle), banl);
		gtk_table_attach (GTK_TABLE (table), banl->checkboxes[i], i+1, i+2, 0, 1, GTK_FILL, GTK_FILL, 0, 0);
	}

	bbox = gtk_hbutton_box_new ();
	gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_SPREAD);
	gtk_container_set_border_width (GTK_CONTAINER (bbox), 5);
	gtk_box_pack_end (GTK_BOX (vbox), bbox, 0, 0, 0);
	gtk_widget_show (bbox);

	banl->but_remove = gtkutil_button (bbox, GTK_STOCK_REMOVE, 0, G_CALLBACK(banlist_unban), banl,
					_("Remove"));
	banl->but_crop = gtkutil_button(bbox, GTK_STOCK_REMOVE, 0, G_CALLBACK(banlist_crop), banl,
					_("Crop"));
	banl->but_clear = gtkutil_button(bbox, GTK_STOCK_CLEAR, 0, G_CALLBACK(banlist_clear), banl,
					_("Clear"));

	banl->but_refresh = gtkutil_button(bbox, GTK_STOCK_REFRESH, 0, G_CALLBACK(banlist_refresh), banl, _("Refresh"));

	banlist_do_refresh (banl);

	gtk_widget_show_all (banl->window);
}
