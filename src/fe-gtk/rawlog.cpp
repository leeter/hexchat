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
#include "precompile.hpp"
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include <fcntl.h>

#include <boost/filesystem/fstream.hpp>

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
#include "../common/server.hpp"
#include "gtkutil.hpp"
#include "palette.hpp"
#include "maingui.hpp"
#include "rawlog.hpp"
#include "xtext.hpp"
#include "fkeys.hpp"

namespace {
static void
close_rawlog (GtkWidget *wid, server *serv)
{
	if (is_server (serv))
		serv->gui->rawlog_window = nullptr;
}

static void
rawlog_save (server *serv, char *file)
{
	if (!file || !serv->gui->rawlog_window)
	{
		return;
	}
	namespace bfs = boost::filesystem;
	bfs::path file_path(file);
	bfs::ofstream outfile{ file_path, std::ios::trunc | std::ios::out | std::ios::binary };
	if (!outfile)
	{
		return;
	}

	xtext::save(*GTK_XTEXT(serv->gui->rawlog_textlist), outfile);
	outfile.flush();
	boost::system::error_code ec;
	bfs::permissions(file_path, bfs::owner_read | bfs::owner_write, ec);
}

static int
rawlog_clearbutton (GtkWidget * wid, server *serv)
{
	auto buffer = xtext_get_current_buffer(GTK_XTEXT(serv->gui->rawlog_textlist));
	gtk_xtext_clear (buffer, 0);
	return FALSE;
}

static int
rawlog_savebutton (GtkWidget * wid, server *serv)
{
	gtkutil_file_req (_("Save As..."), (filereqcallback)rawlog_save, serv, NULL, NULL, FRF_WRITE);
	return FALSE;
}

static gboolean
rawlog_key_cb (GtkWidget * wid, GdkEventKey * key, gpointer userdata)
{
	/* Copy rawlog selection to clipboard when Ctrl+Shift+C is pressed,
	 * but make sure not to copy twice, i.e. when auto-copy is enabled.
	 */
	if (!prefs.hex_text_autocopy_text &&
		(key->keyval == GDK_KEY_c || key->keyval == GDK_KEY_C) &&
		key->state & STATE_SHIFT &&
		key->state & STATE_CTRL)
	{
		gtk_xtext_copy_selection (static_cast<GtkXText*>(userdata));
	}
	return FALSE;
}
}

void
open_rawlog (struct server *serv)
{
	GtkWidget *bbox, *scrolledwindow, *vbox;
	char tbuf[256];

	if (serv->gui->rawlog_window)
	{
		mg_bring_tofront (serv->gui->rawlog_window);
		return;
	}

	snprintf (tbuf, sizeof tbuf, _(DISPLAY_NAME ": Raw Log (%s)"), serv->servername);
	serv->gui->rawlog_window =
		mg_create_generic_tab("RawLog", tbuf, FALSE, TRUE, G_CALLBACK(close_rawlog), serv,
							 640, 320, &vbox, serv);
	gtkutil_destroy_on_esc (serv->gui->rawlog_window);

	scrolledwindow = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow), GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);
	gtk_scrolled_window_set_shadow_type (GTK_SCROLLED_WINDOW (scrolledwindow), GTK_SHADOW_IN);
	gtk_container_add (GTK_CONTAINER (vbox), scrolledwindow);

	serv->gui->rawlog_textlist = gtk_xtext_new (colors, false);
	gtk_container_add (GTK_CONTAINER (scrolledwindow), serv->gui->rawlog_textlist);
	auto xtext = GTK_XTEXT(serv->gui->rawlog_textlist);
	auto buffer = xtext_get_current_buffer(xtext);
	gtk_xtext_set_font (xtext, prefs.hex_text_font);
	gtk_xtext_set_ignore_hidden(xtext, true);

	bbox = gtk_hbutton_box_new ();
	gtk_button_box_set_layout (GTK_BUTTON_BOX (bbox), GTK_BUTTONBOX_SPREAD);
	gtk_box_pack_end (GTK_BOX (vbox), bbox, 0, 0, 4);

	gtkutil_button(bbox, GTK_STOCK_CLEAR, NULL, G_CALLBACK(rawlog_clearbutton),
						 serv, _("Clear Raw Log"));

	gtkutil_button(bbox, GTK_STOCK_SAVE_AS, NULL, G_CALLBACK(rawlog_savebutton),
						 serv, _("Save As..."));

	/* Copy selection to clipboard when Ctrl+Shift+C is pressed AND text auto-copy is disabled */
	g_signal_connect (G_OBJECT (serv->gui->rawlog_window), "key_press_event", G_CALLBACK (rawlog_key_cb), serv->gui->rawlog_textlist);

	gtk_widget_show_all (serv->gui->rawlog_window);
}

void
fe_add_rawlog(server *serv, const boost::string_ref &text, bool outbound)
{
	if (!serv->gui->rawlog_window)
		return;

	std::vector<std::string> split_strings;
	auto buffer = xtext_get_current_buffer(GTK_XTEXT(serv->gui->rawlog_textlist));
	for (auto & it : boost::iter_split(split_strings, text, boost::algorithm::first_finder("\r\n")))
	{
		if (it.empty())
			break;
		std::string new_text((outbound ? u8"\u00034<<\017 " : u8"\u00033>>\017 ") + it);
		
		gtk_xtext_append (buffer, new_text, 0);
	}
}
