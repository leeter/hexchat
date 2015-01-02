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

#ifndef HEXCHAT_GTKUTIL_HPP
#define HEXCHAT_GTKUTIL_HPP

#include <gtk/gtk.h>
#include "../common/fe.hpp"

typedef void (*filereqcallback) (void *, char *file);

void gtkutil_file_req(const char *title, filereqcallback callback, void *userdata, char *filter, char *extensions, int flags);
void gtkutil_destroy (GtkWidget * igad, GtkWidget * dgad);
void gtkutil_destroy_on_esc (GtkWidget *win);
GtkWidget *gtkutil_button (GtkWidget *box, const char *stock, const char *tip, GCallback callback,
				 void *userdata, const char *labeltext);
void gtkutil_label_new (const char *text, GtkWidget * box);
GtkWidget *gtkutil_entry_new (int max, GtkWidget * box, GCallback callback,
										gpointer userdata);
void show_and_unfocus (GtkWidget * wid);
void gtkutil_set_icon (GtkWidget *win);
GtkWidget *gtkutil_window_new (const char *title, const char *role, int width, int height, int flags);
void gtkutil_copy_to_clipboard (GtkWidget *widget, GdkAtom selection,
								const gchar *str);
GtkWidget *gtkutil_treeview_new (GtkWidget *box, GtkTreeModel *model,
								 GtkTreeCellDataFunc mapper, ...);
gboolean gtkutil_treemodel_string_to_iter (GtkTreeModel *model, gchar *pathstr, GtkTreeIter *iter_ret);
gboolean gtkutil_treeview_get_selected_iter (GtkTreeView *view, GtkTreeIter *iter_ret);
gboolean gtkutil_treeview_get_selected (GtkTreeView *view, GtkTreeIter *iter_ret, ...);

#if defined (WIN32) || defined (__APPLE__)
gboolean gtkutil_find_font (const char *fontname);
#endif

#endif
