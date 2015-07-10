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

#include <memory>
#include <boost/utility/string_ref_fwd.hpp>
#include <gtk/gtk.h>

#define GTK_TYPE_XTEXT              (gtk_xtext_get_type ())
#define GTK_XTEXT(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), GTK_TYPE_XTEXT, GtkXText))
#define GTK_XTEXT_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_XTEXT, GtkXTextClass))
#define GTK_IS_XTEXT(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), GTK_TYPE_XTEXT))
#define GTK_IS_XTEXT_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_XTEXT))
#define GTK_XTEXT_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((obj), GTK_TYPE_XTEXT, GtkXTextClass))

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


struct GtkXText;
struct GtkXTextClass;
struct textentry;
using ustring_ref = boost::basic_string_ref<unsigned char>;
/*
* offsets_t is used for retaining search information.
* It is stored in the 'data' member of a GList,
* as chained from ent->marks.  It saves starting and
* ending+1 offset of a found occurrence.
*/
typedef union offsets_u {
	struct offsets_s {
		guint16	start;
		guint16	end;
	} o;
	guint32 u;
} offsets_t;

enum marker_reset_reason {
	MARKER_WAS_NEVER_SET,
	MARKER_IS_SET,
	MARKER_RESET_MANUALLY,
	MARKER_RESET_BY_KILL,
	MARKER_RESET_BY_CLEAR
};

struct xtext_impl;

struct xtext_buffer {
private:
	xtext_buffer(const xtext_buffer&) = delete;
	xtext_buffer& operator=(const xtext_buffer&) = delete;

public:
	explicit xtext_buffer(GtkXText* parent);
	~xtext_buffer() NOEXCEPT;
	std::unique_ptr<xtext_impl> impl;
	
	GtkXText *xtext;					/* attached to this widget */

	gdouble old_value;					/* last known adj->value */
	
	int last_offset_start;
	int last_offset_end;

	int last_pixel_pos;

	int pagetop_line;
	int pagetop_subline;

	int num_lines;
	int indent;						  /* position of separator (pixels) from left */

	int window_width;				/* window size when last rendered. */
	int window_height;

	bool time_stamp;
	bool scrollbar_down;
	bool needs_recalc;
	bool marker_seen;

	GList *search_found;		/* list of textentries where search found strings */
	std::string search_text;		/* desired text to search for */
	std::string search_nee;		/* prepared needle to look in haystack for */
	gtk_xtext_search_flags search_flags;	/* match, bwd, highlight */
	GList *cursearch;			/* GList whose 'data' pts to current textentry */
	GList *curmark;			/* current item in ent->marks */
	offsets_t curdata;		/* current offset info, from *curmark */
	GRegex *search_re;		/* Compiled regular expression */
	textentry *hintsearch;	/* textentry found for last search */
};

struct GtkXText
{
	GtkWidget widget;

	xtext_buffer *buffer;
	xtext_buffer *orig_buffer;
	xtext_buffer *selection_buffer;

	GtkAdjustment *adj;
	GdkPixmap *pixmap;				/* 0 = use palette[19] */
	GdkDrawable *draw_buf;			/* points to ->window */
	GdkCursor *hand_cursor;
	GdkCursor *resize_cursor;

	int pixel_offset;					/* amount of pixels the top line is chopped by */

	int last_win_x;
	int last_win_y;
	int last_win_h;
	int last_win_w;

	GdkGC *bgc;						  /* backing pixmap */
	GdkGC *fgc;						  /* text foreground color */
	GdkGC *light_gc;				  /* sep bar */
	GdkGC *dark_gc;
	GdkGC *thin_gc;
	GdkGC *marker_gc;
	GdkColor palette[XTEXT_COLS];

	gint io_tag;					  /* for delayed refresh events */
	gint add_io_tag;				  /* "" when adding new text */
	gint scroll_tag;				  /* marking-scroll timeout */
	gulong vc_signal_tag;        /* signal handler for "value_changed" adj */

	int select_start_adj;		  /* the adj->value when the selection started */
	int select_start_x;
	int select_start_y;
	int select_end_x;
	int select_end_y;

	int max_lines;

	int col_fore;
	int col_back;

	int depth;						  /* gdk window depth */

	char num[8];					  /* for parsing mirc color */
	int nc;							  /* offset into xtext->num */

	textentry *hilight_ent;
	int hilight_start;
	int hilight_end;

	guint16 fontwidth[128];	  /* each char's width, only the ASCII ones */

	struct pangofont
	{
		PangoFontDescription *font;
		int ascent;
		int descent;
	} *font, pango_font;
	PangoLayout *layout;

	int fontsize;
	int space_width;				  /* width (pixels) of the space " " character */
	int stamp_width;				  /* width of "[88:88:88]" */
	int max_auto_indent;

	unsigned char scratch_buffer[4096];

	int(*urlcheck_function) (GtkWidget * xtext, const char *word);

	int jump_out_offset;	/* point at which to stop rendering */
	int jump_in_offset;	/* "" start rendering */

	int ts_x;			/* ts origin for ->bgc GC */
	int ts_y;

	int clip_x;			/* clipping (x directions) */
	int clip_x2;		/* from x to x2 */

	int clip_y;			/* clipping (y directions) */
	int clip_y2;		/* from y to y2 */

	/* current text states */
	bool underline;
	bool hidden;

	/* text parsing states */
	bool parsing_backcolor;
	bool parsing_color;
	bool backcolor;

	/* various state information */
	bool moving_separator;
	bool word_select;
	bool line_select;
	bool button_down;
	bool hilighting;
	bool dont_render;
	bool dont_render2;
	bool cursor_hand;
	bool cursor_resize;
	bool skip_border_fills;
	bool skip_stamp;
	bool mark_stamp;	/* Cut&Paste with stamps? */
	bool force_stamp;	/* force redrawing it */
	bool render_hilights_only;
	bool in_hilight;
	bool un_hilight;
	bool force_render;
	bool color_paste; /* CTRL was pressed when selection finished */

	/* settings/prefs */
	bool auto_indent;
	bool thinline;
	bool marker;
	bool separator;
	bool wordwrap;
	bool ignore_hidden;	/* rawlog uses this */
};

struct GtkXTextClass
{
	GtkWidgetClass parent_class;
	void(*word_click) (GtkXText * xtext, char *word, GdkEventButton * event);
	void(*set_scroll_adjustments) (GtkXText *xtext, GtkAdjustment *hadj, GtkAdjustment *vadj);
};

GtkWidget *gtk_xtext_new(GdkColor palette[], bool separator);
void gtk_xtext_append(xtext_buffer *buf, boost::string_ref text, time_t stamp);
void gtk_xtext_append_indent(xtext_buffer *buf,
	ustring_ref left_text, ustring_ref right_text, time_t stamp);
bool gtk_xtext_set_font(GtkXText *xtext, const char name[]);
void gtk_xtext_set_background(GtkXText * xtext, GdkPixmap * pixmap);
void gtk_xtext_set_palette(GtkXText * xtext, GdkColor palette[]);
void gtk_xtext_clear(xtext_buffer *buf, int lines);
namespace xtext{
	void save(const GtkXText & xtext, std::ostream & outfile);
}
void gtk_xtext_refresh(GtkXText * xtext);
int gtk_xtext_lastlog(xtext_buffer *out, xtext_buffer *search_area);
textentry *gtk_xtext_search(GtkXText * xtext, const gchar *text, gtk_xtext_search_flags flags, GError **err);
void gtk_xtext_reset_marker_pos(GtkXText *xtext);
int gtk_xtext_moveto_marker_pos(GtkXText *xtext);
void gtk_xtext_check_marker_visibility(GtkXText *xtext);
void gtk_xtext_set_marker_last(session *sess);

bool gtk_xtext_is_empty(const xtext_buffer &buf);

void gtk_xtext_set_error_function(GtkXText *xtext, void(*error_function) (int));
void gtk_xtext_set_indent(GtkXText *xtext, gboolean indent);
void gtk_xtext_set_max_indent(GtkXText *xtext, int max_auto_indent);
void gtk_xtext_set_max_lines(GtkXText *xtext, int max_lines);
void gtk_xtext_set_show_marker(GtkXText *xtext, gboolean show_marker);
void gtk_xtext_set_show_separator(GtkXText *xtext, gboolean show_separator);
void gtk_xtext_set_thin_separator(GtkXText *xtext, gboolean thin_separator);
void gtk_xtext_set_time_stamp(xtext_buffer *buf, gboolean timestamp);
void gtk_xtext_set_urlcheck_function(GtkXText *xtext, int(*urlcheck_function) (GtkWidget *, const char *));
void gtk_xtext_set_wordwrap(GtkXText *xtext, gboolean word_wrap);

xtext_buffer *gtk_xtext_buffer_new(GtkXText *xtext);
void gtk_xtext_buffer_free(xtext_buffer *buf);
void gtk_xtext_buffer_show(GtkXText *xtext, xtext_buffer *buf, bool render);
void gtk_xtext_copy_selection(GtkXText *xtext);
GType gtk_xtext_get_type(void);


#endif
