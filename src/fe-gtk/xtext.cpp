/* X-Chat, Hexchat
* Copyright (C) 1998 Peter Zelezny.
* Copyright (c) 2014-2015 Leetsoftwerx
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
* =========================================================================
*
* xtext, the text widget used by X-Chat.
* By Peter Zelezny <zed@xchat.org>.
*
*/
#include "precompile.hpp"

enum{ MARGIN = 2 };					/* dont touch. */
#define REFRESH_TIMEOUT 20
#define WORDWRAP_LIMIT 24

#include "../../config.h"
#include "../common/hexchat.hpp"
#include "../common/fe.hpp"
#include "../common/util.hpp"
#include "../common/hexchatc.hpp"
#include "../common/url.hpp"
#include "../common/marshal.h"
#include "../common/session.hpp"
#include "../common/glist_iterators.hpp"
#include "../common/charset_helpers.hpp"
#include "../common/text.hpp"
#include "fe-gtk.hpp"
#include "xtext.hpp"
#include "fkeys.hpp"
#include "gtk3bridge.hpp"
#include "xtext_backend.hpp"

#define charlen(str) g_utf8_skip[(guchar)(*str)]

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

char *nocasestrstr(const char *text, const char *tofind);	/* util.c */
//gpointer gtk_xtext_get_instance_private(GtkXText*);
namespace {

	CUSTOM_PTR(PangoFontDescription, pango_font_description_free);
	CUSTOM_PTR(PangoFontMetrics, pango_font_metrics_unref);
	CUSTOM_PTR(PangoAttribute, pango_attribute_destroy);

	/*
	Represents one 'line' or entry of text in the xtext, includes sublines (glyph runs of common
	emphasis)
	*/
	struct textentry
	{
		// the width of the text on the left if there are no restrictions
		std::uint32_t m_indent_text_width;
		std::unique_ptr<xtext::layout> m_layout;
		std::unique_ptr<xtext::layout> m_indentLayout;
	public:
		textentry(std::unique_ptr<xtext::layout> layout, std::unique_ptr<xtext::layout> indent_layout = nullptr)
			:m_indent_text_width(),
			m_layout(std::move(layout)),
			m_indentLayout(std::move(indent_layout)),
			str_width(m_layout->width()),
			mark_start(),
			mark_end(),
			indent(),
			left_len(),
			stamp(),
			next(),
			prev(),
			marks() 
		{
			if (m_indentLayout) {
				m_indentLayout->set_alignment(xtext::right);
				m_indent_text_width = m_indentLayout->width();
			}
		}

		gint16 str_width;
		gint16 mark_start;
		gint16 mark_end;
		// the actual width in pixels for the entry to render, this may be wider than needed
		gint16 indent;
		gint16 left_len;
		std::time_t stamp;
		textentry *next;
		textentry *prev;
		GList *marks;	/* List of found strings */
		std::vector<xtext::offlen_t> slp;
		std::vector<int> sublines;
		xtext::ustring str;

	public:
		int pixel_width() const noexcept {
			return str_width;
		}

		std::string_view stripped_text() const noexcept {
			return m_layout->text();
		}

		std::uint32_t indent_width() const noexcept {
			return m_indent_text_width;
		}

		void set_width(std::uint32_t indent, std::uint32_t text_width) {
			m_layout->set_width(text_width);
			if (m_indentLayout) {
				m_indentLayout->set_width(indent);
			}
		}

		std::uint32_t line_count() const noexcept {
			return m_layout->line_count();
		}

	};

	

}

struct _GtkXText {
	GtkWidget parent_instance;
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

#if !GTK_CHECK_VERSION(3, 0, 0)
	BridgeStyleContext * style;
#endif
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
	xtext::xtext_backend * backend;
};

//#ifndef __INTELLISENSE__
G_DEFINE_TYPE(GtkXText, gtk_xtext, GTK_TYPE_WIDGET)
//#else
//static GtkWidgetClass* gtk_xtext_parent_class;
//#endif

/*
Represents the a common state set for the xtext including all of the entries, usually only one per
context such as a tab or channel
*/
struct xtext_buffer
{
private:
	bool time_stamp;
	GtkXText *m_parent;					/* attached to this widget */
	std::uint32_t m_indent;						  /* position of separator (pixels) from left */
	std::uint32_t m_window_width;				/* window size when last rendered. */
public:
	~xtext_buffer() noexcept;
	int last_offset_start;
	int last_offset_end;
	int last_pixel_pos;
	
	marker_reset_reason marker_state;
	textentry *text_first;
	textentry *text_last;

	textentry *last_ent_start;	  /* this basically describes the last rendered */
	textentry *last_ent_end;	  /* selection. */
	textentry *pagetop_ent;			/* what's at xtext->adj->value */

	textentry *marker_pos;
	gdouble old_value;					/* last known adj->value */
	GRegex *search_re;		/* Compiled regular expression */
	textentry *hintsearch;	/* textentry found for last search */
	std::deque<textentry> entries;

	int pagetop_line;
	int pagetop_subline;

	int num_lines;

	int window_height;

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

	xtext_buffer(GtkXText* parent, int space_width)
		:time_stamp(),
		m_parent(parent),
		m_indent(space_width * 2),
		m_window_width(), /* window size when last rendered. */
		last_offset_start(),
		last_offset_end(),
		last_pixel_pos(),
		marker_state(),
		text_first(), text_last(),

		last_ent_start(), /* this basically describes the last rendered */
		last_ent_end(),   /* selection. */
		pagetop_ent(), /* what's at xtext->adj->value */
		marker_pos(),
		old_value(-1.0), /* last known adj->value */
		search_re(), /* Compiled regular expression */
		hintsearch(),	/* textentry found for last search */
		pagetop_line(), pagetop_subline(),

		num_lines(),

		window_height(),

		scrollbar_down(true), needs_recalc(), marker_seen(),

		search_found(), /* list of textentries where search found strings */
		search_flags(), /* match, bwd, highlight */
		cursearch(),    /* GList whose 'data' pts to current textentry */
		curmark(),      /* current item in ent->marks */
		curdata()       /* current offset info, from *curmark */
	{
	}

	GtkXText* current_xtext() const noexcept {
		return m_parent;
	}

	std::uint32_t indent() const noexcept {
		return m_indent;
	}

	void set_xtext(GtkXText* xtext) {
		g_return_if_fail(GTK_IS_XTEXT(xtext));
		g_return_if_fail(xtext != nullptr);
		if (this->m_parent == xtext) {
			return;
		}
		this->m_parent = xtext;
	}

	/* force scrolling off */
	void dontscroll()
	{
		this->last_pixel_pos = std::numeric_limits<decltype(xtext_buffer::last_pixel_pos)>::max();
	}

	void set_indent(std::uint32_t new_indent) {
		if (new_indent < MARGIN) /* 2 pixels is our left margin */
		{
			new_indent = MARGIN;
		}
		const auto max_auto_indent = static_cast<std::uint32_t>(m_parent->max_auto_indent);
		if (new_indent > max_auto_indent)
		{
			new_indent = max_auto_indent;
		}
		
		/* make indent a multiple of the space width */
		if (m_parent->space_width)
		{
			std::uint32_t j = 0;
			while (j < new_indent)
			{
				j += m_parent->space_width;
			}
			new_indent = j;
		}
		this->dontscroll();	/* force scrolling off, WHY? */
		this->m_indent = new_indent;
		invalidate();
	}

	void invalidate() {
		gtk_widget_queue_draw(GTK_WIDGET(m_parent));
	}

	void set_width(std::uint32_t width) {
		m_window_width = width;
	}

	std::uint32_t width() const noexcept {
		return m_window_width;
	}

	std::uint32_t rendered_width() const noexcept {
		return m_window_width > MARGIN ? m_window_width - MARGIN : 0;
	}

	void set_time_stamping(time_stamping);

	GError* set_search_regex(gtk_xtext_search_flags, const boost::string_ref&);
	bool is_time_stamped() const noexcept {
		return time_stamp;
	}
};

namespace{
	template<class T>
	inline bool is_del(T c)
	{
		return (c == ' ' || c == '\n' || c == '>' || c == '<' || c == 0);
	}

	enum
	{
		WORD_CLICK,
		SET_SCROLL_ADJUSTMENTS,
		LAST_SIGNAL
	};

	/* values for selection info */
	enum
	{
		TARGET_UTF8_STRING,
		TARGET_STRING,
		TARGET_TEXT,
		TARGET_COMPOUND_TEXT
	};

	static guint xtext_signals[LAST_SIGNAL];


	static void gtk_xtext_render_page(GtkXText * xtext, cairo_t* cr);
	static void gtk_xtext_calc_lines(xtext_buffer *buf, bool);
	static std::string gtk_xtext_selection_get_text(GtkXText *xtext);
	static textentry *gtk_xtext_nth(GtkXText *xtext, int line, int &subline);
	static void gtk_xtext_adjustment_changed(GtkAdjustment * adj,
		GtkXText * xtext);
	static void gtk_xtext_scroll_adjustments(GtkXText *xtext, GtkAdjustment *hadj,
		GtkAdjustment *vadj);
	static void gtk_xtext_recalc_widths(xtext_buffer *buf, bool);
	static int gtk_xtext_find_subline(const textentry &ent, int line);
	/* static char *gtk_xtext_conv_color (unsigned char *text, int len, int *newlen); */
	[[deprecated("uses the scratch buffer, use xtext::strip_color instead")]]
	static unsigned char *
		gtk_xtext_strip_color(const ustring_ref & text, unsigned char *outbuf,
		int *newlen, std::vector<xtext::offlen_t> * slp, int strip_hidden);
	static bool gtk_xtext_check_ent_visibility(GtkXText * xtext, textentry *find_ent, int add);
	static int gtk_xtext_render_page_timeout(GtkXText * xtext);
	static int gtk_xtext_search_offset(xtext_buffer *buf, textentry *ent, unsigned int off);
	static GList * gtk_xtext_search_textentry(xtext_buffer *, const textentry &);
	static void gtk_xtext_search_textentry_add(xtext_buffer *, textentry *, GList *, bool);
	static void gtk_xtext_search_textentry_del(xtext_buffer *, textentry *);
	static void gtk_xtext_search_textentry_fini(gpointer, gpointer);
	static void gtk_xtext_search_fini(xtext_buffer *);
	static bool gtk_xtext_search_init(xtext_buffer *buf, const gchar *text, gtk_xtext_search_flags flags, GError **perr);
	static const char * gtk_xtext_get_word(GtkXText * xtext, int x, int y, textentry ** ret_ent, int *ret_off, int *ret_len, std::vector<xtext::offlen_t> *slp);

	/* ======================================= */
	/* ============ PANGO BACKEND ============ */
	/* ======================================= */

	enum emph{
		EMPH_ITAL = 1,
		EMPH_BOLD = 2,
		EMPH_HIDDEN = 4
	};

	static std::array<PangoAttrList *, 4> attr_lists;
	static int fontwidths[4][128];

	static PangoAttribute *
		xtext_pango_attr(PangoAttribute *attr)
	{
		attr->start_index = PANGO_ATTR_INDEX_FROM_TEXT_BEGINNING;
		attr->end_index = PANGO_ATTR_INDEX_TO_TEXT_END;
		return attr;
	}

	static void
		xtext_pango_init(GtkXText *xtext)
	{
		char buf[2] = "\000";

		if (attr_lists[0])
		{
			for (int i = 0; i < (EMPH_ITAL | EMPH_BOLD); i++)
				pango_attr_list_unref(attr_lists[i]);
		}
		for (std::size_t i = 0; i < attr_lists.size(); ++i)
		{
			attr_lists[i] = pango_attr_list_new();
			switch (i)
			{
			case 0:		/* Roman */
				break;
			case EMPH_ITAL:		/* Italic */
				pango_attr_list_insert(attr_lists[i],
					xtext_pango_attr(pango_attr_style_new(PANGO_STYLE_ITALIC)));
				break;
			case EMPH_BOLD:		/* Bold */
				pango_attr_list_insert(attr_lists[i],
					xtext_pango_attr(pango_attr_weight_new(PANGO_WEIGHT_BOLD)));
				break;
			case EMPH_ITAL | EMPH_BOLD:		/* Italic Bold */
				pango_attr_list_insert(attr_lists[i],
					xtext_pango_attr(pango_attr_style_new(PANGO_STYLE_ITALIC)));
				pango_attr_list_insert(attr_lists[i],
					xtext_pango_attr(pango_attr_weight_new(PANGO_WEIGHT_BOLD)));
				break;
			}

			/* Now initialize fontwidths[i] */
			pango_layout_set_attributes(xtext->layout, attr_lists[i]);
			for (int j = 0; j < 128; j++)
			{
				buf[0] = j;
				pango_layout_set_text(xtext->layout, buf, 1);
				pango_layout_get_pixel_size(xtext->layout, &fontwidths[i][j], nullptr);
			}
		}
		xtext->space_width = fontwidths[0][' '];
	}

	static void
		backend_font_close(GtkXText *xtext)
	{
		pango_font_description_free(xtext->font->font);
	}

	static void
		backend_init(GtkXText *xtext)
	{
		if (xtext->layout == nullptr)
		{
			xtext->layout = gtk_widget_create_pango_layout(GTK_WIDGET(xtext), 0);
			if (xtext->font)
				pango_layout_set_font_description(xtext->layout, xtext->font->font);
		}
	}

	static void
		backend_deinit(GtkXText *xtext)
	{
		if (xtext->layout)
		{
			g_object_unref(xtext->layout);
			xtext->layout = nullptr;
		}
	}

	static PangoFontDescription *
		backend_font_open_real(const char *name)
	{
		PangoFontDescription *font;

		font = pango_font_description_from_string(name);
		if (font && pango_font_description_get_size(font) == 0)
		{
			pango_font_description_free(font);
			font = pango_font_description_from_string("sans 11");
		}
		if (!font)
			font = pango_font_description_from_string("sans 11");

		return font;
	}

	static void backend_font_open(GtkXText *xtext, const char *name)
	{
		xtext->font = &xtext->pango_font;
		xtext->font->font = backend_font_open_real(name);
		if (!xtext->font->font)
		{
			xtext->font = nullptr;
			return;
		}

		backend_init(xtext);
		pango_layout_set_font_description(xtext->layout, xtext->font->font);
		xtext_pango_init(xtext);

		/* vte does it this way */
		auto context = gtk_widget_get_pango_context(GTK_WIDGET(xtext));
		auto lang = pango_context_get_language(context);
		PangoFontMetricsPtr metrics{ pango_context_get_metrics(context, xtext->font->font, lang) };
		xtext->font->ascent = pango_font_metrics_get_ascent(metrics.get()) / PANGO_SCALE;
		xtext->font->descent = pango_font_metrics_get_descent(metrics.get()) / PANGO_SCALE;
	}

	/**
	 Gets the width of a line in the context of an xtext
	 */
	static int backend_get_text_width_emph(GtkXText *xtext, const ustring_ref & str, int emphasis)
	{
		if (str.empty())
			return 0;

		if ((emphasis & EMPH_HIDDEN))
			return 0;
		emphasis &= (EMPH_ITAL | EMPH_BOLD);

		int width = 0;
		pango_layout_set_attributes(xtext->layout, attr_lists[emphasis]);
		for (auto itr = str.cbegin(), end = str.cend(); itr != end;)
		{
			const auto mbl = charlen(itr);
			int deltaw;
			if (*itr < 128)
				deltaw = fontwidths[emphasis][*itr];
			else
			{
				
				pango_layout_set_text(xtext->layout, reinterpret_cast<const char*>(&(*itr)), mbl);
				pango_layout_get_pixel_size(xtext->layout, &deltaw, nullptr);
			}
			width += deltaw;
			if (mbl < std::distance(itr, end))
				itr += mbl;
			else
				break;
		}

		return width;
	}

	static int backend_get_text_width_slp(GtkXText *xtext, const guchar *str, const std::vector<xtext::offlen_t> & slp)
	{
		int width = 0;

		for (const auto & meta : slp)
		{
			width += backend_get_text_width_emph(xtext, ustring_ref(str, meta.len), meta.emph);
			str += meta.len;
		}

		return width;
	}

	static void
		xtext_set_fg(GtkXText *xtext, int index)
	{
		xtext->col_fore = index;
		bridge_set_foreground(xtext->style, &xtext->palette[index]);
	}

	static void
		xtext_set_bg(GtkXText *xtext, int index)
	{
		xtext->col_back = index;
		bridge_set_background(xtext->style, &xtext->palette[index]);
	}

	static void gtk_xtext_adjustment_set(xtext_buffer *buf,
					     bool fire_signal)
	{
		auto xtext = buf->current_xtext();
		GtkAdjustment *adj = xtext->adj;

		if (xtext->buffer != buf)
		{
			return;
		}

		auto lower = 0.0;
		auto upper = static_cast<gdouble>(buf->num_lines);

		if (upper == 0.0)
			upper = 1.0;

		GtkAllocation allocation;
		gtk_widget_get_allocation(GTK_WIDGET(buf->current_xtext()), &allocation);
		auto page_size = static_cast<gdouble>(
		    allocation.height /
		    xtext->fontsize);
		auto page_increment = page_size;

		// TODO: need a better name for this
		auto adjustment = upper - page_size;
		auto adj_value = gtk_adjustment_get_value(adj);
		if (adj_value > adjustment)
		{
			buf->scrollbar_down = true;
			adj_value = adjustment;
		}

		if (adj_value < 0.0)
			adj_value = 0.0;

		g_signal_handler_block(adj, xtext->vc_signal_tag);
		gtk_adjustment_configure(adj, adj_value, lower, upper, gtk_adjustment_get_step_increment(adj), page_increment, page_size);
		g_signal_handler_unblock(adj, xtext->vc_signal_tag);
		if (fire_signal)
			gtk_adjustment_changed(adj);
	}

	static gint
		gtk_xtext_adjustment_timeout(GtkXText * xtext)
	{
		gtk_widget_queue_draw(GTK_WIDGET(xtext));
		xtext->io_tag = 0;
		return 0;
	}

	static void
		gtk_xtext_adjustment_changed(GtkAdjustment * /*adj*/, GtkXText * xtext)
	{
		if (!gtk_widget_get_realized(GTK_WIDGET(xtext)))
			return;
		const auto adj_value = gtk_adjustment_get_value(xtext->adj);
		const auto old_adj_val = xtext->buffer->old_value;
		if (old_adj_val != adj_value)
		{
			const auto page_diff = gtk_adjustment_get_upper(xtext->adj) - gtk_adjustment_get_page_size(xtext->adj);
			if (adj_value >= page_diff)
				xtext->buffer->scrollbar_down = true;
			else
				xtext->buffer->scrollbar_down = false;

			if (adj_value + 1.0 == old_adj_val ||
				adj_value - 1.0 == old_adj_val)	/* clicked an arrow? */
			{
				if (xtext->io_tag)
				{
					g_source_remove(xtext->io_tag);
					xtext->io_tag = 0;
				}
				gtk_widget_queue_draw(GTK_WIDGET(xtext));
			}
			else
			{
				if (!xtext->io_tag)
					xtext->io_tag = g_timeout_add(REFRESH_TIMEOUT,
					(GSourceFunc)
					gtk_xtext_adjustment_timeout,
					xtext);
			}
		}
		xtext->buffer->old_value = adj_value;
	}
	auto xtext_get_stamp_str(std::time_t time) {
		return get_stamp_str(prefs.hex_stamp_text_format, time);
	}
	static void
		gtk_xtext_dispose(GObject * object)
	{
		GtkXText *xtext = GTK_XTEXT(object);
		if (xtext->add_io_tag)
		{
			g_source_remove(xtext->add_io_tag);
			xtext->add_io_tag = 0;
		}

		if (xtext->scroll_tag)
		{
			g_source_remove(xtext->scroll_tag);
			xtext->scroll_tag = 0;
		}

		if (xtext->io_tag)
		{
			g_source_remove(xtext->io_tag);
			xtext->io_tag = 0;
		}

		if (xtext->pixmap)
		{
			g_object_unref(xtext->pixmap);
			xtext->pixmap = nullptr;
		}

		if (xtext->font)
		{
			backend_font_close(xtext);
			xtext->font = nullptr;
		}

		if (xtext->adj)
		{
			g_signal_handlers_disconnect_matched(G_OBJECT(xtext->adj),
				G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, xtext);
			/*	gtk_signal_disconnect_by_data (G_OBJECT (xtext->adj), xtext);*/
			g_object_unref(G_OBJECT(xtext->adj));
			xtext->adj = nullptr;
		}

		if (xtext->hand_cursor)
		{
			gdk_cursor_unref(xtext->hand_cursor);
			xtext->hand_cursor = nullptr;
		}

		if (xtext->resize_cursor)
		{
			gdk_cursor_unref(xtext->resize_cursor);
			xtext->resize_cursor = nullptr;
		}

		if (xtext->orig_buffer)
		{
			gtk_xtext_buffer_free(xtext->orig_buffer);
			xtext->orig_buffer = nullptr;
		}
#if !GTK_CHECK_VERSION(3, 0, 0)
		if (xtext->style)
		{
			bridge_style_context_free(xtext->style);
			xtext->style = nullptr;
		}
#endif
		if (xtext->backend) {
			delete xtext->backend;
			xtext->backend = nullptr;
		}
		G_OBJECT_CLASS(gtk_xtext_parent_class)->dispose(object);
	}

	static void
		gtk_xtext_unrealize(GtkWidget * widget)
	{
		backend_deinit(GTK_XTEXT(widget));

		/* if there are still events in the queue, this'll avoid segfault */
		gdk_window_set_user_data(gtk_widget_get_window(widget), nullptr);
		
		GTK_WIDGET_CLASS(gtk_xtext_parent_class)->unrealize(widget);
	}

	static void
		gtk_xtext_realize(GtkWidget * widget)
	{
		gtk_widget_set_realized(widget, true);
		auto xtext = GTK_XTEXT(widget);

		GtkAllocation allocation;
		gtk_widget_get_allocation(widget, &allocation);
		GdkWindowAttr attributes = {};
		attributes.x = allocation.x;
		attributes.y = allocation.y;
		attributes.width = allocation.width;
		attributes.height = allocation.height;
		attributes.wclass = GDK_INPUT_OUTPUT;
		attributes.window_type = GDK_WINDOW_CHILD;
		attributes.event_mask = gtk_widget_get_events(widget) |
			GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
			| GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK;

		auto cmap = gtk_widget_get_colormap(widget);
		attributes.colormap = cmap;
		attributes.visual = gtk_widget_get_visual(widget);

		auto window = gdk_window_new(gtk_widget_get_parent_window(widget), &attributes,
			GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL |
			GDK_WA_COLORMAP);
		gtk_widget_set_window(widget, window);

		gdk_window_set_user_data(window, widget);
		GdkGCValues val = {};
		val.subwindow_mode = GDK_INCLUDE_INFERIORS;
		val.graphics_exposures = 0;

		xtext_set_fg(xtext, XTEXT_FG);
		xtext_set_bg(xtext, XTEXT_BG);
		xtext_set_fg(xtext, XTEXT_BG);

		/* draw directly to window */
		xtext->draw_buf = window;

		xtext->hand_cursor = gdk_cursor_new_for_display(gdk_window_get_display(window), GDK_HAND1);
		xtext->resize_cursor = gdk_cursor_new_for_display(gdk_window_get_display(window), GDK_LEFT_SIDE);

		gdk_window_set_back_pixmap(window, nullptr, false);
		gtk_widget_set_style(widget, gtk_style_attach(gtk_widget_get_style(widget), window));

		backend_init(xtext);
	}

	static void
		gtk_xtext_size_request(GtkWidget *, GtkRequisition * requisition)
	{
		requisition->width = 200;
		requisition->height = 90;
	}

	static void gtk_xtext_size_allocate(GtkWidget *widget,
					    GtkAllocation *allocation)
	{
		GtkXText *xtext = GTK_XTEXT(widget);
		const auto height_only = allocation->width == xtext->buffer->width();

		gtk_widget_set_allocation(widget, allocation);
		if (!gtk_widget_get_realized(GTK_WIDGET(widget)))
		{
			return;
		}

		xtext->buffer->set_width(allocation->width);
		xtext->buffer->window_height = allocation->height;

		gdk_window_move_resize(gtk_widget_get_window(widget), allocation->x,
				       allocation->y, allocation->width,
				       allocation->height);
		xtext->buffer->dontscroll(); /* force scrolling off */
		if (!height_only)
			gtk_xtext_calc_lines(xtext->buffer, false);
		else
		{
			xtext->buffer->pagetop_ent = nullptr;
			gtk_xtext_adjustment_set(xtext->buffer, false);
		}
		if (xtext->buffer->scrollbar_down) {
			gtk_adjustment_set_value(xtext->adj,
				gtk_adjustment_get_upper(xtext->adj) -
				gtk_adjustment_get_page_size(xtext->adj));
		}
	}

	static int
		gtk_xtext_selection_clear(xtext_buffer *buf)
	{
		int ret = 0;

		auto ent = buf->last_ent_start;
		while (ent)
		{
			if (ent->mark_start != -1)
				ret = 1;
			ent->mark_start = -1;
			ent->mark_end = -1;
			if (ent == buf->last_ent_end)
				break;
			ent = ent->next;
		}

		return ret;
	}

	static int
		find_x(GtkXText *xtext, const textentry &ent, int x, int subline, int indent)
	{
		int xx = indent;
		int suboff;

		/* Skip to the first chunk of stuff for the subline */
		std::vector<xtext::offlen_t>::const_iterator meta, hid = ent.slp.cend();
		if (subline > 0)
		{
			suboff = ent.sublines[subline - 1];
			decltype(meta) end = ent.slp.cend();
			for (meta = ent.slp.cbegin(); meta != end; ++meta)
			{
				if (meta->off + meta->len > suboff)
					break;
			}
		}
		else
		{
			suboff = 0;
			meta = ent.slp.cbegin();
		}
		/* Step to the first character of the subline */
		int off = meta->off;
		int len = meta->len;
		if (meta->emph & EMPH_HIDDEN)
			hid = meta;
		while (len > 0)
		{
			if (off >= suboff)
				break;
			const auto mbl = charlen(ent.str.c_str() + off);
			len -= mbl;
			off += mbl;
		}
		if (len < 0)
			return ent.str.size();		/* Bad char -- return max offset. */

		/* Step through characters to find the one at the x position */
		auto wid = x - indent;
		len = meta->len - (off - meta->off);
		while (wid > 0)
		{
			const auto mbl = charlen(ent.str.c_str() + off);
			const auto mbw = backend_get_text_width_emph(xtext, ustring_ref(ent.str.c_str() + off, mbl), meta->emph);
			wid -= mbw;
			xx += mbw;
			if (xx >= x)
				return off;
			len -= mbl;
			off += mbl;
			if (len <= 0)
			{
				if (meta->emph & EMPH_HIDDEN)
					hid = meta;
				++meta;
				if (meta == ent.slp.cend())
					return ent.str.size();
				off = meta->off;
				len = meta->len;
			}
		}

		/* If previous chunk exists and is marked hidden, regard it as unhidden */
		if (hid != ent.slp.cend() && meta != ent.slp.cend() && hid + 1 == meta)
		{
			off = hid->off;
		}

		/* Return offset of character at x within subline */
		return off;
	}

	static int
		gtk_xtext_find_x(GtkXText * xtext, int x, const textentry & ent, int subline,
		int line, gboolean& out_of_bounds)
	{
		const unsigned char *str;
		const auto indent = subline < 1 ? ent.indent : xtext->buffer->indent();

		if (line > gtk_adjustment_get_page_size(xtext->adj) || line < 0)
		{
			out_of_bounds = true;
			return 0;
		}

		str = ent.str.c_str() + gtk_xtext_find_subline(ent, subline);
		if (str >= ent.str.c_str() + ent.str.size())
			return 0;

		/* Let user select left a few pixels to grab hidden text e.g. '<' */
		if (x < indent - xtext->space_width)
		{
			out_of_bounds = true;
			return (str - ent.str.c_str());
		}

		out_of_bounds = false;

		return find_x(xtext, ent, x, subline, indent);
	}

	static textentry *
		gtk_xtext_find_char(GtkXText * xtext, int x, int y, int *off,
		gboolean &out_of_bounds, int *ret_subline)
	{
		/* Adjust y value for negative rounding, double to int */
		if (y < 0)
			y -= xtext->fontsize;

		int subline;
		const auto line = (y + xtext->pixel_offset) / xtext->fontsize;
		auto ent = gtk_xtext_nth(xtext, line + (int)gtk_adjustment_get_value(xtext->adj), subline);
		if (!ent)
			return nullptr;

		if (off)
			*off = gtk_xtext_find_x(xtext, x, *ent, subline, line, out_of_bounds);

		if (ret_subline)
			*ret_subline = subline;

		return ent;
	}

	static void gtk_xtext_draw_sep(GtkXText *xtext, cairo_t *cr, int height)
	{
		if (!xtext->separator || !xtext->buffer->indent())
			return;
		const auto x = xtext->buffer->indent() - ((xtext->space_width + 1) / 2);
		if (x < 1)
			return;

		cairo_stack cr_stack{cr};
		cairo_new_path(cr);
		if (xtext->moving_separator)
		{
			// light
			// col.red = 0xffff; col.green = 0xffff; col.blue =
			// 0xffff;
			cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
		}
		else if (xtext->thinline)
		{
			// thin
			// col.red = 0x8e38; col.green = 0x8e38; col.blue =
			// 0x9f38;
			cairo_set_source_rgb(cr, 0x8e38 / 65535.0,
					     0x8e38 / 65535.0,
					     0x9f38 / 65535.0);
		}
		else
		{
			const auto channel_val = 0x1111 / 65535.0;
			cairo_set_source_rgb(cr, channel_val, channel_val,
					     channel_val);
		}

		/* draw the separator line */
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
		if (xtext->thinline)
		{
			cairo_move_to(cr, x + 0.5, 0.0);
			cairo_line_to(cr, x + 0.5, height);
		}
		else
		{
			cairo_move_to(cr, x, 0.0);
			cairo_line_to(cr, x, height);
		}
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
	}

	static void
		gtk_xtext_draw_marker(GtkXText * xtext, cairo_t* cr, textentry * ent, int y)
	{
		if (!xtext->marker) return;

		double render_y;
		if (xtext->buffer->marker_pos == ent)
		{
			render_y = y + xtext->font->descent;
		}
		else if (xtext->buffer->marker_pos == ent->next && ent->next != nullptr)
		{
			render_y = y + xtext->font->descent + xtext->fontsize * ent->sublines.size();
		}
		else return;

		constexpr int x = 0;
		GtkAllocation allocation = {};
		gtk_widget_get_allocation(GTK_WIDGET(xtext), &allocation);
		const int width = -allocation.width;
		cairo_stack stack{ cr };
		cairo_new_path(cr);
		gdk_cairo_set_source_color(cr, &xtext->palette[XTEXT_MARKER]);
		render_y += 0.5;
		cairo_move_to(cr, x, render_y);
		cairo_line_to(cr, x + width, render_y);
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);

		if (gtk_window_has_toplevel_focus(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(xtext)))))
		{
			xtext->buffer->marker_seen = true;
		}
	}

#if !GTK_CHECK_VERSION(3, 0, 0)
	auto gtk_widget_get_style_context(GtkWidget* w) {
		return GTK_XTEXT(w)->style;
	}
#endif
	static gboolean gtk_xtext_draw(GtkWidget *widget, cairo_t *cr, gpointer /*user_data*/)
	{
		g_return_val_if_fail(GTK_IS_XTEXT(widget), true);
		GtkXText *xtext = GTK_XTEXT(widget);
		GtkAllocation allocation = {};
		gtk_widget_get_allocation(widget, &allocation);
		gtk_render_background(gtk_widget_get_style_context(widget), cr, allocation.x,
				      allocation.y, allocation.width,
				      allocation.height);
		xtext_get_current_buffer(xtext)->dontscroll(); /* force scrolling off */
		gtk_xtext_render_page(xtext, cr);
		return false;
	}

	static gboolean
		gtk_xtext_expose(GtkWidget * widget, GdkEventExpose * event)
	{
		g_return_val_if_fail(GTK_IS_WIDGET(widget), true);
		g_return_val_if_fail(gtk_widget_get_realized(widget), true);
		g_return_val_if_fail(event != NULL, true);
		g_return_val_if_fail(event->type == GDK_EXPOSE, true);
		cairo_tPtr cr{gdk_cairo_create(event->window)};
		gdk_cairo_region(cr.get(), event->region);
		cairo_clip(cr.get());
		cairo_stack cr_stack{ cr.get() };
		gtk_xtext_draw(widget, cr.get(), nullptr);

		return false;
	}

	static void
		gtk_xtext_selection_render(GtkXText *xtext, textentry *start_ent, textentry *end_ent)
	{
		auto buffer = xtext_get_current_buffer(xtext);
		buffer->last_ent_start = start_ent;
		buffer->last_ent_end = end_ent;
		buffer->last_offset_start = start_ent->mark_start;
		buffer->last_offset_end = end_ent->mark_end;
		xtext->skip_border_fills = false;
		xtext->skip_stamp = false;
		// TODO, only queue area of actual draw
		gtk_widget_queue_draw(GTK_WIDGET(xtext));
	}

	static void gtk_xtext_selection_draw(GtkXText * xtext, GdkEventMotion *, bool render)
	{
		int offset_start;
		int offset_end;
		int subline_start;
		int subline_end;
		gboolean oob = false;
		auto ent_start = gtk_xtext_find_char(xtext, xtext->select_start_x, xtext->select_start_y, &offset_start, oob, &subline_start);
		auto ent_end = gtk_xtext_find_char(xtext, xtext->select_end_x, xtext->select_end_y, &offset_end, oob, &subline_end);
	
		if ((!ent_start || !ent_end) && !xtext->buffer->text_last && gtk_adjustment_get_value(xtext->adj) != xtext->buffer->old_value)
		{
			gtk_widget_queue_draw(GTK_WIDGET(xtext));
			return;
		}

		if (!ent_start)
		{
			ent_start = xtext->buffer->text_last;
			offset_start = ent_start->str.size();
		}

		if (!ent_end)
		{
			ent_end = xtext->buffer->text_last;
			offset_end = ent_end->str.size();
		}

		bool marking_up = false;

		if ((ent_start != ent_end && xtext->select_start_y > xtext->select_end_y) || /* different entries */
			(ent_start == ent_end && subline_start > subline_end) || /* different lines */
			(ent_start == ent_end && subline_start == subline_end && xtext->select_start_x > xtext->select_end_x)) /* marking to the left */
		{
			marking_up = true;
		}

		/* word selection */
		if (xtext->word_select)
		{
			int len_start;
			int len_end;
			/* a word selection cannot be started if the cursor is out of bounds in gtk_xtext_button_press */
			gtk_xtext_get_word(xtext, xtext->select_start_x, xtext->select_start_y, nullptr, &offset_start, &len_start, nullptr);

			/* in case the cursor is out of bounds we keep offset_end from gtk_xtext_find_char and fix the length */
			if (gtk_xtext_get_word(xtext, xtext->select_end_x, xtext->select_end_y, nullptr, &offset_end, &len_end, nullptr) == nullptr)
				len_end = offset_end == ent_end->str.size() ? 0 : -1; /* -1 for the space, 0 if at the end */

			if (!marking_up)
				offset_end += len_end;
			else
				offset_start += len_start;
		}
		/* line/ent selection */
		else if (xtext->line_select)
		{
			offset_start = marking_up ? ent_start->str.size() : 0;
			offset_end = marking_up ? 0 : ent_end->str.size();
		}

		if (marking_up)
		{
			/* ensure ent_start is above ent_end */
			if (ent_start != ent_end)
			{
				std::swap(ent_start, ent_end);
			}

			/* switch offsets as well */
			std::swap(offset_start, offset_end);
		}

		/* set all the old mark_ fields to -1 */
		gtk_xtext_selection_clear(xtext->buffer);

		/* set the default values */
		ent_start->mark_end = ent_start->str.size();
		ent_end->mark_start = 0;

		/* set the calculated values (this overwrites the default values if we're on the same ent) */
		ent_start->mark_start = offset_start;
		ent_end->mark_end = offset_end;

		/* set all the mark_ fields of the ents within the selection */
		if (ent_start != ent_end)
		{
			auto ent = ent_start->next;
			while (ent && ent != ent_end)
			{
				ent->mark_start = 0;
				ent->mark_end = ent->str.size();
				ent = ent->next;
			}
		}

		if (render)
			gtk_xtext_selection_render(xtext, ent_start, ent_end);
	}

	static int gtk_xtext_timeout_ms(int pixes)
	{
		const auto apixes = std::abs(pixes);

		if (apixes < 6) return 100;
		if (apixes < 12) return 50;
		if (apixes < 20) return 20;
		return 10;
	}

	static gboolean gtk_xtext_scrolldown_timeout(GtkXText * xtext)
	{
		xtext_buffer *buf = xtext->buffer;
		GtkAdjustment *adj = xtext->adj;
		int p_y;
		gdk_window_get_pointer(gtk_widget_get_window(GTK_WIDGET(xtext)), nullptr, &p_y, nullptr);
		const auto win_height = gdk_window_get_height(gtk_widget_get_window(GTK_WIDGET(xtext)));

		const auto adj_value = gtk_adjustment_get_value(adj);
		if (buf->last_ent_end == nullptr ||	/* If context has changed OR */
			buf->pagetop_ent == nullptr ||	/* pagetop_ent is reset OR */
			p_y <= win_height ||			/* pointer not below bottom margin OR */
			adj_value >= gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj)) 	/* we're scrolled to bottom */
		{
			xtext->scroll_tag = 0;
			return false;
		}

		xtext->select_start_y -= xtext->fontsize;
		xtext->select_start_adj++;
		gtk_adjustment_set_value(adj, adj_value + 1.0);
		gtk_xtext_selection_draw(xtext, nullptr, true);
		gtk_widget_queue_draw(GTK_WIDGET(xtext));
		xtext->scroll_tag = g_timeout_add(gtk_xtext_timeout_ms(p_y - win_height),
			(GSourceFunc)
			gtk_xtext_scrolldown_timeout,
			xtext);

		return false;
	}

	static gboolean gtk_xtext_scrollup_timeout(GtkXText * xtext)
	{
		xtext_buffer *buf = xtext->buffer;
		GtkAdjustment *adj = xtext->adj;
		gint p_y;
		gdk_window_get_pointer(gtk_widget_get_window(GTK_WIDGET(xtext)), nullptr, &p_y, nullptr);
		auto adj_value = gtk_adjustment_get_value(adj);
		if (buf->last_ent_start == nullptr ||	/* If context has changed OR */
			buf->pagetop_ent == nullptr ||		/* pagetop_ent is reset OR */
			p_y >= 0 ||							/* not above top margin OR */
			adj_value == 0.0)						/* we're scrolled to the top */
		{
			xtext->scroll_tag = 0;
			return false;
		}
		int delta_y;
		if (adj_value < 0.0)
		{
			delta_y = adj_value * xtext->fontsize;
			adj_value = 0.0;
		}
		else {
			delta_y = xtext->fontsize;
			adj_value--;
		}
		xtext->select_start_y += delta_y;
		xtext->select_start_adj = static_cast<int>(adj_value);
		gtk_adjustment_set_value(adj, adj_value);
		gtk_xtext_selection_draw(xtext, nullptr, true);
		gtk_widget_queue_draw(GTK_WIDGET(xtext));
		xtext->scroll_tag = g_timeout_add(gtk_xtext_timeout_ms(p_y),
			(GSourceFunc)
			gtk_xtext_scrollup_timeout,
			xtext);

		return false;
	}

	static void
		gtk_xtext_selection_update(GtkXText * xtext, GdkEventMotion * event, int p_y, bool render)
	{
		if (xtext->scroll_tag)
		{
			return;
		}

		const auto win_height = gdk_window_get_height(gtk_widget_get_window(GTK_WIDGET(xtext)));
		const auto adj_value = gtk_adjustment_get_value(xtext->adj);
		/* selecting past top of window, scroll up! */
		if (p_y < 0 && adj_value >= 0)
		{
			gtk_xtext_scrollup_timeout(xtext);
		}

		/* selecting past bottom of window, scroll down! */
		else if (p_y > win_height &&
			adj_value < (gtk_adjustment_get_upper(xtext->adj) - gtk_adjustment_get_page_size(xtext->adj)))
		{
			gtk_xtext_scrolldown_timeout(xtext);
		}
		else
		{
			int moved = static_cast<int>(adj_value) - xtext->select_start_adj;
			xtext->select_start_y -= (moved * xtext->fontsize);
			xtext->select_start_adj = adj_value;
			gtk_xtext_selection_draw(xtext, event, render);
		}
	}

	static const char *
		gtk_xtext_get_word(GtkXText * xtext, int x, int y, textentry ** ret_ent,
		int *ret_off, int *ret_len, std::vector<xtext::offlen_t> *slp)
	{
		int offset;
		gboolean out_of_bounds = false;
		int len_to_offset = 0;

		auto ent = gtk_xtext_find_char(xtext, x, y, &offset, out_of_bounds, nullptr);
		if (!ent || out_of_bounds || offset < 0 || offset >= ent->str.size())
			return nullptr;

		auto word = ent->str.c_str() + offset;
		while ((word = (unsigned char*)g_utf8_find_prev_char((const gchar*)ent->str.c_str(), (const gchar*)word)))
		{
			if (is_del(*word))
			{
				word++;
				len_to_offset--;
				break;
			}
			len_to_offset += charlen(word);
		}
		if (!word)
			word = ent->str.c_str();
		/* remove color characters from the length */
		len_to_offset = xtext::strip_color(ustring_ref(word, len_to_offset), nullptr, false).length();

		auto last = word;
		auto end = ent->str.c_str() + ent->str.size();
		int len = 0;
		do
		{
			if (is_del(*last))
				break;
			len += charlen(last);
			last = (unsigned char*)g_utf8_find_next_char((const gchar*)last, (const gchar*)end);
		} while (last);

		if (len > 0 && word[len - 1] == '.')
			len--;

		if (ret_ent)
			*ret_ent = ent;
		if (ret_off)
			*ret_off = word - ent->str.c_str();
		if (ret_len)
			*ret_len = len;		/* Length before stripping */

		word = gtk_xtext_strip_color(ustring_ref(word, len), xtext->scratch_buffer, nullptr, slp, false);

		/* avoid turning the cursor into a hand for non-url part of the word */
		if (xtext->urlcheck_function && xtext->urlcheck_function(GTK_WIDGET(xtext), reinterpret_cast<const char*>(word)))
		{
			int start, end;
			url_last(&start, &end);

			/* make sure we're not before the start of the match */
			if (len_to_offset < start)
				return 0;

			/* and not after it */
			if (len_to_offset - start >= end - start)
				return 0;
		}

		return reinterpret_cast<const char*>(word);
	}

	static gboolean
		gtk_xtext_leave_notify(GtkWidget * widget, GdkEventCrossing *)
	{
		GtkXText *xtext = GTK_XTEXT(widget);
		if (xtext->cursor_hand)
		{
			xtext->hilight_start = -1;
			xtext->hilight_end = -1;
			xtext->cursor_hand = false;
			gdk_window_set_cursor(gtk_widget_get_window(widget), nullptr);
			xtext->hilight_ent = nullptr;
		}

		if (xtext->cursor_resize)
		{
			//gtk_xtext_unrender_hilight(xtext);
			xtext->hilight_start = -1;
			xtext->hilight_end = -1;
			xtext->cursor_resize = false;
			gdk_window_set_cursor(gtk_widget_get_window(widget), nullptr);
			xtext->hilight_ent = nullptr;
		}

		gtk_widget_queue_draw(widget);
		return false;
	}

	/* check if we should mark time stamps, and if a redraw is needed */

	static bool
		gtk_xtext_check_mark_stamp(GtkXText *xtext, GdkModifierType mask)
	{
		bool redraw = false;
		if (mask & STATE_SHIFT || prefs.hex_text_autocopy_stamp)
		{
			if (!xtext->mark_stamp)
			{
				redraw = true;	/* must redraw all */
				xtext->mark_stamp = true;
			}
		}
		else
		{
			if (xtext->mark_stamp)
			{
				redraw = true;	/* must redraw all */
				xtext->mark_stamp = false;
			}
		}
		return redraw;
	}

	static int
		gtk_xtext_get_word_adjust(GtkXText *xtext, int x, int y, textentry **word_ent, int *offset, int *len)
	{
		std::vector<xtext::offlen_t> slp;

		const auto word = gtk_xtext_get_word(xtext, x, y, word_ent, offset, len, &slp);
		if (!word)
		{
			return 0;
		}

		int laststart, lastend;

		int word_type = xtext->urlcheck_function(GTK_WIDGET(xtext), word);
		if (word_type > 0)
		{
			if (url_last(&laststart, &lastend))
			{
				int cumlen = 0, startadj = 0, endadj = 0;

				for (const auto & meta : slp)
				{
					startadj = meta.off - cumlen;
					cumlen += meta.len;
					if (laststart < cumlen)
						break;
				}
				cumlen = 0;
				for (const auto & meta : slp)
				{
					endadj = meta.off - cumlen;
					cumlen += meta.len;
					if (lastend < cumlen)
						break;
				}
				laststart += startadj;
				*offset += laststart;
				*len = lastend + endadj - laststart;
			}
		}
		return word_type;
	}

	static gboolean
		gtk_xtext_motion_notify(GtkWidget * widget, GdkEventMotion * event)
	{
		GtkXText *xtext = GTK_XTEXT(widget);
		GdkModifierType mask;
		int x, y;

		gdk_window_get_pointer(gtk_widget_get_window(widget), &x, &y, &mask);
		if (xtext->moving_separator)
		{
			GtkAllocation allocation;
			gtk_widget_get_allocation(widget, &allocation);
			if (x < (3 * allocation.width) / 5 && x > 15)
			{
				const auto old_indent = xtext->buffer->indent();
				xtext->buffer->set_indent(x);
				if (old_indent != xtext->buffer->indent())
				{
					gtk_xtext_recalc_widths(xtext->buffer, false);
					if (xtext->buffer->scrollbar_down)
						gtk_adjustment_set_value(xtext->adj, gtk_adjustment_get_upper(xtext->adj) -
						gtk_adjustment_get_page_size(xtext->adj));
					if (!xtext->io_tag)
						xtext->io_tag = g_timeout_add(REFRESH_TIMEOUT,
						(GSourceFunc)
						gtk_xtext_adjustment_timeout,
						xtext);
				}
			}
			return false;
		}

		if (xtext->button_down)
		{
			auto redraw = gtk_xtext_check_mark_stamp(xtext, mask);
			gtk_grab_add(widget);
			/*gdk_pointer_grab (widget->window, true,
			GDK_BUTTON_RELEASE_MASK |
			GDK_BUTTON_MOTION_MASK, nullptr, nullptr, 0);*/
			xtext->select_end_x = x;
			xtext->select_end_y = y;
			gtk_xtext_selection_update(xtext, event, y, !redraw);
			xtext->hilighting = true;

			gtk_widget_queue_draw(widget);
			return false;
		}

		if (xtext->separator && xtext->buffer->indent())
		{
			const auto line_x = xtext->buffer->indent() - ((xtext->space_width + 1) / 2);
			if (line_x == x || line_x == x + 1 || line_x == x - 1)
			{
				if (!xtext->cursor_resize)
				{
					gdk_window_set_cursor(gtk_widget_get_window(GTK_WIDGET(xtext)),
						xtext->resize_cursor);
					xtext->cursor_hand = false;
					xtext->cursor_resize = true;
				}
				return false;
			}
		}

		if (xtext->urlcheck_function == nullptr)
			return false;

		textentry *word_ent;
		int offset, len;
		const auto word_type = gtk_xtext_get_word_adjust(xtext, x, y, &word_ent, &offset, &len);
		if (word_type > 0)
		{
			if (!xtext->cursor_hand ||
				xtext->hilight_ent != word_ent ||
				xtext->hilight_start != offset ||
				xtext->hilight_end != offset + len)
			{
				if (!xtext->cursor_hand)
				{
					gdk_window_set_cursor(gtk_widget_get_window(GTK_WIDGET(xtext)),
						xtext->hand_cursor);
					xtext->cursor_hand = true;
					xtext->cursor_resize = false;
				}

				xtext->hilight_ent = word_ent;
				xtext->hilight_start = offset;
				xtext->hilight_end = offset + len;

				xtext->skip_border_fills = false;
				xtext->render_hilights_only = false;
				xtext->skip_stamp = false;
				gtk_widget_queue_draw(widget);
			}
			return false;
		}

		gtk_xtext_leave_notify(widget, nullptr);
		return false;
	}

	static void gtk_xtext_set_clip_owner(GtkWidget * widget, GdkEventButton * event)
	{
		const auto xtext = GTK_XTEXT(widget);
		if (xtext->selection_buffer &&
			xtext->selection_buffer != xtext->buffer)
			gtk_xtext_selection_clear(xtext->selection_buffer);

		xtext->selection_buffer = xtext->buffer;

		auto str = gtk_xtext_selection_get_text(GTK_XTEXT(xtext));
		if (!str.empty())
		{
			gtk_clipboard_set_text(gtk_widget_get_clipboard(widget, GDK_SELECTION_CLIPBOARD), str.c_str(), str.size());

			gtk_selection_owner_set(widget, GDK_SELECTION_PRIMARY, event ? event->time : GDK_CURRENT_TIME);
			gtk_selection_owner_set(widget, GDK_SELECTION_SECONDARY, event ? event->time : GDK_CURRENT_TIME);
		}
	}

	void gtk_xtext_unselect(GtkXText &xtext)
	{
		xtext_buffer *buf = xtext.buffer;

		xtext.skip_border_fills = true;
		xtext.skip_stamp = true;
		if (!buf->last_ent_start)
			return;
		xtext.jump_in_offset = buf->last_ent_start->mark_start;
		/* just a single ent was marked? */
		if (buf->last_ent_start == buf->last_ent_end)
		{
			xtext.jump_out_offset = buf->last_ent_start->mark_end;
			buf->last_ent_end = nullptr;
		}

		gtk_xtext_selection_clear(xtext.buffer);

		/* FIXME: use jump_out on multi-line selects too! */
		xtext.jump_in_offset = 0;
		xtext.jump_out_offset = 0;
		xtext.skip_border_fills = false;
		xtext.skip_stamp = false;

		xtext.buffer->last_ent_start = nullptr;
		xtext.buffer->last_ent_end = nullptr;
		gtk_widget_queue_draw(GTK_WIDGET(&xtext));
	}

	static gboolean
		gtk_xtext_button_release(GtkWidget * widget, GdkEventButton * event)
	{
		GtkXText *xtext = GTK_XTEXT(widget);
		if (xtext->moving_separator)
		{
			xtext->moving_separator = false;
			const auto old = xtext->buffer->indent();
			GtkAllocation allocation = {};
			gtk_widget_get_allocation(widget, &allocation);
			if (event->x < (4 * allocation.width) / 5 && event->x > 15) {
				xtext->buffer->set_indent(event->x);
			}

			if (xtext->buffer->indent() != old)
			{
				gtk_xtext_recalc_widths(xtext->buffer, false);
				gtk_xtext_adjustment_set(xtext->buffer, true);
			}
			
			gtk_widget_queue_draw(widget);
			return false;
		}

		if (event->button == 1)
		{
			xtext->button_down = false;
			if (xtext->scroll_tag)
			{
				g_source_remove(xtext->scroll_tag);
				xtext->scroll_tag = 0;
			}

			gtk_grab_remove(widget);
			/*gdk_pointer_ungrab (0);*/

			/* got a new selection? */
			if (xtext->buffer->last_ent_start)
			{
				xtext->color_paste = false;
				if (event->state & STATE_CTRL || prefs.hex_text_autocopy_color)
					xtext->color_paste = true;
				if (prefs.hex_text_autocopy_text)
				{
					gtk_xtext_set_clip_owner(GTK_WIDGET(xtext), event);
				}
			}

			if (xtext->word_select || xtext->line_select)
			{
				xtext->word_select = false;
				xtext->line_select = false;
				gtk_widget_queue_draw(widget);
				return false;
			}

			if (xtext->select_start_x == event->x &&
				xtext->select_start_y == event->y &&
				xtext->buffer->last_ent_start)
			{
				gtk_xtext_unselect(*xtext);
				xtext->mark_stamp = false;
				gtk_widget_queue_draw(widget);
				return false;
			}

			if (!xtext->hilighting)
			{
				const auto word = gtk_xtext_get_word(xtext, event->x, event->y, 0, 0, 0, 0);
				g_signal_emit(G_OBJECT(xtext), xtext_signals[WORD_CLICK], 0, word ? word : nullptr, event);
			}
			else
			{
				xtext->hilighting = false;
			}
		}

		return false;
	}

	static gboolean
		gtk_xtext_button_press(GtkWidget * widget, GdkEventButton * event)
	{
		GtkXText *xtext = GTK_XTEXT(widget);
		GdkModifierType mask;
		int x, y;

		gdk_window_get_pointer(gtk_widget_get_window(widget), &x, &y, &mask);

		if (event->button == 3 || event->button == 2) /* right/middle click */
		{
			const auto word = gtk_xtext_get_word(xtext, x, y, nullptr, nullptr, nullptr, nullptr);
			if (word)
			{
				g_signal_emit(G_OBJECT(xtext), xtext_signals[WORD_CLICK], 0,
					word, event);
			}
			else
				g_signal_emit(G_OBJECT(xtext), xtext_signals[WORD_CLICK], 0,
				"", event);
			return false;
		}

		if (event->button != 1)		  /* we only want left button */
			return false;
		if (event->type == GDK_2BUTTON_PRESS)	/* WORD select */
		{
			gtk_xtext_selection_clear(xtext->buffer);
			textentry *ent;
			gtk_xtext_check_mark_stamp(xtext, mask);
			int offset, len;
			if (gtk_xtext_get_word(xtext, x, y, &ent, &offset, &len, nullptr))
			{
				if (len == 0)
					return false;
				
				ent->mark_start = offset;
				ent->mark_end = offset + len;
				xtext->buffer->last_ent_start = ent;
				xtext->buffer->last_ent_end = ent;
				xtext->word_select = true;
			}
			gtk_widget_queue_draw(widget);
			return false;
		}

		if (event->type == GDK_3BUTTON_PRESS)	/* LINE select */
		{
			textentry *ent;
			gtk_xtext_check_mark_stamp(xtext, mask);
			if (gtk_xtext_get_word(xtext, x, y, &ent, nullptr, nullptr, nullptr))
			{
				gtk_xtext_selection_clear(xtext->buffer);
				ent->mark_start = 0;
				ent->mark_end = ent->str.size();
				//gtk_xtext_selection_render(xtext, ent, ent);
				xtext->line_select = true;
			}
			gtk_widget_queue_draw(widget);
			return false;
		}

		/* check if it was a separator-bar click */
		if (xtext->separator && xtext->buffer->indent())
		{
			const auto line_x = xtext->buffer->indent() - ((xtext->space_width + 1) / 2);
			if (line_x == x || line_x == x + 1 || line_x == x - 1)
			{
				xtext->moving_separator = true;
				gtk_widget_queue_draw(widget);
				return false;
			}
		}

		xtext->button_down = true;
		xtext->select_start_x = x;
		xtext->select_start_y = y;
		xtext->select_start_adj = gtk_adjustment_get_value(xtext->adj);
		gtk_widget_queue_draw(widget);
		return false;
	}

	/* another program has claimed the selection */

	gboolean gtk_xtext_selection_kill(GtkXText *xtext, GdkEventSelection *)
	{
#ifndef WIN32
		if (xtext->buffer->last_ent_start)
			gtk_xtext_unselect(*xtext);
#else
		UNREFERENCED_PARAMETER(xtext);
#endif
		return true;
	}

	std::string gtk_xtext_selection_get_text(GtkXText *xtext)
	{
		auto buf = xtext->selection_buffer;
		if (!buf)
			return{};

		/* first find out how much we need to malloc ... */
		int len = 0;
		auto ent = buf->last_ent_start;
		while (ent)
		{
			if (ent->mark_start != -1)
			{
				/* include timestamp? */
				if (ent->mark_start == 0 && xtext->mark_stamp)
				{
					auto time_str = xtext_get_stamp_str(ent->stamp);
					len += time_str.size();
				}

				if (ent->mark_end - ent->mark_start > 0)
					len += (ent->mark_end - ent->mark_start) + 1;
				else
					len++;
			}
			if (ent == buf->last_ent_end)
				break;
			ent = ent->next;
		}

		if (len < 1)
			return{};

		/* now allocate mem and copy buffer */
		std::ostringstream out;
		std::ostream_iterator<char> pos{ out };
		ent = buf->last_ent_start;
		bool first = true;
		while (ent)
		{
			if (ent->mark_start != -1)
			{
				if (!first)
				{
					*pos = '\n';
					pos++;
				}
				first = false;
				if (ent->mark_end - ent->mark_start > 0)
				{
					/* include timestamp? */
					if (ent->mark_start == 0 && xtext->mark_stamp)
					{
						const auto time_str = xtext_get_stamp_str(ent->stamp);
						std::copy(time_str.cbegin(), time_str.cend(), pos);
					}

					std::copy_n(ent->str.cbegin() + ent->mark_start, ent->mark_end - ent->mark_start, pos);
				}
			}
			if (ent == buf->last_ent_end)
				break;
			ent = ent->next;
		}

		std::string stripped = out.str();
		if (!xtext->color_paste)
		{
			auto result = xtext::strip_color(xtext::ustring_ref(reinterpret_cast<const unsigned char*>(stripped.c_str()), stripped.size()), nullptr, false);
			stripped = std::string(result.cbegin(), result.cend());
		}
		return stripped;
	}

	/* another program is asking for our selection */

	static void
		gtk_xtext_selection_get(GtkWidget * widget,
		GtkSelectionData * selection_data_ptr,
		guint info, guint /*time*/)
	{
		const auto stripped = gtk_xtext_selection_get_text(GTK_XTEXT(widget));
		if (stripped.empty())
			return;

		switch (info)
		{
		case TARGET_UTF8_STRING:
			/* it's already in utf8 */
			gtk_selection_data_set_text(selection_data_ptr, stripped.c_str(), stripped.size());
			break;
		case TARGET_TEXT:
		case TARGET_COMPOUND_TEXT:
#ifdef GDK_WINDOWING_X11
		{
			GdkDisplay *display = gdk_window_get_display(gtk_widget_get_window(widget));
			GdkAtom encoding;
			gint format;
			gint new_length;
			guchar* new_text;

			gdk_x11_display_string_to_compound_text(display, stripped.c_str(), &encoding,
				&format, &new_text, &new_length);
			gtk_selection_data_set(selection_data_ptr, encoding, format,
				new_text, new_length);
			gdk_x11_free_compound_text(new_text);

		}
		break;
#endif
		default:
		{
			gsize glen;
			glib_string new_text{ g_locale_from_utf8(stripped.c_str(), stripped.size(), nullptr, &glen, nullptr) };
			gtk_selection_data_set(selection_data_ptr, GDK_SELECTION_TYPE_STRING,
				8, reinterpret_cast<unsigned char*>(new_text.get()), glen);
		} //switch scope
		}

	}

	static gboolean gtk_xtext_scroll(GtkWidget *widget, GdkEventScroll *event)
	{
		g_return_val_if_fail(widget, true);
		g_return_val_if_fail(GTK_IS_XTEXT(widget), true);
		auto adj = xtext_get_adjustments(GTK_XTEXT(widget));
		auto adj_value = gtk_adjustment_get_value(adj);
		const auto page_increment_dec = gtk_adjustment_get_page_increment(adj) / 10.0;
		if (event->direction == GDK_SCROLL_UP) /* mouse wheel pageUp */
		{
			adj_value -= page_increment_dec;
			const auto adj_lower = gtk_adjustment_get_lower(adj);
			if (adj_value < adj_lower)
				adj_value = adj_lower;
		}
		else if (event->direction ==
			 GDK_SCROLL_DOWN) /* mouse wheel pageDn */
		{
			adj_value += page_increment_dec;
			const auto diff_to_upper =
			    gtk_adjustment_get_upper(adj) -
			    gtk_adjustment_get_page_size(adj);
			if (adj_value > diff_to_upper)
				adj_value = diff_to_upper;
		}

		if (adj_value != gtk_adjustment_get_value(adj))
		{
			gtk_adjustment_set_value(adj, adj_value);
		}
		return false;
	}

	static void
		gtk_xtext_scroll_adjustments(GtkXText *xtext, GtkAdjustment *, GtkAdjustment *vadj)
	{
		/* hadj is ignored entirely */

		if (vadj)
			g_return_if_fail(GTK_IS_ADJUSTMENT(vadj));
		else
			vadj = GTK_ADJUSTMENT(gtk_adjustment_new(0.0, 0.0, 1.0, 1.0, 1.0, 1.0));


		if (xtext->adj && (xtext->adj != vadj))
		{
			g_signal_handlers_disconnect_by_func(xtext->adj,
				(void*)gtk_xtext_adjustment_changed,
				xtext);
			g_object_unref(xtext->adj);
		}

		if (xtext->adj != vadj)
		{
			xtext->adj = vadj;
			g_object_ref_sink(xtext->adj);

			xtext->vc_signal_tag = g_signal_connect(xtext->adj, "value-changed",
				G_CALLBACK(gtk_xtext_adjustment_changed),
				xtext);

			gtk_xtext_adjustment_changed(xtext->adj, xtext);
		}
	}

	/* strip MIRC colors and other attribs. */

	/* CL: needs to strip hidden when called by gtk_xtext_text_width, but not when copying text */
	struct chunk_t {
		std::vector<xtext::offlen_t> slp;
		int off1;
		int len1;
		int emph;
		xtext::offlen_t meta;
	};

	void xtext_do_chunk(chunk_t &c)
	{
		if (c.len1 == 0)
			return;

		xtext::offlen_t meta;
		meta.off = c.off1;
		meta.len = c.len1;
		meta.emph = c.emph;
		meta.width = 0;
		c.slp.emplace_back(meta);

		c.len1 = 0;
	}

	[[deprecated("uses the scratch buffer, use xtext::strip_color instead")]]
	unsigned char * gtk_xtext_strip_color(const ustring_ref & text, unsigned char *outbuf,
		int *newlen, std::vector<xtext::offlen_t> * slp, int strip_hidden)
	{
		chunk_t c;
		int i = 0;
		int rcol = 0, bgcol = 0;
		bool hidden = false;
		unsigned char *new_str;
		auto beginning = text.cbegin();
		if (outbuf == nullptr)
			new_str = static_cast<unsigned char*>(g_malloc0(text.size() + 2));
		else
			new_str = outbuf;

		c.off1 = 0;
		c.len1 = 0;
		c.emph = 0;
		std::locale locale;
		for (auto itr = text.cbegin(), end = text.cend(); itr != end;)
		{
			const auto mbl = charlen(itr); /* multi-byte length */
			if (mbl > std::distance(itr, end))
				break; // bad UTF-8

			if ((itr + 1) != end && rcol > 0 && (std::isdigit<char>(*itr, locale) || (*itr == ',' && std::isdigit<char>(itr[1], locale) && !bgcol)))
			{
				if (itr[1] != ',') rcol--;
				if (*itr == ',')
				{
					rcol = 2;
					bgcol = 1;
				}
			}
			else
			{
				rcol = bgcol = 0;
				switch (*itr)
				{
				case ATTR_COLOR:
					xtext_do_chunk(c);
					rcol = 2;
					break;
				case ATTR_BEEP:
				case ATTR_RESET:
				case ATTR_REVERSE:
				case ATTR_BOLD:
				case ATTR_UNDERLINE:
				case ATTR_ITALICS:
					xtext_do_chunk(c);
					if (*itr == ATTR_RESET)
						c.emph = 0;
					if (*itr == ATTR_ITALICS)
						c.emph ^= EMPH_ITAL;
					if (*itr == ATTR_BOLD)
						c.emph ^= EMPH_BOLD;
					break;
				case ATTR_HIDDEN:
					xtext_do_chunk(c);
					c.emph ^= EMPH_HIDDEN;
					hidden = !hidden;
					break;
				default:
					if (strip_hidden == 2 || (!(hidden && strip_hidden)))
					{
						if (c.len1 == 0)
							c.off1 = std::distance(beginning, itr);
						std::copy_n(itr, mbl, new_str + i);
						i += mbl;
						c.len1 += mbl;
					}
				}
			}
			itr += mbl;
		}

		//bad_utf8:		/* Normal ending sequence, and give up if bad utf8 */
		xtext_do_chunk(c);

		new_str[i] = 0;

		if (newlen)
			*newlen = i;

		if (slp)
			*slp = std::move(c.slp);

		return new_str;
	}

	/* gives width of a string, excluding the mIRC codes */
	static int gtk_xtext_text_width_ent(GtkXText *xtext, textentry &ent)
	{
		ent.slp.clear();
		const auto new_buf = xtext::strip_color(ent.str, &ent.slp, 2);
	 
		const auto width //= xtext->backend->get_string_width(ent.str, 2);
		   = backend_get_text_width_slp(xtext, new_buf.c_str(), ent.slp);

		for (auto &meta : ent.slp)
		{
			meta.width = backend_get_text_width_emph(
			    xtext,
			    ustring_ref(ent.str.c_str() + meta.off, meta.len),
			    meta.emph);
		}
		return width;
	}

	int gtk_xtext_text_width(GtkXText *xtext, const xtext::ustring_ref & text)
	{	
		return xtext->backend->get_string_width(text, !xtext->ignore_hidden);
	}

	void gtk_xtext_draw_underline(GtkXText *xtext, int dest_x, int dest_y,
				      int x, int y, int str_width,
				      bool drawable, cairo_t *cr)
	{
		cairo_stack cr_stack{ cr };
		cairo_new_path(cr);
		// used to check if the pixmap existed, why does this matter?
		if (drawable)
			y = dest_y + xtext->font->ascent + 1;
		else
		{
			y++;
			dest_x = x;
		}
		/* draw directly to window, it's out of the range of our
		* DB */
		gdk_cairo_set_source_color(cr, bridge_get_foreground(xtext->style));
		const auto mid_pixel_y = y + 0.5;
		cairo_move_to(cr, dest_x, mid_pixel_y);
		cairo_line_to(cr, dest_x + str_width - 1, mid_pixel_y);
		cairo_set_line_width(cr, 1.0);
		cairo_stroke(cr);
	}

	/* actually draw text to screen (one run with the same color/attribs) */
	//static int gtk_xtext_render_flush(GtkXText *xtext, cairo_t*cr, int x, int y,
	//				  const ustring_ref& str, int *emphasis)
	//{
	//	if (xtext->dont_render || str.empty() || xtext->hidden)
	//		return 0;

	//	const auto str_width = backend_get_text_width_emph(
	//	    xtext, str, *emphasis);

	//	if (xtext->dont_render2)
	//		return str_width;

	//	/* roll-your-own clipping (avoiding XftDrawString is always
	//	 * good!) */
	//	if (x > xtext->clip_x2 || x + str_width < xtext->clip_x)
	//		return str_width;
	//	if (y - xtext->font->ascent > xtext->clip_y2 ||
	//	    (y - xtext->font->ascent) + xtext->fontsize < xtext->clip_y)
	//		return str_width;

	//	int dest_x = 0, dest_y = 0;
	//	if (xtext->render_hilights_only)
	//	{
	//		if (!xtext->in_hilight) /* is it a hilight prefix? */
	//			return str_width;
	//		if (!xtext->un_hilight) /* doing a hilight? no need to
	//					   draw the text */
	//		{
	//			gtk_xtext_draw_underline(xtext, dest_x, dest_y, x, y, str_width, false, cr);
	//			return str_width;
	//		}
	//	}

	//	/*auto pix = gdk_pixmap_new(xtext->draw_buf, str_width,
	//			     xtext->fontsize, xtext->depth);
	//	if (pix)
	//	{
	//		dest_x = x;
	//		dest_y = y - xtext->font->ascent;

	//		gdk_gc_set_ts_origin(xtext->bgc, xtext->ts_x - x,
	//				     xtext->ts_y - dest_y);

	//		x = 0;
	//		y = xtext->font->ascent;
	//		xtext->draw_buf = pix;
	//	}*/

	//	backend_draw_text_emph(xtext, cr, x, y - xtext->font->ascent,
	//			       str, str_width,
	//			       *emphasis);
	//	return str_width;
	//}

	static void
		gtk_xtext_reset(GtkXText * xtext, bool mark, bool attribs)
	{
		if (attribs)
		{
			xtext->underline = false;
			xtext->hidden = false;
		}
		if (!mark)
		{
			xtext->backcolor = false;
			if (xtext->col_fore != XTEXT_FG)
				xtext_set_fg(xtext, XTEXT_FG);
			if (xtext->col_back != XTEXT_BG)
				xtext_set_bg(xtext, XTEXT_BG);
		}
		xtext->col_fore = XTEXT_FG;
		xtext->col_back = XTEXT_BG;
		xtext->parsing_color = false;
		xtext->parsing_backcolor = false;
		xtext->nc = 0;
	}

	/*
	* gtk_xtext_search_offset (buf, ent, off) --
	* Look for arg offset in arg textentry
	* Return one or more flags:
	* 	GTK_MATCH_MID if we are in a match
	* 	GTK_MATCH_START if we're at the first byte of it
	* 	GTK_MATCH_END if we're at the first byte past it
	* 	GTK_MATCH_CUR if it is the current match
	*/
	enum{
		GTK_MATCH_START = 1,
		GTK_MATCH_MID = 2,
		GTK_MATCH_END = 4,
		GTK_MATCH_CUR = 8
	};
	static int
		gtk_xtext_search_offset(xtext_buffer *buf, textentry *ent, unsigned int off)
	{
		offsets_t o;
		int flags = 0;

		for (auto gl = g_list_first(ent->marks); gl; gl = g_list_next(gl))
		{
			o.u = GPOINTER_TO_UINT(gl->data);
			if (off < o.o.start || off > o.o.end)
				continue;
			flags = GTK_MATCH_MID;
			if (off == o.o.start)
				flags |= GTK_MATCH_START;
			if (off == o.o.end)
			{
				gl = g_list_next(gl);
				if (gl)
				{
					o.u = GPOINTER_TO_UINT(gl->data);
					if (off == o.o.start)	/* If subseq match is adjacent */
					{
						flags |= (gl == buf->curmark) ? GTK_MATCH_CUR : 0;
					}
					else		/* If subseq match is not adjacent */
					{
						flags |= GTK_MATCH_END;
					}
				}
				else		/* If there is no subseq match */
				{
					flags |= GTK_MATCH_END;
				}
			}
			else if (gl == buf->curmark)	/* If not yet at the end of this match */
			{
				flags |= GTK_MATCH_CUR;
			}
			break;
		}
		return flags;
	}
#if 0
	/* render a single line, which WONT wrap, and parse mIRC colors */
	static bool
		gtk_xtext_render_str(GtkXText * xtext, cairo_t* cr, int y, textentry * ent,
		const xtext::ustring_ref str, const std::ptrdiff_t offset, int win_width, int indent,
		bool left_only, int *x_size_ret, int *emphasis)
	{
		cairo_stack cr_stack{ cr };
		int i = 0, x = indent, j = 0;
		auto pstr = str.cbegin();
		bool mark = false;
		bool ret = true;
		bool srch_underline = false;
		bool srch_mark = false;
		xtext->in_hilight = false;
		if (ent->mark_start != -1 &&
			ent->mark_start <= i + offset && ent->mark_end > i + offset)
		{
			xtext_set_bg(xtext, XTEXT_MARK_BG);
			xtext_set_fg(xtext, XTEXT_MARK_FG);
			xtext->backcolor = true;
			mark = true;
		}
		if (xtext->hilight_ent == ent &&
			xtext->hilight_start <= i + offset && xtext->hilight_end > i + offset)
		{
			if (!xtext->un_hilight)
			{
				xtext->underline = true;
			}
			xtext->in_hilight = true;
		}

		if (xtext->jump_in_offset > 0 && offset < xtext->jump_in_offset)
			xtext->dont_render2 = true;
		std::locale locale;
		const auto len = str.length();
		while (i < len)
		{

			if (xtext->hilight_ent == ent && xtext->hilight_start == (i + offset))
			{
				x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
				pstr += j;
				j = 0;
				if (!xtext->un_hilight)
				{
					xtext->underline = true;
				}

				xtext->in_hilight = true;
			}

			if ((xtext->parsing_color && std::isdigit<char>(str[i], locale) && xtext->nc < 2) ||
				(xtext->parsing_color && str[i] == ',' && std::isdigit<char>(str[i + 1], locale) && xtext->nc < 3 && !xtext->parsing_backcolor))
			{
				pstr++;
				if (str[i] == ',')
				{
					xtext->parsing_backcolor = true;
					if (xtext->nc)
					{
						xtext->num[xtext->nc] = 0;
						xtext->nc = 0;
						auto col_num = atoi(xtext->num);
						if (col_num == 99)	/* mIRC lameness */
							col_num = XTEXT_FG;
						else
							if (col_num > XTEXT_MAX_COLOR)
								col_num = col_num % XTEXT_MIRC_COLS;
						xtext->col_fore = col_num;
						if (!mark)
							xtext_set_fg(xtext, col_num);
					}
				}
				else
				{
					xtext->num[xtext->nc] = str[i];
					if (xtext->nc < 7)
						xtext->nc++;
				}
			}
			else
			{
				if (xtext->parsing_color)
				{
					xtext->parsing_color = false;
					if (xtext->nc)
					{
						xtext->num[xtext->nc] = 0;
						xtext->nc = 0;
						auto col_num = atoi(xtext->num);
						if (xtext->parsing_backcolor)
						{
							col_num = xtext::handle_mirc_oddness(col_num, XTEXT_BG);
							xtext->backcolor = col_num != XTEXT_BG;
							if (!mark)
								xtext_set_bg(xtext, col_num);
							xtext->col_back = col_num;
						}
						else
						{
							col_num = xtext::handle_mirc_oddness(col_num, XTEXT_FG);
							if (!mark)
								xtext_set_fg(xtext, col_num);
							xtext->col_fore = col_num;
						}
						xtext->parsing_backcolor = false;
					}
					else
					{
						/* got a \003<non-digit>... i.e. reset colors */
						x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
						pstr += j;
						j = 0;
						gtk_xtext_reset(xtext, mark, false);
					}
				}

				int k;
				if (!left_only && !mark &&
					(k = gtk_xtext_search_offset(xtext->buffer, ent, offset + i)))
				{
					x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
					pstr += j;
					j = 0;
					if (!(xtext->buffer->search_flags & highlight))
					{
						if (k & GTK_MATCH_CUR)
						{
							xtext_set_bg(xtext, XTEXT_MARK_BG);
							xtext_set_fg(xtext, XTEXT_MARK_FG);
							xtext->backcolor = true;
							srch_mark = true;
						}
						else
						{
							xtext_set_bg(xtext, xtext->col_back);
							xtext_set_fg(xtext, xtext->col_fore);
							xtext->backcolor = (xtext->col_back != XTEXT_BG) ? true : false;
							srch_mark = false;
						}
					}
					else
					{
						xtext->underline = (k & GTK_MATCH_CUR) ? true : false;
						if (k & (GTK_MATCH_START | GTK_MATCH_MID))
						{
							xtext_set_bg(xtext, XTEXT_MARK_BG);
							xtext_set_fg(xtext, XTEXT_MARK_FG);
							xtext->backcolor = true;
							srch_mark = true;
						}
						if (k & GTK_MATCH_END)
						{
							xtext_set_bg(xtext, xtext->col_back);
							xtext_set_fg(xtext, xtext->col_fore);
							xtext->backcolor = (xtext->col_back != XTEXT_BG) ? true : false;
							srch_mark = false;
							xtext->underline = false;
						}
						srch_underline = xtext->underline;
					}
				}
				int tmp;
				switch (str[i])
				{
				case '\n':
					/*case ATTR_BEEP:*/
					break;
				case ATTR_REVERSE:
					x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
					pstr += j + 1;
					j = 0;
					std::swap(xtext->col_fore, xtext->col_back);
					if (!mark)
					{
						xtext_set_fg(xtext, xtext->col_fore);
						xtext_set_bg(xtext, xtext->col_back);
					}
					if (xtext->col_back != XTEXT_BG)
						xtext->backcolor = true;
					else
						xtext->backcolor = false;
					break;
				case ATTR_BOLD:
					x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
					*emphasis ^= EMPH_BOLD;
					pstr += j + 1;
					j = 0;
					break;
				case ATTR_UNDERLINE:
					x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
					xtext->underline = !xtext->underline;
					pstr += j + 1;
					j = 0;
					break;
				case ATTR_ITALICS:
					x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
					*emphasis ^= EMPH_ITAL;
					pstr += j + 1;
					j = 0;
					break;
				case ATTR_HIDDEN:
					x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
					xtext->hidden = (!xtext->hidden) && (!xtext->ignore_hidden);
					pstr += j + 1;
					j = 0;
					break;
				case ATTR_RESET:
					x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
					*emphasis = 0;
					pstr += j + 1;
					j = 0;
					gtk_xtext_reset(xtext, mark, !xtext->in_hilight);
					break;
				case ATTR_COLOR:
					x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
					xtext->parsing_color = true;
					pstr += j + 1;
					j = 0;
					break;
				default:
					tmp = charlen(str.cbegin() + i);
					/* invalid utf8 safe guard */
					if (tmp + i > len)
						tmp = len - i;
					j += tmp;	/* move to the next utf8 char */
				}
			}
			i += charlen(str.cbegin() + i);	/* move to the next utf8 char */
			/* invalid utf8 safe guard */
			if (i > len)
				i = len;

			/* Separate the left part, the space and the right part
			into separate runs, and reset bidi state inbetween.
			Perform this only on the first line of the message.
			*/
			if (offset == 0)
			{
				/* we've reached the end of the left part? */
				if (std::distance(str.cbegin(), pstr) + j == ent->left_len)
				{
					x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
					pstr += j;
					j = 0;
				}
				else if (std::distance(str.cbegin(), pstr) + j == ent->left_len + 1)
				{
					x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
					pstr += j;
					j = 0;
				}
			}

			/* have we been told to stop rendering at this point? */
			if (xtext->jump_out_offset > 0 && xtext->jump_out_offset <= (i + offset))
			{
				gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
				ret = false;	/* skip the rest of the lines, we're done. */
				j = 0;
				break;
			}

			if (xtext->jump_in_offset > 0 && xtext->jump_in_offset == (i + offset))
			{
				x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
				pstr += j;
				j = 0;
				xtext->dont_render2 = false;
			}

			if (xtext->hilight_ent == ent && xtext->hilight_end == (i + offset))
			{
				x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
				pstr += j;
				j = 0;
				xtext->underline = false;
				xtext->in_hilight = false;
				if (xtext->render_hilights_only)
				{
					/* stop drawing this ent */
					ret = false;
					break;
				}
			}

			if (!mark && ent->mark_start == (i + offset))
			{
				x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
				pstr += j;
				j = 0;
				xtext_set_bg(xtext, XTEXT_MARK_BG);
				xtext_set_fg(xtext, XTEXT_MARK_FG);
				xtext->backcolor = true;
				if (srch_underline)
				{
					xtext->underline = false;
					srch_underline = false;
				}
				mark = true;
			}

			if (mark && ent->mark_end == (i + offset))
			{
				x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
				pstr += j;
				j = 0;
				xtext_set_bg(xtext, xtext->col_back);
				xtext_set_fg(xtext, xtext->col_fore);
				if (xtext->col_back != XTEXT_BG)
					xtext->backcolor = true;
				else
					xtext->backcolor = false;
				mark = false;
			}

		}

		if (j)
			x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);

		if (mark || srch_mark)
		{
			xtext_set_bg(xtext, xtext->col_back);
			xtext_set_fg(xtext, xtext->col_fore);
			if (xtext->col_back != XTEXT_BG)
				xtext->backcolor = true;
			else
				xtext->backcolor = false;
		}

		xtext->dont_render2 = false;

		/* return how much we drew in the x direction */
		if (x_size_ret)
			*x_size_ret = x - indent;

		return ret;
	}
#endif

	/* walk through str until this line doesn't fit anymore */

	static int
		find_next_wrap(GtkXText * xtext, const textentry & ent, const unsigned char str[],
		int win_width, int indent)
	{
		/* single liners */
		if (win_width >= ent.pixel_width() + ent.indent)
			return ent.str.size();

		auto last_space = str;
		auto orig_str = str;
		int str_width = indent;
		int rcol = 0, bgcol = 0;
		bool hidden = false;
		int ret;
		int limit_offset = 0;
		int emphasis = 0;
		std::locale locale;
		/* it does happen! */
		if (win_width < 1)
		{
			ret = ent.str.size() - std::distance(ent.str.c_str(), str);
			goto done;
		}

		/* Find emphasis value for the offset that is the first byte of our string */
		for (const auto & meta : ent.slp)
		{
			auto start = ent.str.c_str() + meta.off;
			auto end = start + meta.len;
			if (str >= start && str < end)
			{
				emphasis = meta.emph;
				break;
			}
		}

		for (;;)
		{
			if (rcol > 0 && (std::isdigit<char>(*str, locale) || (*str == ',' && std::isdigit<char>(str[1], locale) && !bgcol)))
			{
				if (str[1] != ',') rcol--;
				if (*str == ',')
				{
					rcol = 2;
					bgcol = 1;
				}
				limit_offset++;
				str++;
			}
			else
			{
				rcol = bgcol = 0;
				switch (*str)
				{
				case ATTR_COLOR:
					rcol = 2;
				case ATTR_BEEP:
				case ATTR_RESET:
				case ATTR_REVERSE:
				case ATTR_BOLD:
				case ATTR_UNDERLINE:
				case ATTR_ITALICS:
					if (*str == ATTR_RESET)
						emphasis = 0;
					if (*str == ATTR_ITALICS)
						emphasis ^= EMPH_ITAL;
					if (*str == ATTR_BOLD)
						emphasis ^= EMPH_BOLD;
					limit_offset++;
					str++;
					break;
				case ATTR_HIDDEN:
					if (xtext->ignore_hidden)
						goto def;
					hidden = !hidden;
					limit_offset++;
					str++;
					break;
				default:
				def :
				{
					int mbl = static_cast<int>(charlen(str));
					int char_width = backend_get_text_width_emph(xtext, ustring_ref(str, mbl), emphasis);
					if (!hidden) str_width += char_width;
					if (str_width > win_width)
					{
						if (xtext->wordwrap)
						{
							if (std::distance(last_space, str) > WORDWRAP_LIMIT + limit_offset)
								ret = std::distance(orig_str, str); /* fall back to character wrap */
							else
							{
								if (*last_space == ' ')
									last_space++;
								ret = std::distance(orig_str, last_space);
								if (ret == 0) /* fall back to character wrap */
									ret = std::distance(orig_str, str);
							}
							goto done;
						}
						ret = std::distance(orig_str, str);
						goto done;
					}

					/* keep a record of the last space, for word wrapping */
					if (is_del(*str))
					{
						last_space = str;
						limit_offset = 0;
					}

					/* progress to the next char */
					str += mbl;
				} // switch statement scope

				}
			}

			if (str >= ent.str.c_str() + ent.str.size())
			{
				ret = std::distance(orig_str, str);
				break; // goto done;
			}
		}

	done:

		/* must make progress */
		if (ret < 1)
			ret = 1;

		return ret;
	}

	/* find the offset, in bytes, that wrap number 'line' starts at */
	int gtk_xtext_find_subline(const textentry &ent, int line)
	{
		int rlen = 0;

		if (line > 0 && line <= ent.sublines.size())
		{
			rlen = ent.sublines[line - 1];
			if (rlen == 0)
				rlen = ent.str.size();
		}
		return rlen;
	}

	/* horrible hack for drawing time stamps */
	void gtk_xtext_render_stamp(GtkXText * xtext, cairo_t* cr,
		const boost::string_ref & text, int line, int win_width)
	{
		textentry tmp_ent(xtext->backend->make_layout(xtext::ustring{ text.cbegin(), text.cend() }, xtext->buffer->indent()));
		auto jo = xtext->jump_out_offset;	/* back these up */
		auto ji = xtext->jump_in_offset;
		auto hs = xtext->hilight_start;
		xtext->jump_out_offset = 0;
		xtext->jump_in_offset = 0;
		xtext->hilight_start = 0xffff;	/* temp disable */

		if (xtext->mark_stamp)
		{
			/* if this line is marked, mark this stamp too */
			if (tmp_ent.mark_start == 0)
			{
				tmp_ent.mark_start = 0;
				tmp_ent.mark_end = text.size();
			}
			else
			{
				tmp_ent.mark_start = -1;
				tmp_ent.mark_end = -1;				
			}
			tmp_ent.str = xtext::ustring{ text.cbegin(), text.cend() };
		}

		//int xsize, emphasis = 0;
		const auto y = (xtext->fontsize * line)/* + xtext->font->ascent*/ - xtext->pixel_offset;
		xtext->backend->render_at(cr, 0, y, win_width, 0, tmp_ent.mark_start, tmp_ent.mark_end, xtext::left, { reinterpret_cast<unsigned const char*>(text.data()), text.size() });
		/*gtk_xtext_render_str(xtext, cr, y, &tmp_ent, { reinterpret_cast<unsigned const char*>(text.data()), text.size() }, 0,
			win_width, 2, true, &xsize, &emphasis);*/

		/* restore everything back to how it was */
		xtext->jump_out_offset = jo;
		xtext->jump_in_offset = ji;
		xtext->hilight_start = hs;

		/* with a non-fixed-width font, sometimes we don't draw enough
		background i.e. when this stamp is shorter than xtext->stamp_width */
		//xsize += MARGIN;
		//if (xsize < xtext->stamp_width)
		//{
		//	y -= xtext->font->ascent;
		//	//xtext_draw_bg(xtext,
		//	//	xsize,	/* x */
		//	//	y,			/* y */
		//	//	xtext->stamp_width - xsize,	/* width */
		//	//	xtext->fontsize					/* height */);
		//}
	}

	/* render a single line, which may wrap to more lines */
	int gtk_xtext_render_line(GtkXText * xtext, cairo_t* cr, textentry * ent, int line,
		int lines_max, int subline, int win_width)
	{
		int taken, len, y;
		//int emphasis = 0;

		//auto entline = taken = 0;
		//auto str = ent->str.c_str();
		const auto indent = (xtext->buffer->indent() - ent->indent);
		const auto start_subline = subline;
		
		/* draw the timestamp */
		if (xtext->auto_indent && xtext->buffer->is_time_stamped() &&
			(!xtext->skip_stamp || xtext->mark_stamp || xtext->force_stamp))
		{
			const auto time_str = xtext_get_stamp_str(ent->stamp);
			gtk_xtext_render_stamp(xtext, cr, time_str, line, win_width);
		}

		/* draw each line one by one */
		y = (xtext->fontsize * line) /* + xtext->font->ascent*/ - xtext->pixel_offset;
		const auto real_x = xtext->buffer->indent() - indent;
		return xtext->backend->render_at(cr, real_x, y, win_width - real_x, 0 - indent, ent->mark_start, ent->mark_end, xtext::left, ent->str);
		//do
		//{
		//	
		//	if (entline > 0)
		//		len = ent->sublines[entline] - ent->sublines[entline - 1];
		//	else
		//		len = ent->sublines[entline];

		//	entline++;

		//	
		//	if (!subline)
		//	{
		//		
		//		/*if (!gtk_xtext_render_str(xtext, cr, y, ent, xtext::ustring_ref( str,  len ), std::distance(ent->str.c_str(), str), win_width,
		//			indent, false, nullptr, &emphasis))*/
		//		if()
		//		{
		//			/* small optimization */
		//			gtk_xtext_draw_marker(xtext, cr, ent, y - xtext->fontsize * (taken + start_subline + 1));
		//			return ent->sublines.size() - subline;
		//		}
		//	}
		//	else
		//	{
		//		xtext->dont_render = true;
		//		xtext->backend->render_at(cr, indent, y, win_width - indent, ent->mark_start, ent->mark_end, xtext::xtext_backend::left, xtext::ustring_ref(str, len));
		//		/*gtk_xtext_render_str(xtext, cr, y, ent,xtext::ustring_ref ( str, len ), std::distance(ent->str.c_str(), str), win_width,
		//			indent, false, nullptr, &emphasis);*/
		//		xtext->dont_render = false;
		//		subline--;
		//		line--;
		//		taken--;
		//	}

		//	indent = xtext->buffer->indent();

		//	if (line >= lines_max)
		//		break;

		//} while (str < ent->str.c_str() + ent->str.size());

		gtk_xtext_draw_marker(xtext, cr, ent, y - xtext->fontsize * (taken + start_subline));

		return taken;
	}

	static void gtk_xtext_recalc_widths(xtext_buffer *buf,
					    bool do_str_width)
	{
		const auto xtext = buf->current_xtext();
		/* since we have a new font, we have to recalc the text widths
		 */
		for (auto &ent : buf->entries)
		{
			if (do_str_width)
			{
				ent.str_width = xtext->backend->get_string_width(ent.str, 2);
			}
			if (ent.left_len != -1)
			{
				ent.indent = (buf->indent() -
					      gtk_xtext_text_width(
						  xtext,
						  xtext::ustring_ref(ent.str.c_str(),
							      ent.left_len))) -
					     xtext->space_width;
				if (ent.indent < MARGIN)
					ent.indent = MARGIN;
			}
		}

		gtk_xtext_calc_lines(buf, false);
	}

	/* count how many lines 'ent' will take (with wraps) */

	int gtk_xtext_lines_taken(xtext_buffer *buf, textentry & ent)
	{
		ent.sublines.clear();
		const auto win_width = buf->rendered_width();

		if (win_width >= ent.indent + ent.pixel_width())
		{
			ent.sublines.push_back(ent.str.size());
			return 1;
		}

		int indent = ent.indent;
		auto str = ent.str.c_str();
		auto xtext = buf->current_xtext();
		do
		{
			int len = find_next_wrap(xtext, ent, str, win_width, indent);
			ent.sublines.push_back(str + len - ent.str.c_str());
			indent = buf->indent();
			str += len;
		} while (str < ent.str.c_str() + ent.str.size());

		return ent.sublines.size();
	}

	/* Calculate number of actual lines (with wraps), to set adj->lower. *
	* This should only be called when the window resizes.               */
	void gtk_xtext_calc_lines(xtext_buffer *buf, bool fire_signal)
	{
		const auto xtext = buf->current_xtext();
		auto window = gtk_widget_get_window(GTK_WIDGET(xtext));
		const auto height = gdk_window_get_height(window);
		const auto width = gdk_window_get_width(window) - MARGIN;
		if (width < 30 || height < xtext->fontsize ||
		    width < buf->indent() + 30)
			return;

		buf->num_lines = std::accumulate(
			buf->entries.begin(),
			buf->entries.end(),
			0, 
			[buf](auto val, auto& next) {
			return val + gtk_xtext_lines_taken(buf, next);
		});

		buf->pagetop_ent = nullptr;
		gtk_xtext_adjustment_set(buf, fire_signal);
	}

	/* find the n-th line in the linked list, this includes wrap calculations */
	textentry * gtk_xtext_nth(GtkXText *xtext, int line, int &subline)
	{
		int lines = 0;
		textentry * ent = xtext->buffer->text_first;

		/* -- optimization -- try to make a short-cut using the pagetop ent */
		if (xtext->buffer->pagetop_ent)
		{
			if (line == xtext->buffer->pagetop_line)
			{
				subline = xtext->buffer->pagetop_subline;
				return xtext->buffer->pagetop_ent;
			}
			if (line > xtext->buffer->pagetop_line)
			{
				/* lets start from the pagetop instead of the absolute beginning */
				ent = xtext->buffer->pagetop_ent;
				lines = xtext->buffer->pagetop_line - xtext->buffer->pagetop_subline;
			}
			else if (line > xtext->buffer->pagetop_line - line)
			{
				/* move backwards from pagetop */
				ent = xtext->buffer->pagetop_ent;
				lines = xtext->buffer->pagetop_line - xtext->buffer->pagetop_subline;
				for (;;)
				{
					if (lines <= line)
					{
						subline = line - lines;
						return ent;
					}
					ent = ent->prev;
					if (!ent)
						break;
					lines -= ent->sublines.size();
				}
				return nullptr;
			}
		}
		/* -- end of optimization -- */

		while (ent)
		{
			lines += ent->sublines.size();
			if (lines > line)
			{
				subline = ent->sublines.size() - (lines - line);
				return ent;
			}
			ent = ent->next;
		}
		return nullptr;
	}

	/* render a whole page/window, starting from 'startline' */
	void gtk_xtext_render_page(GtkXText * xtext, cairo_t * cr)
	{
		cairo_stack cr_stack{ cr };

		GtkWidget * widget = GTK_WIDGET(xtext);
		GtkAllocation allocation;
		gtk_widget_get_allocation(widget, &allocation);

		if (allocation.width < 34 ||
			allocation.height < xtext->fontsize ||
			allocation.width < xtext->buffer->indent() + 32)
		{
			return;
		}
		const auto adj_value = gtk_adjustment_get_value(xtext->adj);
		//truncate the adjustment to get the first line in the buffer to render
		const auto firstLineToRender = static_cast<int>(adj_value);
		xtext->pixel_offset = (adj_value - firstLineToRender) * xtext->fontsize;

		int subline = 0;
		int line = 0;
		auto ent = xtext->buffer->text_first;

		if (firstLineToRender > 0)
			ent = gtk_xtext_nth(xtext, firstLineToRender, subline);

		xtext->buffer->pagetop_ent = ent;
		xtext->buffer->pagetop_subline = subline;
		xtext->buffer->pagetop_line = firstLineToRender;

		if (xtext->buffer->num_lines <= gtk_adjustment_get_page_size(xtext->adj)) {
			xtext->buffer->dontscroll();
		}

		const auto pos = adj_value * xtext->fontsize;
		const auto overlap = xtext->buffer->last_pixel_pos - pos;
		xtext->buffer->last_pixel_pos = pos;

#ifndef __APPLE__
		if (!xtext->pixmap && std::abs(overlap) < allocation.height)
		{
			GdkRectangle area = {};
			cairo_stack cr_stack1{ cr };
			cairo_new_path(cr);
			if (overlap < 1)	/* DOWN */
			{
				gdk_cairo_set_source_pixmap(cr, xtext->pixmap, 0.0, 0.0);
				auto remainder = ((allocation.height - xtext->font->descent) % xtext->fontsize) +
					xtext->font->descent;
				area.y = (allocation.height + overlap) - remainder;
				area.height = remainder - overlap;
			}
			else
			{
				gdk_cairo_set_source_color(cr, &xtext->palette[XTEXT_FG]);
				area.y = 0;
				area.height = overlap;
			}
			cairo_rectangle(cr, 0.0, 0.0, allocation.width, allocation.height);
			cairo_fill(cr);

			if (area.height > 0)
			{
				area.x = 0;
				area.width = allocation.width;
			}

			return;
		}
#endif
		gtk_xtext_reset(xtext, false, true);
		gdk_cairo_set_source_color(cr, bridge_get_foreground(xtext->style));
		const auto width = allocation.width - MARGIN;
		const auto height = allocation.height;
		const auto lines_max = ((height + xtext->pixel_offset) / xtext->fontsize) + 1;

		while (ent)
		{
			line += gtk_xtext_render_line(xtext, cr, ent, line, lines_max,
				subline, width);
			subline = 0;

			if (line >= lines_max)
				break;

			ent = ent->next;
		}

		line = (xtext->fontsize * line) - xtext->pixel_offset;

		/* draw the separator line */
		gtk_xtext_draw_sep(xtext, cr, allocation.height);
	}

	bool gtk_xtext_kill_ent(xtext_buffer *buffer, textentry *ent)
	{
		const auto xtext = buffer->current_xtext();
		/* Set visible to true if this is the current buffer */
		/* and this ent shows up on the screen now */
		const bool visible = xtext->buffer == buffer &&
			gtk_xtext_check_ent_visibility(xtext, ent, 0);

		if (ent == buffer->pagetop_ent)
			buffer->pagetop_ent = nullptr;

		if (ent == buffer->last_ent_start)
		{
			buffer->last_ent_start = ent->next;
			buffer->last_offset_start = 0;
		}

		if (ent == buffer->last_ent_end)
		{
			buffer->last_ent_start = nullptr;
			buffer->last_ent_end = nullptr;
		}

		if (buffer->marker_pos == ent)
		{
			/* Allow for "Marker line reset because exceeded scrollback limit. to appear. */
			buffer->marker_pos = ent->next;
			buffer->marker_state = MARKER_RESET_BY_KILL;
		}

		if (ent->marks)
		{
			gtk_xtext_search_textentry_del(buffer, ent);
		}

		return visible;
	}

	/* remove the topline from the list */
	void gtk_xtext_remove_top(xtext_buffer *buffer)
	{
		auto ent = buffer->text_first;
		if (!ent)
			return;
		if (buffer->entries.empty())
		{
			throw std::runtime_error("attempt to remove without anything in the store!");
		}
		buffer->num_lines -= ent->sublines.size();
		buffer->pagetop_line -= ent->sublines.size();
		const auto xtext = buffer->current_xtext();
		buffer->last_pixel_pos -= (ent->sublines.size() * xtext->fontsize);
		buffer->text_first = ent->next;
		if (buffer->text_first)
			buffer->text_first->prev = nullptr;
		else
			buffer->text_last = nullptr;

		buffer->old_value -= ent->sublines.size();
		if (xtext->buffer ==
		    buffer) /* is it the current buffer? */
		{
			gtk_adjustment_set_value(
			    xtext->adj,
			    gtk_adjustment_get_value(xtext->adj) -
				ent->sublines.size());
			xtext->select_start_adj -= ent->sublines.size();
		}

		if (gtk_xtext_kill_ent(buffer, ent))
		{
			if (!xtext->add_io_tag)
			{
				/* remove scrolling events */
				if (xtext->io_tag)
				{
					g_source_remove(xtext->io_tag);
					xtext->io_tag = 0;
				}
				xtext->force_render = true;
				xtext->add_io_tag = g_timeout_add(REFRESH_TIMEOUT * 2,
					(GSourceFunc)
					gtk_xtext_render_page_timeout,
					xtext);
			}
		}
		buffer->entries.pop_front();
	}

	static void
		gtk_xtext_remove_bottom(xtext_buffer *buffer)
	{
		auto ent = buffer->text_last;
		if (!ent)
			return;
		if (buffer->entries.empty())
		{
			throw std::runtime_error("attempt to remove without anything in the store!");
		}
		buffer->num_lines -= ent->sublines.size();
		buffer->text_last = ent->prev;
		if (buffer->text_last)
			buffer->text_last->next = nullptr;
		else
			buffer->text_first = nullptr;
		const auto xtext = buffer->current_xtext();
		if (gtk_xtext_kill_ent(buffer, ent))
		{
			if (!xtext->add_io_tag)
			{
				/* remove scrolling events */
				if (xtext->io_tag)
				{
					g_source_remove(xtext->io_tag);
					xtext->io_tag = 0;
				}
				xtext->force_render = true;
				xtext->add_io_tag = g_timeout_add(REFRESH_TIMEOUT * 2,
					(GSourceFunc)
					gtk_xtext_render_page_timeout,
					xtext);
			}
		}
		buffer->entries.pop_back();
	}

	static bool
		gtk_xtext_check_ent_visibility(GtkXText * xtext, textentry *find_ent, int add)
	{
		if (!find_ent)
		{
			return false;
		}

		const auto height = gdk_window_get_height(gtk_widget_get_window(GTK_WIDGET(xtext)));
		xtext_buffer *buf = xtext->buffer;
		auto ent = buf->pagetop_ent;
		/* If top line not completely displayed return false */
		if (ent == find_ent && buf->pagetop_subline > 0)
		{
			return false;
		}
		/* Loop through line positions looking for find_ent */
		auto lines = ((height + xtext->pixel_offset) / xtext->fontsize) + buf->pagetop_subline + add;
		while (ent)
		{
			lines -= ent->sublines.size();
			if (lines <= 0)
			{
				return false;
			}
			if (ent == find_ent)
			{
				return true;
			}
			ent = ent->next;
		}

		return false;
	}

	static void gtk_xtext_unstrip_color(gint start, gint end, const std::vector<xtext::offlen_t>& slp, GList **gl, gint maxo)
	{
		auto off1 = 0;
		auto curlen = 0;
		for (const auto & meta : slp)
		{
			if (start < meta.len)
			{
				off1 = meta.off + start;
				break;
			}
			curlen += meta.len;
			start -= meta.len;
			end -= meta.len;
		}

		auto off2 = off1;
		auto meta = slp.cbegin(), end_itr = slp.cend();
		for (; meta != end_itr; ++meta)
		{
			if (end < meta->len)
			{
				off2 = meta->off + end;
				break;
			}
			curlen += meta->len;
			end -= meta->len;
		}
		if (meta == end_itr)
		{
			off2 = maxo;
		}
		offsets_t marks = {};
		marks.o.start = off1;
		marks.o.end = off2;
		*gl = g_list_append(*gl, GUINT_TO_POINTER(marks.u));
	}

	/* Search a single textentry for occurrence(s) of search arg string */
	static GList * gtk_xtext_search_textentry(xtext_buffer *buf, const textentry &ent)
	{
		if (buf->search_text.empty())
		{
			return nullptr;
		}
		gint lstr;
		std::vector<xtext::offlen_t> slp;
		const auto xtext = buf->current_xtext();
		/* text string to be searched */
		auto str = (gchar*)gtk_xtext_strip_color(ent.str, xtext->scratch_buffer,
			&lstr, &slp, !xtext->ignore_hidden);
		GList *gl = nullptr;
		/* Regular-expression matching --- */
		if (buf->search_flags & regexp)
		{
			if (buf->search_re == nullptr)
			{
				return gl;
			}
			GMatchInfo *gmi;
			gint start, end;
			g_regex_match(buf->search_re, str, GRegexMatchFlags(), &gmi);
			while (g_match_info_matches(gmi))
			{
				g_match_info_fetch_pos(gmi, 0, &start, &end);
				gtk_xtext_unstrip_color(start, end, slp, &gl, ent.str.size());
				g_match_info_next(gmi, nullptr);
			}
			g_match_info_free(gmi);

			/* Non-regular-expression matching --- */
		}
		else {
			gchar *pos;
			gint len;
			gint match = buf->search_flags & case_match;

			glib_string hay(match ? g_strdup(str) : g_utf8_casefold(str, lstr));
			auto lhay = std::strlen(hay.get());
			auto off = 0;

			for (pos = hay.get(), len = lhay; len;
				off += buf->search_nee.size(), pos = hay.get() + off, len = lhay - off)
			{
				str = g_strstr_len(pos, len, buf->search_nee.c_str());
				if (!str)
				{
					break;
				}
				off = str - hay.get();
				gtk_xtext_unstrip_color(off, off + buf->search_nee.size(),
					slp, &gl, ent.str.size());
			}
		}

		return gl;
	}

	/* Add a list of found search results to an entry, maybe nullptr */
	static void gtk_xtext_search_textentry_add(xtext_buffer *buf, textentry *ent, GList *gl, bool pre)
	{
		ent->marks = gl;
		if (gl)
		{
			buf->search_found = (pre ? g_list_prepend : g_list_append) (buf->search_found, ent);
			if (!pre && buf->hintsearch == nullptr)
			{
				buf->hintsearch = ent;
			}
		}
	}

	/* Free all search information for a textentry */
	static void
		gtk_xtext_search_textentry_del(xtext_buffer *buf, textentry *ent)
	{
		g_list_free(ent->marks);
		ent->marks = nullptr;
		if (buf->cursearch && buf->cursearch->data == ent)
		{
			buf->cursearch = nullptr;
			buf->curmark = nullptr;
			buf->curdata.u = 0;
		}
		if (buf->pagetop_ent == ent)
		{
			buf->pagetop_ent = nullptr;
		}
		if (buf->hintsearch == ent)
		{
			buf->hintsearch = nullptr;
		}
		buf->search_found = g_list_remove(buf->search_found, ent);
	}

	/* Used only by glist_foreach */
	static void
		gtk_xtext_search_textentry_fini(gpointer entp, gpointer)
	{
		textentry *ent = static_cast<textentry *>(entp);

		g_list_free(ent->marks);
		ent->marks = nullptr;
	}

	/* Free all search information for all textentrys and the xtext_buffer */
	static void
		gtk_xtext_search_fini(xtext_buffer *buf)
	{
		g_list_foreach(buf->search_found, gtk_xtext_search_textentry_fini, 0);
		g_list_free(buf->search_found);
		buf->search_found = nullptr;
		buf->search_text.clear();
		buf->search_nee.clear();
		buf->search_flags = gtk_xtext_search_flags();
		buf->cursearch = nullptr;
		buf->curmark = nullptr;
		/* but leave buf->curdata.u alone! */
		if (buf->search_re)
		{
			g_regex_unref(buf->search_re);
			buf->search_re = nullptr;
		}
	}

	/* Returns true if the base search information exists and is still okay to use */
	static bool
		gtk_xtext_search_init(xtext_buffer *buf, const gchar *text, gtk_xtext_search_flags flags, GError **perr)
	{
		/* Of the five flags, backward and highlight_all do not need a new search */
		if (buf->search_found && buf->search_text == text &&
			(buf->search_flags & case_match) == (flags & case_match) &&
			(buf->search_flags & follow) == (flags & follow) &&
			(buf->search_flags & regexp) == (flags & regexp))
		{
			return true;
		}
		buf->hintsearch = static_cast<textentry *>(buf->cursearch ? buf->cursearch->data : nullptr);
		gtk_xtext_search_fini(buf);
		buf->search_text = text;
		if (flags & regexp)
		{
			buf->search_re = g_regex_new(text, (flags & case_match) ? GRegexCompileFlags() : G_REGEX_CASELESS, GRegexMatchFlags(), perr);
			if (perr && *perr)
			{
				return false;
			}
		}
		else
		{
			if (flags & case_match)
			{
				buf->search_nee = text;
			}
			else
			{
				glib_string folded{ g_utf8_casefold(text, strlen(text)) };
				buf->search_nee = folded.get();
			}
		}
		buf->search_flags = flags;
		buf->cursearch = nullptr;
		buf->curmark = nullptr;
		/* but leave buf->curdata.u alone! */
		return false;
	}

	static int
		gtk_xtext_render_page_timeout(GtkXText * xtext)
	{
		GtkAdjustment *adj = xtext->adj;

		xtext->add_io_tag = 0;

		/* less than a complete page? */
		const auto page_size = gtk_adjustment_get_page_size(adj);
		if (xtext->buffer->num_lines <= page_size)
		{
			xtext->buffer->old_value = 0.0;
			g_signal_handler_block(xtext->adj, xtext->vc_signal_tag);
			gtk_adjustment_set_value(adj, 0.0);
			g_signal_handler_unblock(xtext->adj, xtext->vc_signal_tag);
		}
		else if (xtext->buffer->scrollbar_down)
		{
			g_signal_handler_block(xtext->adj, xtext->vc_signal_tag);
			gtk_xtext_adjustment_set(xtext->buffer, false);
			gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj) - page_size);
			g_signal_handler_unblock(xtext->adj, xtext->vc_signal_tag);
			xtext->buffer->old_value = gtk_adjustment_get_value(adj);
		}
		else
		{
			gtk_xtext_adjustment_set(xtext->buffer, true);
			if (xtext->force_render)
			{
				xtext->force_render = false;
			}
		}
		gtk_widget_queue_draw(GTK_WIDGET(xtext));
		return 0;
	}

	/* append a textentry to our linked list */
	//static void gtk_xtext_append_entry(xtext_buffer *buf, const textentry && ent, time_t stamp) = delete;

	static void gtk_xtext_append_entry(xtext_buffer *buf, textentry ent, time_t stamp)
	{
		/* we don't like tabs */
		std::replace(ent.str.begin(), ent.str.end(), '\t', ' ');

		ent.stamp = stamp;
		if (stamp == 0)
			ent.stamp = std::time(nullptr);
		const auto xtext = buf->current_xtext();
		ent.str_width = gtk_xtext_text_width_ent(xtext, ent);
		ent.mark_start = -1;
		ent.mark_end = -1;
		ent.next = nullptr;
		ent.marks = nullptr;

		if (ent.indent < MARGIN)
			ent.indent = MARGIN;	  /* 2 pixels is the left margin */

		buf->entries.emplace_back(std::move(ent));
		textentry * ent_ptr = &buf->entries.back();
		/* append to our linked list */
		if (buf->text_last)
			buf->text_last->next = ent_ptr;
		else
			buf->text_first = ent_ptr;
		ent_ptr->prev = buf->text_last;
		buf->text_last = ent_ptr;

		buf->num_lines += gtk_xtext_lines_taken(buf, *ent_ptr);
		if ((buf->marker_pos == nullptr || buf->marker_seen) && (xtext->buffer != buf ||
			!gtk_window_has_toplevel_focus(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(xtext))))))
		{
			buf->marker_pos = ent_ptr;
			buf->marker_state = MARKER_IS_SET;
			buf->dontscroll(); /* force scrolling off */
			buf->marker_seen = false;
		}

		if (xtext->max_lines > 2 && xtext->max_lines < buf->num_lines)
		{
			gtk_xtext_remove_top(buf);
		}
		const auto page_size = gtk_adjustment_get_page_size(xtext->adj);
		if (xtext->buffer == buf)
		{
			/* this could be improved */
			if ((buf->num_lines - 1) <= page_size) {
				buf->dontscroll();
			}

			if (!xtext->add_io_tag)
			{
				/* remove scrolling events */
				if (xtext->io_tag)
				{
					g_source_remove(xtext->io_tag);
					xtext->io_tag = 0;
				}
				xtext->add_io_tag = g_timeout_add(REFRESH_TIMEOUT * 2,
					(GSourceFunc)
					gtk_xtext_render_page_timeout,
					xtext);
			}
		}
		if (buf->scrollbar_down)
		{
			buf->old_value = buf->num_lines - page_size;
			if (buf->old_value < 0.0)
				buf->old_value = 0.0;
		}
		if (buf->search_flags & follow)
		{
			auto gl = gtk_xtext_search_textentry(buf, *ent_ptr);
			gtk_xtext_search_textentry_add(buf, ent_ptr, gl, false);
		}
	}

} // end anonymous namespace

/* the main two public functions */

void gtk_xtext_append_indent(xtext_buffer *buf, ustring_ref left_text, ustring_ref right_text, time_t stamp)
{
	const auto xtext = buf->current_xtext();
	if (right_text.size() >= sizeof(xtext->scratch_buffer))
		right_text = right_text.substr(0, sizeof(xtext->scratch_buffer) - 1);

	if (right_text.back() == '\n')
		right_text.remove_suffix(1);

	textentry ent(xtext->backend->make_layout(right_text, ~0), xtext->backend->make_layout(left_text, ~0));
	ent.str.reserve(left_text.length() + right_text.length() + 2);
	ent.str.append(left_text.cbegin(), left_text.cend());
	ent.str.push_back(' ');
	ent.str.append(right_text.cbegin(), right_text.cend());

	const auto left_width = ent.indent_width();

	ent.left_len = left_text.length();
	ent.indent = (buf->indent() - left_width) - xtext->space_width;

	auto space = buf->is_time_stamped() ? xtext->stamp_width : 0;

	/* do we need to auto adjust the separator position? */
	if (xtext->auto_indent && ent.indent < MARGIN + space)
	{
		const auto tempindent =
		    MARGIN + space + xtext->space_width + left_width;

		if (tempindent > buf->indent())
			buf->set_indent(tempindent);

		/*if (buf->indent() > xtext->max_auto_indent)
			buf->set_indent(xtext->max_auto_indent);*/

		gtk_xtext_recalc_widths(buf, false);

		ent.indent =
		    (buf->indent() - left_width) - xtext->space_width;
		xtext->force_render = true;
	}

	gtk_xtext_append_entry(buf, std::move(ent), stamp);
}

void gtk_xtext_append(xtext_buffer *buf, boost::string_ref text, time_t stamp)
{
	if (text.back() == '\n')
		text.remove_suffix(text.size() - 1);

	if (text.size() >= sizeof(GtkXText::scratch_buffer))
		text.remove_suffix(sizeof(GtkXText::scratch_buffer) - 1);

	textentry ent(buf->current_xtext()->backend->make_layout(xtext::ustring_ref(reinterpret_cast<const unsigned char*>(text.data()), text.size()), 1000));
	if (!text.empty())
	{
		ent.str = xtext::ustring{text.cbegin(), text.cend()};
	}
	ent.indent = 0;
	ent.left_len = -1;

	gtk_xtext_append_entry(buf, std::move(ent), stamp);
}

bool gtk_xtext_is_empty(const xtext_buffer &buf)
{
	return buf.text_first == nullptr;
}

int gtk_xtext_lastlog(xtext_buffer *out, xtext_buffer *search_area)
{
	int matches = 0;
	const auto xtext = search_area->current_xtext();
	for (const auto &ent : search_area->entries)
	{
		auto gl = gtk_xtext_search_textentry(out, ent);
		if (!gl)
		{
			continue;
		}
		++matches;
		/* copy the text over */
		if (xtext->auto_indent)
		{
			gtk_xtext_append_indent(
			    out, ustring_ref(ent.str.c_str(), ent.left_len),
			    ustring_ref(ent.str.c_str() + ent.left_len + 1,
			    ent.str.size() - ent.left_len - 1), 0);
		}
		else
		{
			gtk_xtext_append(
			    out,
			    boost::string_ref{
				reinterpret_cast<const char *>(ent.str.c_str()),
				ent.str.size()},
			    0);
		}

		out->text_last->stamp = ent.stamp;
		gtk_xtext_search_textentry_add(out, out->text_last, gl,
					       true);
	}
	out->search_found = g_list_reverse(out->search_found);

	return matches;
}

void
gtk_xtext_set_indent(GtkXText *xtext, gboolean indent)
{
	xtext->auto_indent = !!indent;
}

void
gtk_xtext_set_max_indent(GtkXText *xtext, int max_auto_indent)
{
	xtext->max_auto_indent = max_auto_indent;
}

void
gtk_xtext_set_max_lines(GtkXText *xtext, int max_lines)
{
	xtext->max_lines = max_lines;
}

void
gtk_xtext_set_show_marker(GtkXText *xtext, gboolean show_marker)
{
	xtext->marker = !!show_marker;
}

void
gtk_xtext_set_show_separator(GtkXText *xtext, gboolean show_separator)
{
	xtext->separator = !!show_separator;
}

void
gtk_xtext_set_thin_separator(GtkXText *xtext, gboolean thin_separator)
{
	xtext->thinline = !!thin_separator;
}

void
gtk_xtext_set_urlcheck_function(GtkXText *xtext, int(*urlcheck_function) (GtkWidget *, const char *))
{
	xtext->urlcheck_function = urlcheck_function;
}

void
gtk_xtext_set_wordwrap(GtkXText *xtext, gboolean wordwrap)
{
	xtext->wordwrap = !!wordwrap;
}

void
gtk_xtext_set_marker_last(session *sess)
{
	xtext_buffer *buf = static_cast<xtext_buffer *>(sess->res->buffer);

	buf->marker_pos = buf->text_last;
	buf->marker_state = MARKER_IS_SET;
}

void
gtk_xtext_reset_marker_pos(GtkXText *xtext)
{
	if (xtext->buffer->marker_pos)
	{
		xtext->buffer->marker_pos = nullptr;
		xtext->buffer->dontscroll(); /* force scrolling off */
		gtk_widget_queue_draw(GTK_WIDGET(xtext));
		xtext->buffer->marker_state = MARKER_RESET_MANUALLY;
	}
}

int
gtk_xtext_moveto_marker_pos(GtkXText *xtext)
{
	xtext_buffer *buf = xtext->buffer;
	if (buf->marker_pos == nullptr)
		return buf->marker_state;

	if (gtk_xtext_check_ent_visibility(xtext, buf->marker_pos, 1) == false)
	{
		GtkAdjustment *adj = xtext->adj;
		std::size_t ivalue = 0;
		for(const auto & ent : buf->entries)
		{
			if (&ent == buf->marker_pos)
				break;
			ivalue += ent.sublines.size();
		}

		gdouble value = static_cast<gdouble>(ivalue);
		const auto adj_value = gtk_adjustment_get_value(adj);
		const auto adj_page_size = gtk_adjustment_get_page_size(adj);
		if (value >= adj_value && value < adj_value + adj_page_size)
			return MARKER_IS_SET;
		value -= adj_page_size / 2.0;
		if (value < 0.0)
			value = 0.0;
		const auto diff_to_upper = gtk_adjustment_get_upper(adj) - adj_page_size;
		if (value > diff_to_upper)
			value = diff_to_upper;

		gtk_adjustment_set_value(adj, value);
		gtk_widget_queue_draw(GTK_WIDGET(xtext));
	}

	/* If we previously lost marker position to scrollback limit -- */
	if (buf->marker_pos == buf->text_first &&
		buf->marker_state == MARKER_RESET_BY_KILL)
		return MARKER_RESET_BY_KILL;
	else
		return MARKER_IS_SET;
}

void
gtk_xtext_buffer_show(GtkXText *xtext, xtext_buffer *buf, bool render)
{
	gtk_xtext_buffer_set_xtext(buf, xtext);
	if (xtext->buffer == buf)
		return;

	/*printf("text_buffer_show: xtext=%p buffer=%p\n", xtext, buf);*/

	if (xtext->add_io_tag)
	{
		g_source_remove(xtext->add_io_tag);
		xtext->add_io_tag = 0;
	}

	if (xtext->io_tag)
	{
		g_source_remove(xtext->io_tag);
		xtext->io_tag = 0;
	}

	auto widget = GTK_WIDGET(xtext);
	if (!gtk_widget_get_realized(widget))
		gtk_widget_realize(widget);

	auto window = gtk_widget_get_window(widget);
	int h = gdk_window_get_height(window);
	int w = gdk_window_get_width(window);

	/* after a font change */
	if (buf->needs_recalc)
	{
		buf->needs_recalc = false;
		gtk_xtext_recalc_widths(buf, true);
	}

	/* now change to the new buffer */
	xtext->buffer = buf;
	buf->dontscroll();	/* force scrolling off */
	auto value = buf->old_value;
	auto upper = static_cast<gdouble>(buf->num_lines);
	const auto page_size = gtk_adjustment_get_page_size(xtext->adj);
	if (upper == 0.0)
		upper = 1.0;
	/* sanity check */
	else if (value > upper - page_size)
	{
		/*buf->pagetop_ent = nullptr;*/
		value = upper - page_size;
		if (value < 0.0)
			value = 0.0;
	}

	gtk_adjustment_set_upper(xtext->adj, upper);
	gtk_adjustment_set_value(xtext->adj, value);

	if (render)
	{
		/* did the window change size since this buffer was last shown? */
		if (buf->width() != w)
		{
			buf->set_width(w);
			gtk_xtext_calc_lines(buf, false);
			if (buf->scrollbar_down)
				gtk_adjustment_set_value(xtext->adj, gtk_adjustment_get_upper(xtext->adj) -
				gtk_adjustment_get_page_size(xtext->adj));
		}
		else if (buf->window_height != h)
		{
			buf->window_height = h;
			buf->pagetop_ent = nullptr;
			if (buf->scrollbar_down)
				gtk_adjustment_set_value(xtext->adj, gtk_adjustment_get_upper(xtext->adj));
			gtk_xtext_adjustment_set(buf, false);
		}

		gtk_widget_queue_draw(widget);
		gtk_adjustment_changed(xtext->adj);
	}
}

xtext_buffer *
gtk_xtext_buffer_new(GtkXText *xtext)
{
	xtext_buffer *buf = new xtext_buffer(xtext, xtext->space_width);
	buf->dontscroll();

	return buf;
}

void xtext_buffer::set_time_stamping(time_stamping new_stamping)
{
	if (this->time_stamp == static_cast<bool>(new_stamping)) {
		return;
	}
	this->time_stamp = new_stamping;
	this->needs_recalc = true;
	this->invalidate();
}

GError* xtext_buffer::set_search_regex(gtk_xtext_search_flags flags, const boost::string_ref & str)
{
	const auto search_string = str.to_string();
	const auto gcf = (flags & case_match) ? GRegexCompileFlags() : G_REGEX_CASELESS;
	GError* err = nullptr;
	this->search_re = g_regex_new(search_string.c_str(), gcf, GRegexMatchFlags(), &err);
	return err;
}

xtext_buffer::~xtext_buffer() noexcept
{
	if (this->search_found)
	{
		gtk_xtext_search_fini(this);
	}
}

void
gtk_xtext_buffer_free(xtext_buffer *buf)
{
	std::unique_ptr<xtext_buffer> buf_ptr(buf);
	const auto xtext = buf->current_xtext();
	if (xtext->buffer == buf)
		xtext->buffer = xtext->orig_buffer;

	if (xtext->selection_buffer == buf)
		xtext->selection_buffer = nullptr;
}

xtext_buffer* xtext_get_current_buffer(GtkXText* self) {
	return self->buffer;
}

GtkAdjustment* xtext_get_adjustments(GtkXText*self) {
	return self->adj;
}


static void
gtk_xtext_init(GtkXText * xtext)
{
	xtext->col_back = XTEXT_BG;
	xtext->col_fore = XTEXT_FG;
	xtext->clip_x2 = 1000000;
	xtext->clip_y2 = 1000000;
	gtk_xtext_scroll_adjustments(xtext, nullptr, nullptr);

	auto backend = xtext::create_backend(prefs.hex_text_font_main, GTK_WIDGET(xtext));
	if (backend) {
		xtext->backend = backend.release();
	}

	static const GtkTargetEntry targets[] = {
		{ "UTF8_STRING", 0, TARGET_UTF8_STRING },
		{ "STRING", 0, TARGET_STRING },
		{ "TEXT", 0, TARGET_TEXT },
		{ "COMPOUND_TEXT", 0, TARGET_COMPOUND_TEXT }
	};

	gtk_selection_add_targets(GTK_WIDGET(xtext), GDK_SELECTION_PRIMARY,
		targets, std::extent<decltype(targets)>::value);
}

static void gtk_xtext_class_init(GtkXTextClass * klass)
{
	const auto object_class = G_OBJECT_CLASS(klass);
	const auto widget_class = GTK_WIDGET_CLASS(klass);

	xtext_signals[WORD_CLICK] =
		g_signal_new("word_click",
			G_TYPE_FROM_CLASS(object_class),
			G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION | G_SIGNAL_NO_HOOKS,
			0,
			nullptr, nullptr,
			_hexchat_marshal_VOID__POINTER_POINTER,
			G_TYPE_NONE,
			2, G_TYPE_POINTER, G_TYPE_POINTER);
	xtext_signals[SET_SCROLL_ADJUSTMENTS] =
		g_signal_new("set_scroll_adjustments",
			G_OBJECT_CLASS_TYPE(object_class),
			G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION | G_SIGNAL_NO_HOOKS,
			0,
			nullptr, nullptr,
			_hexchat_marshal_VOID__OBJECT_OBJECT,
			G_TYPE_NONE,
			2, GTK_TYPE_ADJUSTMENT, GTK_TYPE_ADJUSTMENT);

	object_class->dispose = gtk_xtext_dispose;

	widget_class->realize = gtk_xtext_realize;
	widget_class->unrealize = gtk_xtext_unrealize;
	widget_class->size_request = gtk_xtext_size_request;
	widget_class->size_allocate = gtk_xtext_size_allocate;
	widget_class->button_press_event = gtk_xtext_button_press;
	widget_class->button_release_event = gtk_xtext_button_release;
	widget_class->motion_notify_event = gtk_xtext_motion_notify;
	widget_class->selection_clear_event = (gboolean(*)(GtkWidget*, GdkEventSelection*))gtk_xtext_selection_kill;
	widget_class->selection_get = gtk_xtext_selection_get;
#if GTK_CHECK_VERSION(3, 0, 0)
	widget_class->draw = gtk_xtext_draw;
#else
	widget_class->expose_event = gtk_xtext_expose;
#endif
	widget_class->scroll_event = gtk_xtext_scroll;
	widget_class->leave_notify_event = gtk_xtext_leave_notify;
	widget_class->set_scroll_adjustments_signal = xtext_signals[SET_SCROLL_ADJUSTMENTS];
}

GtkWidget *gtk_xtext_new(const gsl::span<GdkColor, XTEXT_COLS> palette, bool separator)
{
	GtkXText *xtext = static_cast<GtkXText *>(
		g_object_new(gtk_xtext_get_type(), nullptr));
	xtext->separator = separator;
	xtext->wordwrap = true;
	xtext->buffer = gtk_xtext_buffer_new(xtext);
	xtext->orig_buffer = xtext->buffer;
#if !GTK_CHECK_VERSION(3, 0, 0)
	xtext->style = gtk_style_context_new();
#endif
	g_signal_connect(xtext, "set_scroll_adjustments", (GCallback)gtk_xtext_scroll_adjustments, nullptr);
	auto widget = GTK_WIDGET(xtext);
	/*gtk_widget_set_double_buffered(widget, false);*/
	gtk_xtext_set_palette(xtext, palette);
	xtext->backend->set_palette(palette);
	return widget;
}

void
gtk_xtext_copy_selection(GtkXText *xtext)
{
	gtk_xtext_set_clip_owner(GTK_WIDGET(xtext), nullptr);
}

void gtk_xtext_set_ignore_hidden(GtkXText * xtext, bool ignore_hidden)
{
	xtext->ignore_hidden = ignore_hidden;
}

void gtk_xtext_buffer_set_xtext(xtext_buffer * buf, GtkXText * xtext)
{
	buf->set_xtext(xtext);
}

GError* gtk_xtext_buffer_set_search_regex(xtext_buffer * buf, gtk_xtext_search_flags flags, const boost::string_ref &regex)
{
	return buf->set_search_regex(flags, regex);
}

void gtk_xtext_buffer_set_stamping(xtext_buffer* buffer, time_stamping stamping) {
	buffer->set_time_stamping(stamping);
}

void gtk_xtext_buffer_set_search_paramters(xtext_buffer* buffer, const std::string_view search_string, const std::string_view needle, gtk_xtext_search_flags flags)
{
	buffer->search_flags = flags;
	buffer->search_text = search_string;
	buffer->search_nee = needle;
}

void
gtk_xtext_set_palette(GtkXText * xtext, const gsl::span<GdkColor, XTEXT_COLS> palette)
{
	std::copy_n(palette.cbegin(), XTEXT_COLS, std::begin(xtext->palette));
	xtext->backend->set_palette(palette);
	if (gtk_widget_get_realized(GTK_WIDGET(xtext)))
	{
		xtext_set_fg(xtext, XTEXT_FG);
		xtext_set_bg(xtext, XTEXT_BG);
		xtext_set_fg(xtext, XTEXT_BG);
	}
	xtext->col_fore = XTEXT_FG;
	xtext->col_back = XTEXT_BG;
}

bool gtk_xtext_set_font(GtkXText *xtext, const char name[])
{
	if (xtext->font)
		backend_font_close(xtext);

	/* realize now, so that font_open has a XDisplay */
	gtk_widget_realize(GTK_WIDGET(xtext));

	backend_font_open(xtext, name);
	if (xtext->font == nullptr)
		return false;
	xtext->backend->set_default_font(name);
	xtext->fontsize = xtext->font->ascent + xtext->font->descent;

	{
		auto time_str = xtext_get_stamp_str(std::time(nullptr));
		xtext->stamp_width =
			gtk_xtext_text_width(xtext, ustring_ref(reinterpret_cast<const unsigned char*>(time_str.c_str()), time_str.size())) + MARGIN;
	}

	xtext->buffer->invalidate();

	if (gtk_widget_get_realized(GTK_WIDGET(xtext)))
		gtk_xtext_recalc_widths(xtext->buffer, true);

	return true;
}

namespace xtext {
	void save(const GtkXText & xtext, std::ostream & outfile)
	{
		for (const auto & ent : xtext.buffer->entries)
		{
			if (!outfile)
			{
				break;
			}
			auto buf = xtext::strip_color(ent.str, nullptr, false);
			boost::string_ref ref(reinterpret_cast<const char*>(buf.data()), buf.length());
			outfile << ref << '\n';
		}
	}
}

void
gtk_xtext_refresh(GtkXText * xtext)
{
	if (gtk_widget_get_realized(GTK_WIDGET(xtext)))
	{
		gtk_widget_queue_draw(GTK_WIDGET(xtext));
	}
}
/* If lines=0 => clear all */

void
gtk_xtext_clear(xtext_buffer *buf, int lines)
{
	bool marker_reset = false;
	const auto xtext = buf->current_xtext();
	if (lines != 0)
	{
		if (lines < 0)
		{
			/* delete lines from bottom */
			lines *= -1;
			while (lines)
			{
				if (buf->text_last == buf->marker_pos)
					marker_reset = true;
				gtk_xtext_remove_bottom(buf);
				lines--;
			}
		}
		else
		{
			/* delete lines from top */
			while (lines)
			{
				if (buf->text_first == buf->marker_pos)
					marker_reset = true;
				gtk_xtext_remove_top(buf);
				lines--;
			}
		}
	}
	else
	{
		/* delete all */
		if (buf->search_found)
			gtk_xtext_search_fini(buf);
		if (xtext->auto_indent)
			buf->set_indent(MARGIN);
		buf->scrollbar_down = true;
		buf->last_ent_start = nullptr;
		buf->last_ent_end = nullptr;
		buf->marker_pos = nullptr;
		if (buf->text_first)
			marker_reset = true;
		buf->dontscroll();
		buf->entries.clear();
		buf->text_first = nullptr;
		/*while (buf->text_first)
		{
		auto next = buf->text_first->next;
		delete buf->text_first;
		buf->text_first = next;
		}*/
		buf->text_last = nullptr;
	}

	if (xtext->buffer == buf)
	{
		gtk_xtext_calc_lines(buf, true);
		gtk_xtext_refresh(xtext);
	}
	else
	{
		gtk_xtext_calc_lines(buf, false);
	}

	if (marker_reset)
		buf->marker_state = MARKER_RESET_BY_CLEAR;
}

void
gtk_xtext_check_marker_visibility(GtkXText * xtext)
{
	if (gtk_xtext_check_ent_visibility(xtext, xtext->buffer->marker_pos, 1))
		xtext->buffer->marker_seen = true;
}


#define BACKWARD (flags & backward)
#define FIRSTLAST(lp)  (BACKWARD? g_list_last(lp): g_list_first(lp))
#define NEXTPREVIOUS(lp) (BACKWARD? g_list_previous(lp): g_list_next(lp))
bool
gtk_xtext_search(GtkXText * xtext, const gchar *text, gtk_xtext_search_flags flags, GError **perr)
{
	xtext_buffer *buf = xtext->buffer;

	if (buf->text_first == nullptr)
	{
		return nullptr;
	}
	textentry *ent = nullptr;
	/* If the text arg is nullptr, one of these has been toggled: highlight follow */
	if (!text)		/* Here on highlight or follow toggle */
	{
		gint oldfollow = buf->search_flags & follow;
		gint newfollow = flags & follow;

		/* If "Follow" has just been checked, search possible new textentries --- */
		if (newfollow && (newfollow != oldfollow))
		{
			auto gl = g_list_last(buf->search_found);
			ent = static_cast<textentry *>(gl ? gl->data : buf->text_first);
			for (; ent; ent = ent->next)
			{
				auto gl = gtk_xtext_search_textentry(buf, *ent);
				gtk_xtext_search_textentry_add(buf, ent, gl, false);
			}
		}
		buf->search_flags = flags;
		ent = buf->pagetop_ent;
	}

	/* if the text arg is "", the reset button has been clicked or Control-Shift-F has been hit */
	else if (text[0] == 0)		/* Let a null string do a reset. */
	{
		gtk_xtext_search_fini(buf);
	}

	/* If the text arg is neither nullptr nor "", it's the search string */
	else
	{
		if (gtk_xtext_search_init(buf, text, flags, perr) == false)	/* If a new search: */
		{
			if (perr && *perr)
			{
				return nullptr;
			}
			for (ent = buf->text_first; ent; ent = ent->next)
			{
				auto gl = gtk_xtext_search_textentry(buf, *ent);
				gtk_xtext_search_textentry_add(buf, ent, gl, true);
			}
			buf->search_found = g_list_reverse(buf->search_found);
		}

		/* Now base search results are in place. */

		if (buf->search_found)
		{
			/* If we're in the midst of moving among found items */
			if (buf->cursearch)
			{
				ent = static_cast<textentry *>(buf->cursearch->data);
				buf->curmark = NEXTPREVIOUS(buf->curmark);
				if (buf->curmark == nullptr)
				{
					/* We've returned all the matches for this textentry. */
					buf->cursearch = NEXTPREVIOUS(buf->cursearch);
					if (buf->cursearch)
					{
						ent = static_cast<textentry *>(buf->cursearch->data);
						buf->curmark = FIRSTLAST(ent->marks);
					}
					else	/* We've returned all the matches for all textentries */
					{
						ent = nullptr;
					}
				}
			}

			/* If user changed the search, let's look starting where he was */
			else if (buf->hintsearch)
			{
				GList *mark;
				offsets_t last, this_line;
				/*
				* If we already have a 'current' item from the last search, and if
				* the first character of an occurrence on this line for this new search
				* is within that former item, use the occurrence as current.
				*/
				ent = buf->hintsearch;
				last.u = buf->curdata.u;
				for (mark = ent->marks; mark; mark = mark->next)
				{
					this_line.u = GPOINTER_TO_UINT(mark->data);
					if (this_line.o.start >= last.o.start && this_line.o.start < last.o.end)
						break;
				}
				if (!mark)
				{
					for (ent = buf->hintsearch; ent; ent = BACKWARD ? ent->prev : ent->next)
						if (ent->marks)
							break;
					mark = ent ? FIRSTLAST(ent->marks) : nullptr;
				}
				buf->cursearch = g_list_find(buf->search_found, ent);
				buf->curmark = mark;
			}

			/* This is a fresh search */
			else
			{
				buf->cursearch = FIRSTLAST(buf->search_found);
				ent = static_cast<textentry *>(buf->cursearch->data);
				buf->curmark = FIRSTLAST(ent->marks);
			}
			buf->curdata.u = (buf->curmark) ? GPOINTER_TO_UINT(buf->curmark->data) : 0;
		}
	}
	buf->hintsearch = ent;

	if (!gtk_xtext_check_ent_visibility(xtext, ent, 1))
	{
		GtkAdjustment *adj = xtext->adj;
		gdouble value;

		buf->pagetop_ent = nullptr;
		for (value = 0.0, ent = buf->text_first;
			ent && ent != buf->hintsearch; ent = ent->next)
		{
			value += ent->sublines.size();
		}
		const auto page_size = gtk_adjustment_get_page_size(adj);
		const auto diff_to_page_top = gtk_adjustment_get_upper(adj) - page_size;
		if (value > diff_to_page_top)
		{
			value = diff_to_page_top;
		}
		else if ((flags & backward) && ent)
		{
			value -= page_size - ent->sublines.size();
			if (value < 0.0)
			{
				value = 0.0;
			}
		}
		gtk_adjustment_set_value(adj, value);
	}

	gtk_widget_queue_draw(GTK_WIDGET(xtext));

	return buf->hintsearch != nullptr;
}
#undef BACKWARD
#undef FIRSTLAST
#undef NEXTPREVIOUS

