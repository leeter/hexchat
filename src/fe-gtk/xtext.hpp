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

#ifndef HEXCHAT_XTEXT_HPP
#define HEXCHAT_XTEXT_HPP

#include <string_view>
#include <memory>
#include <boost/utility/string_ref_fwd.hpp>
#include <gtk/gtk.h>
#include <glib-object.h>
#include <gsl.h>
#include <../common/sessfwd.hpp>
#include <../common/url.hpp>

#define GTK_TYPE_XTEXT              (gtk_xtext_get_type ())
G_DECLARE_FINAL_TYPE(GtkXText, gtk_xtext, GTK, XTEXT, GtkWidget)

enum text_attr{
	ATTR_BOLD = '\002',
	ATTR_COLOR = '\003',
	ATTR_BLINK = '\006',
	ATTR_BEEP = '\007',
	ATTR_HIDDEN = '\010',
	ATTR_ITALICS2 = '\011',
	ATTR_RESET = '\017',
	ATTR_REVERSE = '\026',
	ATTR_ITALICS = '\035',
	ATTR_UNDERLINE = '\037'
};

/* these match palette.h */
#define XTEXT_MIRC_COLS 32
#define XTEXT_COLS 37		/* 32 plus 5 for extra stuff below */
#define XTEXT_MARK_FG 32	/* for marking text */
#define XTEXT_MARK_BG 33
#define XTEXT_FG 34
#define XTEXT_BG 35
#define XTEXT_MARKER 36		/* for marker line */
#define XTEXT_MAX_COLOR 41

using ustring_ref = std::basic_string_view<unsigned char>;

enum marker_reset_reason {
	MARKER_WAS_NEVER_SET,
	MARKER_IS_SET,
	MARKER_RESET_MANUALLY,
	MARKER_RESET_BY_KILL,
	MARKER_RESET_BY_CLEAR
};

enum time_stamping : bool {
	no_stamp = false,
	time_stamped = true
};

struct xtext_buffer;

GtkWidget *gtk_xtext_new(const gsl::span<GdkColor, XTEXT_COLS> palette, bool separator);
void gtk_xtext_append(xtext_buffer *buf, boost::string_ref text, time_t stamp);
void gtk_xtext_append_indent(xtext_buffer *buf,
	ustring_ref left_text, ustring_ref right_text, time_t stamp);
bool gtk_xtext_set_font(GtkXText *xtext, const char name[]);
//void gtk_xtext_set_background(GtkXText * xtext, GdkPixmap * pixmap);
void gtk_xtext_set_palette(GtkXText * xtext, const gsl::span<GdkColor, XTEXT_COLS> palette);
void gtk_xtext_clear(xtext_buffer *buf, int lines);
namespace xtext{
	void save(const GtkXText & xtext, std::ostream & outfile);
}
void gtk_xtext_refresh(GtkXText * xtext);
int gtk_xtext_lastlog(xtext_buffer *out, xtext_buffer *search_area);
bool gtk_xtext_search(GtkXText * xtext, const gchar *text, gtk_xtext_search_flags flags, GError **err);
void gtk_xtext_reset_marker_pos(GtkXText *xtext);
int gtk_xtext_moveto_marker_pos(GtkXText *xtext);
void gtk_xtext_check_marker_visibility(GtkXText *xtext);
void gtk_xtext_set_marker_last(session *sess);

bool gtk_xtext_is_empty(const xtext_buffer &buf);

void gtk_xtext_set_indent(GtkXText *xtext, gboolean indent);
void gtk_xtext_set_max_indent(GtkXText *xtext, int max_auto_indent);
void gtk_xtext_set_max_lines(GtkXText *xtext, int max_lines);
void gtk_xtext_set_show_marker(GtkXText *xtext, gboolean show_marker);
void gtk_xtext_set_show_separator(GtkXText *xtext, gboolean show_separator);
void gtk_xtext_set_thin_separator(GtkXText *xtext, gboolean thin_separator);
void gtk_xtext_set_urlcheck_function(GtkXText *xtext, int(*urlcheck_function) (GtkWidget *, const char *));
void gtk_xtext_set_wordwrap(GtkXText *xtext, gboolean word_wrap);
xtext_buffer* xtext_get_current_buffer(GtkXText*);
xtext_buffer *gtk_xtext_buffer_new(GtkXText *xtext);
GtkAdjustment* xtext_get_adjustments(GtkXText*);
void gtk_xtext_buffer_free(xtext_buffer *buf);
void gtk_xtext_buffer_show(GtkXText *xtext, xtext_buffer *buf, bool render);
void gtk_xtext_copy_selection(GtkXText *xtext);
void gtk_xtext_set_ignore_hidden(GtkXText* xtext, bool ignore_hidden);
void gtk_xtext_buffer_set_xtext(xtext_buffer* buf, GtkXText* xtext);
GError* gtk_xtext_buffer_set_search_regex(xtext_buffer* buf, gtk_xtext_search_flags, const boost::string_ref&);
void gtk_xtext_buffer_set_stamping(xtext_buffer*, time_stamping);
void gtk_xtext_buffer_set_search_paramters(xtext_buffer* buffer, const std::string_view search_string, const std::string_view needle, gtk_xtext_search_flags flags);
#endif
