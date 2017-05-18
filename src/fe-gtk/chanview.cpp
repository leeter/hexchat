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

/* abstract channel view: tabs or tree or anything you like */
#include "precompile.hpp"
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <string>
#include <boost/utility/string_ref.hpp>

#include <gdk/gdk.h>

#include "../common/hexchat.hpp"
#include "../common/hexchatc.hpp"
#include "fe-gtk.hpp"
#include "maingui.hpp"
#include "gtkutil.hpp"
#include "chanview.hpp"
#include "gtk_helpers.hpp"

/* treeStore columns */
#define COL_NAME 0		/* (char *) */
#define COL_CHAN 1		/* (chan *) */
#define COL_ATTR 2		/* (PangoAttrList *) */
#define COL_PIXBUF 3		/* (GdkPixbuf *) */

struct chanview
{
	/* impl scratch area */
	char implscratch[sizeof (void *) * 8];

	GtkTreeStore *store;
	int size;			/* number of channels in view */

	GtkWidget *box;	/* the box we destroy when changing implementations */
	GtkStyle *style;	/* style used for tree */
	chan *focused;		/* currently focused channel */
	int trunc_len;

	/* callbacks */
	void (*cb_focus) (chanview *, chan *, int tag, void *userdata);
	void (*cb_xbutton) (chanview *, chan *, int tag, void *userdata);
	gboolean (*cb_contextmenu) (chanview *, chan *, int tag, void *userdata, GdkEventButton *);
	int (*cb_compare) (void *a, void *b);

	/* impl */
	void (*func_init) (chanview *);
	void (*func_postinit) (chanview *);
	void *(*func_add) (chanview *, chan *, const char *, GtkTreeIter *);
	void (*func_move_focus) (chanview *, gboolean, int);
	void (*func_change_orientation) (chanview *);
	void (*func_remove) (chan *);
	void (*func_move) (chan *, int delta);
	void (*func_move_family) (chan *, int delta);
	void (*func_focus) (chan *);
	void (*func_set_color) (chan *, PangoAttrList *);
	void (*func_rename) (chan *, const char[]);
	gboolean (*func_is_collapsed) (chan *);
	chan *(*func_get_parent) (chan *);
	void (*func_cleanup) (chanview *);

	bool sorted;
	bool vertical;
	bool use_icons;
};

struct chan
{
	chanview *cv;	/* our owner */
	GtkTreeIter iter;
	void *userdata;	/* session * */
	void *family;		/* server * or null */
	void *impl;	/* togglebutton or null */
	GdkPixbuf *icon;
	short allow_closure;	/* allow it to be closed when it still has children? */
	short tag;
};

static chan *cv_find_chan_by_number (chanview *cv, int num);
static int cv_find_number_of_chan (chanview *cv, chan *find_ch);


/* ======= TABS ======= */

struct tabview
{
	GtkWidget *outer;	/* outer box */
	GtkWidget *inner;	/* inner box */
	GtkWidget *b1;		/* button1 */
	GtkWidget *b2;		/* button2 */
};

static void chanview_populate(chanview *cv);

/* ignore "toggled" signal? */
static bool ignore_toggle = false;
static bool tab_left_is_moving = false;
static bool tab_right_is_moving = false;

/* userdata for gobjects used here:
*
* tab (togglebuttons inside boxes):
*   "u" userdata passed to tab-focus callback function (sess)
*   "c" the tab's (chan *)
*
* box (family box)
*   "f" family
*
*/

/*
* GtkViewports request at least as much space as their children do.
* If we don't intervene here, the GtkViewport will be granted its
* request, even at the expense of resizing the top-level window.
*/
static void
cv_tabs_sizerequest(GtkWidget *, GtkRequisition *requisition, chanview *cv)
{
	if (!cv->vertical)
		requisition->width = 1;
	else
		requisition->height = 1;
}

static void
cv_tabs_sizealloc(GtkWidget *, GtkAllocation *, chanview *cv)
{
	GtkAdjustment *adj;
	GtkWidget *inner;
	gint viewport_size;

	inner = ((tabview *)cv)->inner;
	GdkWindow *parent_win = gtk_widget_get_window(gtk_widget_get_parent(inner));

	if (cv->vertical)
	{
		adj = gtk_viewport_get_vadjustment(GTK_VIEWPORT(gtk_widget_get_parent(inner)));
		gdk_window_get_geometry(parent_win, nullptr, nullptr, nullptr, &viewport_size, nullptr);
	}
	else
	{
		adj = gtk_viewport_get_hadjustment(GTK_VIEWPORT(gtk_widget_get_parent(inner)));
		gdk_window_get_geometry(parent_win, nullptr, nullptr, &viewport_size, nullptr, nullptr);
	}

	if (gtk_adjustment_get_upper(adj) <= viewport_size)
	{
		gtk_widget_hide(((tabview *)cv)->b1);
		gtk_widget_hide(((tabview *)cv)->b2);
	}
	else
	{
		gtk_widget_show(((tabview *)cv)->b1);
		gtk_widget_show(((tabview *)cv)->b2);
	}
}

static gint
tab_search_offset(GtkWidget *inner, gint start_offset,
gboolean forward, gboolean vertical)
{
	GList *boxes;
	GList *tabs;
	GtkWidget *box;
	GtkWidget *button;
	GtkAllocation allocation;
	gint found;

	boxes = gtk_container_get_children(GTK_CONTAINER(inner));
	if (!forward && boxes)
		boxes = g_list_last(boxes);

	while (boxes)
	{
		box = (GtkWidget *)boxes->data;
		boxes = (forward ? boxes->next : boxes->prev);

		tabs = gtk_container_get_children(GTK_CONTAINER(box));
		if (!forward && tabs)
			tabs = g_list_last(tabs);

		while (tabs)
		{
			button = (GtkWidget *)tabs->data;
			tabs = (forward ? tabs->next : tabs->prev);

			if (!GTK_IS_TOGGLE_BUTTON(button))
				continue;

			gtk_widget_get_allocation(button, &allocation);
			found = (vertical ? allocation.y : allocation.x);
			if ((forward && found > start_offset) ||
				(!forward && found < start_offset))
				return found;
		}
	}

	return 0;
}

static void
tab_scroll_left_up_clicked(GtkWidget *, chanview *cv)
{
	GtkAdjustment *adj;
	gint viewport_size;
	GtkWidget *inner;
	GdkWindow *parent_win;

	inner = ((tabview *)cv)->inner;
	parent_win = gtk_widget_get_window(gtk_widget_get_parent(inner));

	if (cv->vertical)
	{
		adj = gtk_viewport_get_vadjustment(GTK_VIEWPORT(gtk_widget_get_parent(inner)));
		gdk_window_get_geometry(parent_win, 0, 0, 0, &viewport_size, 0);
	}
	else
	{
		adj = gtk_viewport_get_hadjustment(GTK_VIEWPORT(gtk_widget_get_parent(inner)));
		gdk_window_get_geometry(parent_win, 0, 0, &viewport_size, 0, 0);
	}

	gdouble new_value = tab_search_offset(inner, gtk_adjustment_get_value(adj), FALSE, cv->vertical);

	if (new_value + viewport_size > gtk_adjustment_get_upper(adj))
		new_value = gtk_adjustment_get_upper(adj) - viewport_size;

	if (!tab_left_is_moving)
	{
		tab_left_is_moving = true;

		for (auto i = gtk_adjustment_get_value(adj); ((i > new_value) && (tab_left_is_moving)); i -= 0.1)
		{
			gtk_adjustment_set_value(adj, i);
			while (g_main_context_pending(nullptr))
				g_main_context_iteration(nullptr, TRUE);
		}

		gtk_adjustment_set_value(adj, new_value);

		tab_left_is_moving = false;		/* hSP: set to false in case we didnt get stopped (the normal case) */
	}
	else
	{
		tab_left_is_moving = false;		/* hSP: jump directly to next element if user is clicking faster than we can scroll.. */
	}
}

static void
tab_scroll_right_down_clicked(GtkWidget * /*widget*/, chanview *cv)
{
	GtkAdjustment *adj;
	gint viewport_size;
	GtkWidget *inner;
	GdkWindow *parent_win;

	inner = ((tabview *)cv)->inner;
	parent_win = gtk_widget_get_window(gtk_widget_get_parent(inner));

	if (cv->vertical)
	{
		adj = gtk_viewport_get_vadjustment(GTK_VIEWPORT(gtk_widget_get_parent(inner)));
		gdk_window_get_geometry(parent_win, 0, 0, 0, &viewport_size, 0);
	}
	else
	{
		adj = gtk_viewport_get_hadjustment(GTK_VIEWPORT(gtk_widget_get_parent(inner)));
		gdk_window_get_geometry(parent_win, 0, 0, &viewport_size, 0, 0);
	}

	gdouble new_value = tab_search_offset(inner, gtk_adjustment_get_value(adj), TRUE, cv->vertical);

	if (new_value == 0.0 || new_value + viewport_size > gtk_adjustment_get_upper(adj))
		new_value = gtk_adjustment_get_upper(adj) - viewport_size;

	if (!tab_right_is_moving)
	{
		tab_right_is_moving = true;

		for (auto i = gtk_adjustment_get_value(adj); ((i < new_value) && (tab_right_is_moving)); i += 0.1)
		{
			gtk_adjustment_set_value(adj, i);
			while (g_main_context_pending(nullptr))
				g_main_context_iteration(nullptr, TRUE);
		}

		gtk_adjustment_set_value(adj, new_value);

		tab_right_is_moving = false;		/* hSP: set to false in case we didnt get stopped (the normal case) */
	}
	else
	{
		tab_right_is_moving = false;		/* hSP: jump directly to next element if user is clicking faster than we can scroll.. */
	}
}

static gboolean
tab_scroll_cb(GtkWidget *widget, GdkEventScroll *event, gpointer cv)
{
	if (prefs.hex_gui_tab_scrollchans)
	{
		if (event->direction == GDK_SCROLL_DOWN)
			mg_switch_page(1, 1);
		else if (event->direction == GDK_SCROLL_UP)
			mg_switch_page(1, -1);
	}
	else
	{
		chanview * view = static_cast<chanview*>(cv);
		/* mouse wheel scrolling */
		if (event->direction == GDK_SCROLL_UP)
			tab_scroll_left_up_clicked(widget, view);
		else if (event->direction == GDK_SCROLL_DOWN)
			tab_scroll_right_down_clicked(widget, view);
	}

	return FALSE;
}

static void
cv_tabs_xclick_cb(GtkWidget * /*button*/, chanview *cv)
{
	cv->cb_xbutton(cv, cv->focused, cv->focused->tag, cv->focused->userdata);
}

/* make a Scroll (arrow) button */

static GtkWidget *
make_sbutton(GtkArrowType type, GSourceFunc click_cb, void *userdata)
{
	GtkWidget *button, *arrow;

	button = gtk_button_new();
	arrow = gtk_arrow_new(type, GTK_SHADOW_NONE);
	gtk_container_add(GTK_CONTAINER(button), arrow);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	g_signal_connect(G_OBJECT(button), "clicked",
		G_CALLBACK(click_cb), userdata);
	g_signal_connect(G_OBJECT(button), "scroll_event",
		G_CALLBACK(tab_scroll_cb), userdata);
	gtk_widget_show(arrow);

	return button;
}

static void
cv_tabs_init(chanview *cv)
{
	GtkWidget *box, *hbox = nullptr;
	GtkWidget *viewport;
	GtkWidget *outer;
	GtkWidget *button;

	if (cv->vertical)
		outer = gtk_vbox_new(0, 0);
	else
		outer = gtk_hbox_new(0, 0);
	((tabview *)cv)->outer = outer;
	g_signal_connect(G_OBJECT(outer), "size_allocate",
		G_CALLBACK(cv_tabs_sizealloc), cv);
	/*	gtk_container_set_border_width (GTK_CONTAINER (outer), 2);*/
	gtk_widget_show(outer);

	viewport = gtk_viewport_new(0, 0);
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(viewport), GTK_SHADOW_NONE);
	g_signal_connect(G_OBJECT(viewport), "size_request",
		G_CALLBACK(cv_tabs_sizerequest), cv);
	g_signal_connect(G_OBJECT(viewport), "scroll_event",
		G_CALLBACK(tab_scroll_cb), cv);
	gtk_box_pack_start(GTK_BOX(outer), viewport, 1, 1, 0);
	gtk_widget_show(viewport);

	if (cv->vertical)
		box = gtk_vbox_new(FALSE, 0);
	else
		box = gtk_hbox_new(FALSE, 0);
	((tabview *)cv)->inner = box;
	gtk_container_add(GTK_CONTAINER(viewport), box);
	gtk_widget_show(box);

	/* if vertical, the buttons can be side by side */
	if (cv->vertical)
	{
		hbox = gtk_hbox_new(FALSE, 0);
		gtk_box_pack_start(GTK_BOX(outer), hbox, 0, 0, 0);
		gtk_widget_show(hbox);
	}

	/* make the Scroll buttons */
	((tabview *)cv)->b2 = make_sbutton(cv->vertical ?
	GTK_ARROW_UP : GTK_ARROW_LEFT,
				   (GSourceFunc)tab_scroll_left_up_clicked,
				   cv);

	((tabview *)cv)->b1 = make_sbutton(cv->vertical ?
	GTK_ARROW_DOWN : GTK_ARROW_RIGHT,
					 (GSourceFunc)tab_scroll_right_down_clicked,
					 cv);

	if (hbox)
	{
		gtk_container_add(GTK_CONTAINER(hbox), ((tabview *)cv)->b2);
		gtk_container_add(GTK_CONTAINER(hbox), ((tabview *)cv)->b1);
	}
	else
	{
		gtk_box_pack_start(GTK_BOX(outer), ((tabview *)cv)->b2, 0, 0, 0);
		gtk_box_pack_start(GTK_BOX(outer), ((tabview *)cv)->b1, 0, 0, 0);
	}

	button = gtkutil_button(outer, GTK_STOCK_CLOSE, nullptr, G_CALLBACK(cv_tabs_xclick_cb),
		cv, 0);
	gtk_button_set_relief(GTK_BUTTON(button), GTK_RELIEF_NONE);
	gtk_widget_set_can_focus(button, FALSE);

	gtk_container_add(GTK_CONTAINER(cv->box), outer);
}

static void
cv_tabs_postinit(chanview * /*cv*/)
{
}

static void
tab_add_sorted(chanview *cv, GtkWidget *box, GtkWidget *tab, chan *ch)
{
	if (!cv->sorted)
	{
		gtk_box_pack_start(GTK_BOX(box), tab, 0, 0, 0);
		gtk_widget_show(tab);
		return;
	}

	/* sorting TODO:
	*   - move tab if renamed (dialogs) */

	/* userdata, passed to mg_tabs_compare() */
	auto b = ch->userdata;
	int i = 0;
	auto list = gtk_container_get_children(GTK_CONTAINER(box));
	while (list)
	{
		auto child = static_cast<GtkWidget*>(list->data);
		if (!GTK_IS_SEPARATOR(child))
		{
			void *a = g_object_get_data(G_OBJECT(child), "u");

			if (ch->tag == 0 && cv->cb_compare(a, b) > 0)
			{
				gtk_box_pack_start(GTK_BOX(box), tab, 0, 0, 0);
				gtk_box_reorder_child(GTK_BOX(box), tab, ++i);
				gtk_widget_show(tab);
				return;
			}
		}
		i++;
		list = list->next;
	}

	/* append */
	gtk_box_pack_start(GTK_BOX(box), tab, 0, 0, 0);
	gtk_box_reorder_child(GTK_BOX(box), tab, i);
	gtk_widget_show(tab);
}

/* remove empty boxes and separators */

static void
cv_tabs_prune(chanview *cv)
{
	GList *boxes, *children;
	GtkWidget *inner;

	inner = ((tabview *)cv)->inner;
	boxes = gtk_container_get_children(GTK_CONTAINER(inner));
	while (boxes)
	{
		auto child = static_cast<GtkWidget*>(boxes->data);
		auto box = child;
		boxes = boxes->next;

		/* check if the box is empty (except a vseperator) */
		bool empty = true;
		children = gtk_container_get_children(GTK_CONTAINER(box));
		while (children)
		{
			if (!GTK_IS_SEPARATOR((GtkWidget *)children->data))
			{
				empty = false;
				break;
			}
			children = children->next;
		}

		if (empty)
			gtk_widget_destroy(box);
	}
}

static void
tab_add_real(chanview *cv, GtkWidget *tab, chan *ch)
{
	GList *boxes, *children;
	GtkWidget *sep, *box, *inner;
	GtkWidget *child;

	inner = ((tabview *)cv)->inner;
	/* see if a family for this tab already exists */
	boxes = gtk_container_get_children(GTK_CONTAINER(inner));
	while (boxes)
	{
		child = static_cast<GtkWidget*>(boxes->data);
		box = child;

		if (g_object_get_data(G_OBJECT(box), "f") == ch->family)
		{
			tab_add_sorted(cv, box, tab, ch);
			gtk_widget_queue_resize(gtk_widget_get_parent(inner));
			return;
		}

		boxes = boxes->next;

		/* check if the box is empty (except a vseperator) */
		bool empty = true;
		children = gtk_container_get_children(GTK_CONTAINER(box));
		while (children)
		{
			if (!GTK_IS_SEPARATOR((GtkWidget *)children->data))
			{
				empty = false;
				break;
			}
			children = children->next;
		}

		if (empty)
			gtk_widget_destroy(box);
	}

	/* create a new family box */
	if (cv->vertical)
	{
		/* vertical */
		box = gtk_vbox_new(FALSE, 0);
		sep = gtk_hseparator_new();
	}
	else
	{
		/* horiz */
		box = gtk_hbox_new(FALSE, 0);
		sep = gtk_vseparator_new();
	}

	gtk_box_pack_end(GTK_BOX(box), sep, 0, 0, 4);
	gtk_widget_show(sep);
	gtk_box_pack_start(GTK_BOX(inner), box, 0, 0, 0);
	g_object_set_data(G_OBJECT(box), "f", ch->family);
	gtk_box_pack_start(GTK_BOX(box), tab, 0, 0, 0);
	gtk_widget_show(tab);
	gtk_widget_show(box);
	gtk_widget_queue_resize(gtk_widget_get_parent(inner));
}

static gboolean
tab_ignore_cb(GtkWidget * /*widget*/, GdkEventCrossing * /*event*/, gpointer /*user_data*/)
{
	return TRUE;
}

/* called when a tab is clicked (button down) */

static void
tab_pressed_cb(GtkToggleButton *tab, chan *ch)
{
	chan *old_tab;
	bool is_switching = true;
	chanview *cv = ch->cv;

	ignore_toggle = true;
	/* de-activate the old tab */
	old_tab = cv->focused;
	if (old_tab && old_tab->impl)
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(old_tab->impl), FALSE);
		if (old_tab == ch)
			is_switching = false;
	}
	gtk_toggle_button_set_active(tab, TRUE);
	ignore_toggle = false;
	cv->focused = ch;

	if (/*tab->active*/is_switching)
		/* call the focus callback */
		cv->cb_focus(cv, ch, ch->tag, ch->userdata);
}

/* called for keyboard tab toggles only */
static void
tab_toggled_cb(GtkToggleButton *tab, chan *ch)
{
	if (ignore_toggle)
		return;

	/* activated a tab via keyboard */
	tab_pressed_cb(tab, ch);
}

static gboolean
tab_click_cb(GtkWidget * /*wid*/, GdkEventButton *event, chan *ch)
{
	return ch->cv->cb_contextmenu(ch->cv, ch, ch->tag, ch->userdata, event);
}

static void *
cv_tabs_add(chanview *cv, chan *ch, const char *name, GtkTreeIter * /*parent*/)
{
	GtkWidget *but;

	but = gtk_toggle_button_new_with_label(name);
	gtk_widget_set_name(but, "hexchat-tab");
	g_object_set_data(G_OBJECT(but), "c", ch);
	/* used to trap right-clicks */
	g_signal_connect(G_OBJECT(but), "button_press_event",
		G_CALLBACK(tab_click_cb), ch);
	/* avoid prelights */
	g_signal_connect(G_OBJECT(but), "enter_notify_event",
		G_CALLBACK(tab_ignore_cb), nullptr);
	g_signal_connect(G_OBJECT(but), "leave_notify_event",
		G_CALLBACK(tab_ignore_cb), nullptr);
	g_signal_connect(G_OBJECT(but), "pressed",
		G_CALLBACK(tab_pressed_cb), ch);
	/* for keyboard */
	g_signal_connect(G_OBJECT(but), "toggled",
		G_CALLBACK(tab_toggled_cb), ch);
	g_object_set_data(G_OBJECT(but), "u", ch->userdata);

	tab_add_real(cv, but, ch);

	return but;
}

/* traverse all the family boxes of tabs
*
* A "group" is basically:
* GtkV/HBox
* `-GtkViewPort
*   `-GtkV/HBox (inner box)
*     `- GtkBox (family box)
*        `- GtkToggleButton
*        `- GtkToggleButton
*        `- ...
*     `- GtkBox
*        `- GtkToggleButton
*        `- GtkToggleButton
*        `- ...
*     `- ...
*
* */

static int
tab_group_for_each_tab(chanview *cv,
const std::function<int(GtkWidget *tab, int num, int usernum)> & callback,
int usernum)
{
	GtkBox *innerbox = (GtkBox *)((tabview *)cv)->inner;
	auto boxes = gtk_container_get_children(GTK_CONTAINER(innerbox));
	int i = 0;
	while (boxes)
	{
		auto child = static_cast<GtkWidget*>(boxes->data);
		auto tabs = gtk_container_get_children(GTK_CONTAINER(child));

		while (tabs)
		{
			child = static_cast<GtkWidget*>(tabs->data);

			if (!GTK_IS_SEPARATOR(child))
			{
				if (callback(child, i, usernum) != -1)
					return i;
				i++;
			}
			tabs = tabs->next;
		}

		boxes = boxes->next;
	}

	return i;
}

static int
tab_check_focus_cb(GtkWidget *tab, int num, int /*unused*/)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tab)))
		return num;

	return -1;
}

/* returns the currently focused tab number */

static int
tab_group_get_cur_page(chanview *cv)
{
	return tab_group_for_each_tab(cv, tab_check_focus_cb, 0);
}

static void
cv_tabs_focus(chan *ch)
{
	if (ch->impl)
		/* focus the new one (tab_pressed_cb defocuses the old one) */
		tab_pressed_cb(GTK_TOGGLE_BUTTON(ch->impl), ch);
}

static int
tab_focus_num_cb(GtkWidget *tab, int num, int want)
{
	if (num == want)
	{
		cv_tabs_focus(static_cast<chan*>(g_object_get_data(G_OBJECT(tab), "c")));
		return 1;
	}

	return -1;
}

static void
cv_tabs_change_orientation(chanview *cv)
{
	/* cleanup the old one */
	if (cv->func_cleanup)
		cv->func_cleanup(cv);

	/* now rebuild a new tabbar or tree */
	cv->func_init(cv);
	chanview_populate(cv);
}

/* switch to the tab number specified */

static void
cv_tabs_move_focus(chanview *cv, gboolean relative, int num)
{
	if (relative)
	{
		auto max = cv->size;
		auto i = tab_group_get_cur_page(cv) + num;
		/* make it wrap around at both ends */
		if (i < 0)
			i = max - 1;
		if (i >= max)
			i = 0;
		tab_group_for_each_tab(cv, tab_focus_num_cb, i);
		return;
	}

	tab_group_for_each_tab(cv, tab_focus_num_cb, num);
}

static void
cv_tabs_remove(chan *ch)
{
	gtk_widget_destroy(static_cast<GtkWidget*>(ch->impl));
	ch->impl = nullptr;

	cv_tabs_prune(ch->cv);
}

static void
cv_tabs_move(chan *ch, int delta)
{
	int i = 0;
	int pos = 0;
	GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(ch->impl));

	for (auto list = gtk_container_get_children(GTK_CONTAINER(parent)); list; list = list->next)
	{
		GtkWidget *child_entry = static_cast<GtkWidget*>(list->data);
		if (child_entry == ch->impl)
			pos = i;

		/* keep separator at end to not throw off our count */
		if (GTK_IS_SEPARATOR(child_entry))
			gtk_box_reorder_child(GTK_BOX(parent), child_entry, -1);
		else
			i++;
	}

	pos = (pos - delta) % i;
	gtk_box_reorder_child(GTK_BOX(parent), static_cast<GtkWidget*>(ch->impl), pos);
}

static void
cv_tabs_move_family(chan *ch, int delta)
{
	int pos = 0;
	GtkWidget *box = nullptr;

	/* find position of tab's family */
	int i = 0;
	for (auto list = gtk_container_get_children(GTK_CONTAINER(((tabview *)ch->cv)->inner)); list; list = list->next)
	{
		GtkWidget *child_entry;
		void *fam;

		child_entry = static_cast<GtkWidget*>(list->data);
		fam = g_object_get_data(G_OBJECT(child_entry), "f");
		if (fam == ch->family)
		{
			box = child_entry;
			pos = i;
		}
		i++;
	}

	pos = (pos - delta) % i;
	gtk_box_reorder_child(GTK_BOX(gtk_widget_get_parent(box)), box, pos);
}

static void
cv_tabs_cleanup(chanview *cv)
{
	if (cv->box)
		gtk_widget_destroy(((tabview *)cv)->outer);
}

static void
cv_tabs_set_color(chan *ch, PangoAttrList *list)
{
	gtk_label_set_attributes(GTK_LABEL(gtk_bin_get_child(GTK_BIN(ch->impl))), list);
}

static void
cv_tabs_rename(chan *ch, const char name[])
{
	PangoAttrList *attr;
	GtkWidget *tab = static_cast<GtkWidget*>(ch->impl);

	attr = gtk_label_get_attributes(GTK_LABEL(gtk_bin_get_child(GTK_BIN(tab))));
	if (attr)
		pango_attr_list_ref(attr);

	gtk_button_set_label(GTK_BUTTON(tab), name);
	gtk_widget_queue_resize(gtk_widget_get_parent(gtk_widget_get_parent(gtk_widget_get_parent(tab))));

	if (attr)
	{
		gtk_label_set_attributes(GTK_LABEL(gtk_bin_get_child(GTK_BIN(tab))), attr);
		pango_attr_list_unref(attr);
	}
}

static gboolean
cv_tabs_is_collapsed(chan * /*ch*/)
{
	return FALSE;
}

static chan *
cv_tabs_get_parent(chan * /*ch*/)
{
	return nullptr;
}


/* ======= TREE ======= */

struct treeview
{
	GtkTreeView *tree;
	GtkWidget *scrollw;	/* scrolledWindow */
};



static void 	/* row-activated, when a row is double clicked */
cv_tree_activated_cb(GtkTreeView *view, GtkTreePath *path,
GtkTreeViewColumn * /*column*/, gpointer /*data*/)
{
	if (gtk_tree_view_row_expanded(view, path))
		gtk_tree_view_collapse_row(view, path);
	else
		gtk_tree_view_expand_row(view, path, FALSE);
}

static void		/* row selected callback */
cv_tree_sel_cb(GtkTreeSelection *sel, chanview *cv)
{
	GtkTreeModel *model;
	GtkTreeIter iter;
	chan *ch;

	if (gtk_tree_selection_get_selected(sel, &model, &iter))
	{
		gtk_tree_model_get(model, &iter, COL_CHAN, &ch, -1);

		cv->focused = ch;
		cv->cb_focus(cv, ch, ch->tag, ch->userdata);
	}
}

static gboolean
cv_tree_click_cb(GtkTreeView *tree, GdkEventButton *event, chanview *cv)
{
	chan *ch;
	GtkTreePath *path;
	GtkTreeIter iter;
	int ret = FALSE;

	if (gtk_tree_view_get_path_at_pos(tree, event->x, event->y, &path, 0, 0, 0))
	{
		if (gtk_tree_model_get_iter(GTK_TREE_MODEL(cv->store), &iter, path))
		{
			gtk_tree_model_get(GTK_TREE_MODEL(cv->store), &iter, COL_CHAN, &ch, -1);
			ret = cv->cb_contextmenu(cv, ch, ch->tag, ch->userdata, event);
		}
		gtk_tree_path_free(path);
	}
	return ret;
}

static gboolean
cv_tree_scroll_event_cb(GtkWidget * /*widget*/, GdkEventScroll *event, gpointer /*user_data*/)
{
	if (prefs.hex_gui_tab_scrollchans)
	{
		if (event->direction == GDK_SCROLL_DOWN)
			mg_switch_page(1, 1);
		else if (event->direction == GDK_SCROLL_UP)
			mg_switch_page(1, -1);

		return TRUE;
	}

	return FALSE;
}

static void
cv_tree_init(chanview *cv)
{
	GtkWidget *view, *win;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *col;
	int wid1, wid2;
	static const GtkTargetEntry dnd_src_target[] =
	{
		{ "HEXCHAT_CHANVIEW", GTK_TARGET_SAME_APP, 75 }
	};
	static const GtkTargetEntry dnd_dest_target[] =
	{
		{ "HEXCHAT_USERLIST", GTK_TARGET_SAME_APP, 75 }
	};

	win = gtk_scrolled_window_new(0, 0);
	/*gtk_container_set_border_width (GTK_CONTAINER (win), 1);*/
	gtk_scrolled_window_set_shadow_type(GTK_SCROLLED_WINDOW(win),
		GTK_SHADOW_IN);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(win),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(cv->box), win);
	gtk_widget_show(win);

	view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(cv->store));
	gtk_widget_set_name(view, "hexchat-tree");
	if (cv->style)
		gtk_widget_set_style(view, cv->style);
	/*gtk_widget_modify_base (view, GTK_STATE_NORMAL, &colors[COL_BG]);*/
	gtk_widget_set_can_focus(view, FALSE);
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view), FALSE);

	if (prefs.hex_gui_tab_dots)
	{
		gtk_tree_view_set_enable_tree_lines(GTK_TREE_VIEW(view), TRUE);
	}

	/* Indented channels with no server looks silly, but we still want expanders */
	if (!prefs.hex_gui_tab_server)
	{
		gtk_widget_style_get(view, "expander-size", &wid1, "horizontal-separator", &wid2, nullptr);
		gtk_tree_view_set_level_indentation(GTK_TREE_VIEW(view), -wid1 - wid2);
	}


	gtk_container_add(GTK_CONTAINER(win), view);
	col = gtk_tree_view_column_new();

	/* icon column */
	if (cv->use_icons)
	{
		renderer = gtk_cell_renderer_pixbuf_new();
		if (prefs.hex_gui_compact)
			g_object_set(G_OBJECT(renderer), "ypad", 0, nullptr);

		gtk_tree_view_column_pack_start(col, renderer, FALSE);
		gtk_tree_view_column_set_attributes(col, renderer, "pixbuf", COL_PIXBUF, nullptr);
	}

	/* main column */
	renderer = gtk_cell_renderer_text_new();
	if (prefs.hex_gui_compact)
		g_object_set(G_OBJECT(renderer), "ypad", 0, nullptr);
	gtk_cell_renderer_text_set_fixed_height_from_font(GTK_CELL_RENDERER_TEXT(renderer), 1);
	gtk_tree_view_column_pack_start(col, renderer, TRUE);
	gtk_tree_view_column_set_attributes(col, renderer, "text", COL_NAME, "attributes", COL_ATTR, nullptr);
	gtk_tree_view_append_column(GTK_TREE_VIEW(view), col);

	g_signal_connect(G_OBJECT(gtk_tree_view_get_selection(GTK_TREE_VIEW(view))),
		"changed", G_CALLBACK(cv_tree_sel_cb), cv);
	g_signal_connect(G_OBJECT(view), "button-press-event",
		G_CALLBACK(cv_tree_click_cb), cv);
	g_signal_connect(G_OBJECT(view), "row-activated",
		G_CALLBACK(cv_tree_activated_cb), nullptr);
	g_signal_connect(G_OBJECT(view), "scroll_event",
		G_CALLBACK(cv_tree_scroll_event_cb), nullptr);

	gtk_drag_dest_set(view, GTK_DEST_DEFAULT_ALL, dnd_dest_target, 1,
		static_cast<GdkDragAction>(GDK_ACTION_MOVE | GDK_ACTION_COPY | GDK_ACTION_LINK));
	gtk_drag_source_set(view, GDK_BUTTON1_MASK, dnd_src_target, 1, GDK_ACTION_COPY);

	g_signal_connect(G_OBJECT(view), "drag_begin",
		G_CALLBACK(mg_drag_begin_cb), nullptr);
	g_signal_connect(G_OBJECT(view), "drag_drop",
		G_CALLBACK(mg_drag_drop_cb), nullptr);
	g_signal_connect(G_OBJECT(view), "drag_motion",
		G_CALLBACK(mg_drag_motion_cb), nullptr);
	g_signal_connect(G_OBJECT(view), "drag_end",
		G_CALLBACK(mg_drag_end_cb), nullptr);

	((treeview *)cv)->tree = GTK_TREE_VIEW(view);
	((treeview *)cv)->scrollw = win;
	gtk_widget_show(view);
}

static void
cv_tree_postinit(chanview *cv)
{
	gtk_tree_view_expand_all(((treeview *)cv)->tree);
}

static void *
cv_tree_add(chanview *cv, chan * /*ch*/, const char * /*name*/, GtkTreeIter *parent)
{
	GtkTreePath *path;

	if (parent)
	{
		/* expand the parent node */
		path = gtk_tree_model_get_path(GTK_TREE_MODEL(cv->store), parent);
		if (path)
		{
			gtk_tree_view_expand_row(((treeview *)cv)->tree, path, FALSE);
			gtk_tree_path_free(path);
		}
	}

	return nullptr;
}

static void
cv_tree_change_orientation(chanview *)
{
}

static void
cv_tree_focus(chan *ch)
{
	GtkTreeView *tree = ((treeview *)ch->cv)->tree;
	GtkTreeModel *model = gtk_tree_view_get_model(tree);
	GtkTreeIter parent;
	GdkRectangle cell_rect;
	GdkRectangle vis_rect;
	gint dest_y;

	/* expand the parent node */
	if (gtk_tree_model_iter_parent(model, &parent, &ch->iter))
	{
		GtkTreePathPtr path(gtk_tree_model_get_path(model, &parent));
		if (path)
		{
			/*if (!gtk_tree_view_row_expanded (tree, path))
			{
			gtk_tree_path_free (path);
			return;
			}*/
			gtk_tree_view_expand_row(tree, path.get(), FALSE);
		}
	}

	GtkTreePathPtr path(gtk_tree_model_get_path(model, &ch->iter));
	if (path)
	{
		/* This full section does what
		* gtk_tree_view_scroll_to_cell (tree, path, nullptr, TRUE, 0.5, 0.5);
		* does, except it only scrolls the window if the provided cell is
		* not visible. Basic algorithm taken from gtktreeview.c */

		/* obtain information to see if the cell is visible */
		gtk_tree_view_get_background_area(tree, path.get(), nullptr, &cell_rect);
		gtk_tree_view_get_visible_rect(tree, &vis_rect);

		/* The cordinates aren't offset correctly */
		gtk_tree_view_convert_widget_to_bin_window_coords(tree, cell_rect.x, cell_rect.y, nullptr, &cell_rect.y);

		/* only need to scroll if out of bounds */
		if (cell_rect.y < vis_rect.y ||
			cell_rect.y + cell_rect.height > vis_rect.y + vis_rect.height)
		{
			dest_y = cell_rect.y - ((vis_rect.height - cell_rect.height)  * 0.5);
			if (dest_y < 0)
				dest_y = 0;
			gtk_tree_view_scroll_to_point(tree, -1, dest_y);
		}
		/* theft done, now make it focused like */
		gtk_tree_view_set_cursor(tree, path.get(), nullptr, FALSE);
	}
}

static void
cv_tree_move_focus(chanview *cv, gboolean relative, int num)
{
	chan *ch;

	if (relative)
	{
		num += cv_find_number_of_chan(cv, cv->focused);
		num %= cv->size;
		/* make it wrap around at both ends */
		if (num < 0)
			num = cv->size - 1;
	}

	ch = cv_find_chan_by_number(cv, num);
	if (ch)
		cv_tree_focus(ch);
}

static void
cv_tree_remove(chan *)
{
}

static void
move_row(chan *ch, int delta, GtkTreeIter * /*parent*/)
{
	GtkTreeStore *store = ch->cv->store;
	GtkTreeIter *src = &ch->iter;
	GtkTreeIter dest = ch->iter;

	if (delta < 0) /* down */
	{
		if (gtk_tree_model_iter_next(GTK_TREE_MODEL(store), &dest))
			gtk_tree_store_swap(store, src, &dest);
		else	/* move to top */
			gtk_tree_store_move_after(store, src, nullptr);

	}
	else
	{
		GtkTreePathPtr dest_path(gtk_tree_model_get_path(GTK_TREE_MODEL(store), &dest));
		if (gtk_tree_path_prev(dest_path.get()))
		{
			gtk_tree_model_get_iter(GTK_TREE_MODEL(store), &dest, dest_path.get());
			gtk_tree_store_swap(store, src, &dest);
		}
		else
		{	/* move to bottom */
			gtk_tree_store_move_before(store, src, nullptr);
		}
	}
}

static void
cv_tree_move(chan *ch, int delta)
{
	GtkTreeIter parent;

	/* do nothing if this is a server row */
	if (gtk_tree_model_iter_parent(GTK_TREE_MODEL(ch->cv->store), &parent, &ch->iter))
		move_row(ch, delta, &parent);
}

static void
cv_tree_move_family(chan *ch, int delta)
{
	move_row(ch, delta, nullptr);
}

static void
cv_tree_cleanup(chanview *cv)
{
	if (cv->box)
		/* kill the scrolled window */
		gtk_widget_destroy(((treeview *)cv)->scrollw);
}

static void
cv_tree_set_color(chan *, PangoAttrList *)
{
	/* nothing to do, it's already set in the store */
}

static void
cv_tree_rename(chan *, const char[])
{
	/* nothing to do, it's already renamed in the store */
}

static chan *
cv_tree_get_parent(chan *ch)
{
	chan *parent_ch = nullptr;
	GtkTreeIter parent;

	if (gtk_tree_model_iter_parent(GTK_TREE_MODEL(ch->cv->store), &parent, &ch->iter))
	{
		gtk_tree_model_get(GTK_TREE_MODEL(ch->cv->store), &parent, COL_CHAN, &parent_ch, -1);
	}

	return parent_ch;
}

static gboolean
cv_tree_is_collapsed(chan *ch)
{
	chan *parent = cv_tree_get_parent(ch);

	if (parent == nullptr)
		return FALSE;

	GtkTreePathPtr path( gtk_tree_model_get_path(GTK_TREE_MODEL(parent->cv->store),
		&parent->iter));
	gboolean ret = !gtk_tree_view_row_expanded(((treeview *)parent->cv)->tree, path.get());

	return ret;
}


/* ==== ABSTRACT CHANVIEW ==== */

static std::string truncate_tab_name (const boost::string_ref & name, int max)
{
	if (max > 2 && g_utf8_strlen (name.data(), name.size()) > max)
	{
		/* truncate long channel names */
		std::string buf = name.to_string();
		buf.reserve(name.size() + 4);
		const char* offset = g_utf8_offset_to_pointer (buf.c_str(), max);
		buf.erase(buf.begin() + std::distance(buf.c_str(), offset), buf.end());
		buf.append("..");
		return buf;
	}

	return name.to_string();
}

/* iterate through a model, into 1 depth of children */

static void
model_foreach_1 (GtkTreeModel *model, std::function<void(GtkTreeIter*)> func)
{
	GtkTreeIter iter, inner;

	if (gtk_tree_model_get_iter_first (model, &iter))
	{
		do
		{
			func (&iter);
			if (gtk_tree_model_iter_children (model, &inner, &iter))
			{
				do
					func (&inner);
				while (gtk_tree_model_iter_next (model, &inner));
			}
		}
		while (gtk_tree_model_iter_next (model, &iter));
	}
}

static void
chanview_pop_cb (chanview *cv, GtkTreeIter *iter)
{
	chan *ch;
	char *name;
	PangoAttrList *attr;

	gtk_tree_model_get (GTK_TREE_MODEL (cv->store), iter,
							  COL_NAME, &name, COL_CHAN, &ch, COL_ATTR, &attr, -1);
	glib_string name_ptr{ name };
	ch->impl = cv->func_add (cv, ch, name, nullptr);
	if (attr)
	{
		cv->func_set_color (ch, attr);
		pango_attr_list_unref (attr);
	}
}

static void
chanview_populate (chanview *cv)
{
	model_foreach_1(GTK_TREE_MODEL(cv->store), [cv](auto itr) {
		chanview_pop_cb(cv, itr);
	});
}

void
chanview_set_impl (chanview *cv, int type)
{
	/* cleanup the old one */
	if (cv->func_cleanup)
		cv->func_cleanup (cv);

	switch (type)
	{
	case 0:
		cv->func_init = cv_tabs_init;
		cv->func_postinit = cv_tabs_postinit;
		cv->func_add = cv_tabs_add;
		cv->func_move_focus = cv_tabs_move_focus;
		cv->func_change_orientation = cv_tabs_change_orientation;
		cv->func_remove = cv_tabs_remove;
		cv->func_move = cv_tabs_move;
		cv->func_move_family = cv_tabs_move_family;
		cv->func_focus = cv_tabs_focus;
		cv->func_set_color = cv_tabs_set_color;
		cv->func_rename = cv_tabs_rename;
		cv->func_is_collapsed = cv_tabs_is_collapsed;
		cv->func_get_parent = cv_tabs_get_parent;
		cv->func_cleanup = cv_tabs_cleanup;
		break;

	default:
		cv->func_init = cv_tree_init;
		cv->func_postinit = cv_tree_postinit;
		cv->func_add = cv_tree_add;
		cv->func_move_focus = cv_tree_move_focus;
		cv->func_change_orientation = cv_tree_change_orientation;
		cv->func_remove = cv_tree_remove;
		cv->func_move = cv_tree_move;
		cv->func_move_family = cv_tree_move_family;
		cv->func_focus = cv_tree_focus;
		cv->func_set_color = cv_tree_set_color;
		cv->func_rename = cv_tree_rename;
		cv->func_is_collapsed = cv_tree_is_collapsed;
		cv->func_get_parent = cv_tree_get_parent;
		cv->func_cleanup = cv_tree_cleanup;
		break;
	}

	/* now rebuild a new tabbar or tree */
	cv->func_init (cv);

	chanview_populate (cv);

	cv->func_postinit (cv);

	/* force re-focus */
	if (cv->focused)
		cv->func_focus (cv->focused);
}

static void
chanview_free_ch (chanview *cv, GtkTreeIter *iter)
{
	chan *ch;

	gtk_tree_model_get (GTK_TREE_MODEL (cv->store), iter, COL_CHAN, &ch, -1);
	delete ch;
}

static void
chanview_destroy_store (chanview *cv)	/* free every (chan *) in the store */
{
	model_foreach_1(GTK_TREE_MODEL(cv->store), [cv](auto itr) {
		chanview_free_ch(cv, itr);
	});
	g_object_unref (cv->store);
}

static void
chanview_destroy (chanview *cv)
{
	if (cv->func_cleanup)
		cv->func_cleanup (cv);

	if (cv->box)
		gtk_widget_destroy (cv->box);

	chanview_destroy_store (cv);
	delete cv;
}

static void
chanview_box_destroy_cb (GtkWidget * /*box*/, chanview *cv)
{
	cv->box = nullptr;
	chanview_destroy (cv);
}

chanview *
chanview_new (int type, int trunc_len, bool sort, bool use_icons,
				  GtkStyle *style)
{
	chanview *cv = new chanview();
	cv->store = gtk_tree_store_new (4, G_TYPE_STRING, G_TYPE_POINTER,
											  PANGO_TYPE_ATTR_LIST, GDK_TYPE_PIXBUF);
	cv->style = style;
	cv->box = gtk_hbox_new (0, 0);
	cv->trunc_len = trunc_len;
	cv->sorted = sort;
	cv->use_icons = use_icons;
	gtk_widget_show (cv->box);
	chanview_set_impl (cv, type);

	g_signal_connect (G_OBJECT (cv->box), "destroy",
							G_CALLBACK (chanview_box_destroy_cb), cv);

	return cv;
}

/* too lazy for signals */

void
chanview_set_callbacks (chanview *cv,
	void (*cb_focus) (chanview *, chan *, int tag, void *userdata),
	void (*cb_xbutton) (chanview *, chan *, int tag, void *userdata),
	gboolean (*cb_contextmenu) (chanview *, chan *, int tag, void *userdata, GdkEventButton *),
	int (*cb_compare) (void *a, void *b))
{
	cv->cb_focus = cb_focus;
	cv->cb_xbutton = cb_xbutton;
	cv->cb_contextmenu = cb_contextmenu;
	cv->cb_compare = cb_compare;
}

/* find a place to insert this new entry, based on the compare function */

static void
chanview_insert_sorted (chanview *cv, GtkTreeIter *add_iter, GtkTreeIter *parent, void *ud)
{
	GtkTreeIter iter;
	chan *ch;

	if (cv->sorted && gtk_tree_model_iter_children (GTK_TREE_MODEL (cv->store), &iter, parent))
	{
		do
		{
			gtk_tree_model_get (GTK_TREE_MODEL (cv->store), &iter, COL_CHAN, &ch, -1);
			if (ch->tag == 0 && cv->cb_compare (ch->userdata, ud) > 0)
			{
				gtk_tree_store_insert_before (cv->store, add_iter, parent, &iter);
				return;
			}
		}
		while (gtk_tree_model_iter_next (GTK_TREE_MODEL (cv->store), &iter));
	}

	gtk_tree_store_append (cv->store, add_iter, parent);
}

/* find a parent node with the same "family" pointer (i.e. the Server tab) */

static int
chanview_find_parent (chanview *cv, void *family, GtkTreeIter *search_iter, chan *avoid)
{
	chan *search_ch;

	/* find this new row's parent, if any */
	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (cv->store), search_iter))
	{
		do
		{
			gtk_tree_model_get (GTK_TREE_MODEL (cv->store), search_iter, 
									  COL_CHAN, &search_ch, -1);
			if (family == search_ch->family && search_ch != avoid /*&&
				 gtk_tree_store_iter_depth (cv->store, search_iter) == 0*/)
				return TRUE;
		}
		while (gtk_tree_model_iter_next (GTK_TREE_MODEL (cv->store), search_iter));
	}

	return FALSE;
}

static chan *
chanview_add_real (chanview *cv, const char *name, void *family, void *userdata,
						 gboolean allow_closure, int tag, GdkPixbuf *icon,
						 chan *ch, chan *avoid)
{
	GtkTreeIter parent_iter;
	GtkTreeIter iter;
	bool has_parent = false;

	if (chanview_find_parent (cv, family, &parent_iter, avoid))
	{
		chanview_insert_sorted (cv, &iter, &parent_iter, userdata);
		has_parent = true;
	} else
	{
		gtk_tree_store_append (cv->store, &iter, nullptr);
	}

	if (!ch)
	{
		ch = new chan();
		ch->userdata = userdata;
		ch->family = family;
		ch->cv = cv;
		ch->allow_closure = allow_closure;
		ch->tag = tag;
		ch->icon = icon;
	}
	ch->iter = iter;
	//memcpy (&(ch->iter), &iter, sizeof (iter));

	gtk_tree_store_set (cv->store, &iter, COL_NAME, name, COL_CHAN, ch,
							  COL_PIXBUF, icon, -1);

	cv->size++;
	if (!has_parent)
		ch->impl = cv->func_add (cv, ch, name, nullptr);
	else
		ch->impl = cv->func_add (cv, ch, name, &parent_iter);

	return ch;
}

chan *
chanview_add (chanview *cv, const boost::string_ref name, void *family, void *userdata, gboolean allow_closure, int tag, GdkPixbuf *icon)
{
	auto new_name = truncate_tab_name (name, cv->trunc_len);

	auto ret = chanview_add_real (cv, new_name.c_str(), family, userdata, allow_closure, tag, icon, nullptr, nullptr);

	return ret;
}

int
chanview_get_size (chanview *cv)
{
	return cv->size;
}

GtkWidget *
chanview_get_box (chanview *cv)
{
	return cv->box;
}

void
chanview_move_focus (chanview *cv, gboolean relative, int num)
{
	cv->func_move_focus (cv, relative, num);
}

GtkOrientation
chanview_get_orientation (chanview *cv)
{
	return (cv->vertical ? GTK_ORIENTATION_VERTICAL : GTK_ORIENTATION_HORIZONTAL);
}

void
chanview_set_orientation (chanview *cv, bool vertical)
{
	if (vertical != cv->vertical)
	{
		cv->vertical = vertical;
		cv->func_change_orientation (cv);
	}
}

int
chan_get_tag (chan *ch)
{
	return ch->tag;
}

void *
chan_get_userdata (chan *ch)
{
	return ch->userdata;
}

void
chan_focus (chan *ch)
{
	if (ch->cv->focused == ch)
		return;

	ch->cv->func_focus (ch);
}

void
chan_move (chan *ch, int delta)
{
	ch->cv->func_move (ch, delta);
}

void
chan_move_family (chan *ch, int delta)
{
	ch->cv->func_move_family (ch, delta);
}

void
chan_set_color (chan *ch, PangoAttrList *list)
{
	gtk_tree_store_set (ch->cv->store, &ch->iter, COL_ATTR, list, -1);	
	ch->cv->func_set_color (ch, list);
}

void
chan_rename (chan *ch, char *name, int trunc_len)
{
	auto new_name = truncate_tab_name (name, trunc_len);

	gtk_tree_store_set (ch->cv->store, &ch->iter, COL_NAME, new_name.c_str(), -1);
	ch->cv->func_rename (ch, new_name.c_str());
	ch->cv->trunc_len = trunc_len;
}

/* this thing is overly complicated */

static int
cv_find_number_of_chan (chanview *cv, chan *find_ch)
{
	GtkTreeIter iter, inner;
	chan *ch;
	int i = 0;

	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (cv->store), &iter))
	{
		do
		{
			gtk_tree_model_get (GTK_TREE_MODEL (cv->store), &iter, COL_CHAN, &ch, -1);
			if (ch == find_ch)
				return i;
			i++;

			if (gtk_tree_model_iter_children (GTK_TREE_MODEL (cv->store), &inner, &iter))
			{
				do
				{
					gtk_tree_model_get (GTK_TREE_MODEL (cv->store), &inner, COL_CHAN, &ch, -1);
					if (ch == find_ch)
						return i;
					i++;
				}
				while (gtk_tree_model_iter_next (GTK_TREE_MODEL (cv->store), &inner));
			}
		}
		while (gtk_tree_model_iter_next (GTK_TREE_MODEL (cv->store), &iter));
	}

	return 0;	/* WARNING */
}

/* this thing is overly complicated too */

static chan *
cv_find_chan_by_number (chanview *cv, int num)
{
	GtkTreeIter iter, inner;
	chan *ch;
	int i = 0;

	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (cv->store), &iter))
	{
		do
		{
			if (i == num)
			{
				gtk_tree_model_get (GTK_TREE_MODEL (cv->store), &iter, COL_CHAN, &ch, -1);
				return ch;
			}
			i++;

			if (gtk_tree_model_iter_children (GTK_TREE_MODEL (cv->store), &inner, &iter))
			{
				do
				{
					if (i == num)
					{
						gtk_tree_model_get (GTK_TREE_MODEL (cv->store), &inner, COL_CHAN, &ch, -1);
						return ch;
					}
					i++;
				}
				while (gtk_tree_model_iter_next (GTK_TREE_MODEL (cv->store), &inner));
			}
		}
		while (gtk_tree_model_iter_next (GTK_TREE_MODEL (cv->store), &iter));
	}

	return nullptr;
}

static void
chan_emancipate_children (chan *ch)
{
	GtkTreeIter childiter;
	PangoAttrList *attr;

	while (gtk_tree_model_iter_children (GTK_TREE_MODEL (ch->cv->store), &childiter, &ch->iter))
	{
		char *name;
		chan *childch;
		/* remove and re-add all the children, but avoid using "ch" as parent */
		gtk_tree_model_get (GTK_TREE_MODEL (ch->cv->store), &childiter,
								  COL_NAME, &name, COL_CHAN, &childch, COL_ATTR, &attr, -1);
		glib_string name_ptr(name);
		ch->cv->func_remove (childch);
		gtk_tree_store_remove (ch->cv->store, &childiter);
		ch->cv->size--;
		chanview_add_real (childch->cv, name, childch->family, childch->userdata, childch->allow_closure, childch->tag, childch->icon, childch, ch);
		if (attr)
		{
			childch->cv->func_set_color (childch, attr);
			pango_attr_list_unref (attr);
		}
	}
}

bool chan_remove (chan *ch, bool force)
{
	chan *new_ch;
	int i, num;
	extern std::atomic_bool hexchat_is_quitting;

	if (hexchat_is_quitting)	/* avoid lots of looping on exit */
		return true;

	/* is this ch allowed to be closed while still having children? */
	if (!force &&
		 gtk_tree_model_iter_has_child (GTK_TREE_MODEL (ch->cv->store), &ch->iter) &&
		 !ch->allow_closure)
		return false;

	chan_emancipate_children (ch);
	ch->cv->func_remove (ch);

	/* is it the focused one? */
	if (ch->cv->focused == ch)
	{
		ch->cv->focused = nullptr;

		/* try to move the focus to some other valid channel */
		num = cv_find_number_of_chan (ch->cv, ch);
		/* move to the one left of the closing tab */
		new_ch = cv_find_chan_by_number (ch->cv, num - 1);
		if (new_ch && new_ch != ch)
		{
			chan_focus (new_ch);	/* this'll will set ch->cv->focused for us too */
		} else
		{
			/* if it fails, try focus from tab 0 and up */
			for (i = 0; i < ch->cv->size; i++)
			{
				new_ch = cv_find_chan_by_number (ch->cv, i);
				if (new_ch && new_ch != ch)
				{
					chan_focus (new_ch);	/* this'll will set ch->cv->focused for us too */
					break;
				}
			}
		}
	}

	ch->cv->size--;
	gtk_tree_store_remove (ch->cv->store, &ch->iter);
	delete ch;
	return true;
}

gboolean
chan_is_collapsed (chan *ch)
{
	return ch->cv->func_is_collapsed (ch);
}

chan *
chan_get_parent (chan *ch)
{
	return ch->cv->func_get_parent (ch);
}
