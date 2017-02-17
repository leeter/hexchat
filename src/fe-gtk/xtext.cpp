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
#include "fe-gtk.hpp"
#include "xtext.hpp"
#include "fkeys.hpp"
#include "gtk3bridge.hpp"

#define charlen(str) g_utf8_skip[*(guchar *)(str)]

#ifdef WIN32
//#include <io.h>
#else
#include <unistd.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#endif

namespace {
	struct GtkXTextPrivate {

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

		int depth;						  /* gdk window depth */

		char num[8];					  /* for parsing mirc color */
		int nc;							  /* offset into priv->num */

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
}

G_DEFINE_TYPE_WITH_PRIVATE(GtkXText, gtk_xtext, GTK_TYPE_WIDGET)

char *nocasestrstr(const char *text, const char *tofind);	/* util.c */
std::string xtext_get_stamp_str(time_t);

using ustring = std::basic_string<unsigned char>;
using ustring_ref = boost::basic_string_ref<unsigned char>;

/* For use by gtk_xtext_strip_color() and its callers -- */
struct offlen_t {
	guint16 off;
	guint16 len;
	guint16 emph;
	guint16 width;
};

struct textentry
{
	textentry()
		:tag(),
		str_width(),
		mark_start(),
		mark_end(),
		indent(),
		left_len(),
		stamp(),
		next(),
		prev(),
		marks(){}

	guchar tag;
	gint16 str_width;
	gint16 mark_start;
	gint16 mark_end;
	gint16 indent;
	gint16 left_len;
	std::time_t stamp;
	textentry *next;
	textentry *prev;
	GList *marks;	/* List of found strings */
	std::vector<offlen_t> slp;
	std::vector<int> sublines;
	ustring str;
};

struct xtext_impl
{
	marker_reset_reason marker_state;
	textentry *text_first;
	textentry *text_last;

	textentry *last_ent_start;	  /* this basically describes the last rendered */
	textentry *last_ent_end;	  /* selection. */
	textentry *pagetop_ent;			/* what's at priv->adj->value */

	textentry *marker_pos;
	
	std::deque<textentry> entries;

	xtext_impl()
		:marker_state(),
		text_first(), text_last(),

		last_ent_start(), /* this basically describes the last rendered */
		last_ent_end(),   /* selection. */
		pagetop_ent(), /* what's at priv->adj->value */
		marker_pos()
	{}
};

namespace
{
	struct scope_exit {
		scope_exit(std::function<void(void)> f) : f_(f) {}
		~scope_exit(void) { f_(); }
	private:
		std::function<void(void)> f_;
	};


	template<class T>
	inline bool is_del(T c)
	{
		return (c == ' ' || c == '\n' || c == '>' || c == '<' || c == 0);
	}


	/* force scrolling off */
	void dontscroll(xtext_buffer* buf)
	{
		(buf)->last_pixel_pos = 0x7fffffff;
	}

	static GtkWidgetClass *parent_class = nullptr;

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
	static void gtk_xtext_fix_indent(xtext_buffer *buf);
	static int gtk_xtext_find_subline(const textentry &ent, int line);
	/* static char *gtk_xtext_conv_color (unsigned char *text, int len, int *newlen); */
	static unsigned char *
		gtk_xtext_strip_color(const ustring_ref & text, unsigned char *outbuf,
		int *newlen, std::vector<offlen_t> * slp, int strip_hidden);
	static ustring gtk_xtext_strip_color(const ustring_ref &text,
		std::vector<offlen_t> *slp,
		int strip_hidden);
	static bool gtk_xtext_check_ent_visibility(GtkXText * xtext, textentry *find_ent, int add);
	static int gtk_xtext_render_page_timeout(GtkXText * xtext);
	static int gtk_xtext_search_offset(xtext_buffer *buf, textentry *ent, unsigned int off);
	static GList * gtk_xtext_search_textentry(xtext_buffer *, const textentry &);
	static void gtk_xtext_search_textentry_add(xtext_buffer *, textentry *, GList *, bool);
	static void gtk_xtext_search_textentry_del(xtext_buffer *, textentry *);
	static void gtk_xtext_search_textentry_fini(gpointer, gpointer);
	static void gtk_xtext_search_fini(xtext_buffer *);
	static bool gtk_xtext_search_init(xtext_buffer *buf, const gchar *text, gtk_xtext_search_flags flags, GError **perr);
	static const char * gtk_xtext_get_word(GtkXText * xtext, int x, int y, textentry ** ret_ent, int *ret_off, int *ret_len, std::vector<offlen_t> *slp);

	/* Avoid warning messages for this unused function */
#if 0
	/* gives width of a 8bit string - with no mIRC codes in it */

	static int
		gtk_xtext_text_width_8bit(GtkXText *xtext, unsigned char *str, int len)
	{
		int width = 0;

		while (len)
		{
			width += priv->fontwidth[*str];
			str++;
			len--;
		}

		return width;
	}
#endif

//#define xtext_draw_bg(xt,x,y,w,h) gdk_draw_rectangle(xt->draw_buf, xt->bgc, 1, x, y, w, h);

	/* ======================================= */
	/* ============ PANGO BACKEND ============ */
	/* ======================================= */

	enum emph{
		EMPH_ITAL = 1,
		EMPH_BOLD = 2,
		EMPH_HIDDEN = 4
	};

	static PangoAttrList *attr_lists[4];
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
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		for (std::size_t i = 0; i < std::extent<decltype(attr_lists)>::value; i++)
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
			pango_layout_set_attributes(priv->layout, attr_lists[i]);
			for (int j = 0; j < 128; j++)
			{
				buf[0] = j;
				pango_layout_set_text(priv->layout, buf, 1);
				pango_layout_get_pixel_size(priv->layout, &fontwidths[i][j], nullptr);
			}
		}
		priv->space_width = fontwidths[0][' '];
	}

	static void
		backend_font_close(GtkXText *xtext)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		pango_font_description_free(priv->font->font);
	}

	static void
		backend_init(GtkXText *xtext)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		if (priv->layout == nullptr)
		{
			priv->layout = gtk_widget_create_pango_layout(GTK_WIDGET(xtext), 0);
			if (priv->font)
				pango_layout_set_font_description(priv->layout, priv->font->font);
		}
	}

	static void
		backend_deinit(GtkXText *xtext)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		if (priv->layout)
		{
			g_object_unref(priv->layout);
			priv->layout = nullptr;
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
	CUSTOM_PTR(PangoFontMetrics, pango_font_metrics_unref)

		static void backend_font_open(GtkXText *xtext, const char *name)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		priv->font = &priv->pango_font;
		priv->font->font = backend_font_open_real(name);
		if (!priv->font->font)
		{
			priv->font = nullptr;
			return;
		}

		backend_init(xtext);
		pango_layout_set_font_description(priv->layout, priv->font->font);
		xtext_pango_init(xtext);

		/* vte does it this way */
		auto context = gtk_widget_get_pango_context(GTK_WIDGET(xtext));
		auto lang = pango_context_get_language(context);
		PangoFontMetricsPtr metrics{ pango_context_get_metrics(context, priv->font->font, lang) };
		priv->font->ascent = pango_font_metrics_get_ascent(metrics.get()) / PANGO_SCALE;
		priv->font->descent = pango_font_metrics_get_descent(metrics.get()) / PANGO_SCALE;
	}

	static int backend_get_text_width_emph(GtkXText *xtext, const ustring_ref & str, int emphasis)
	{
		if (str.empty())
			return 0;

		if ((emphasis & EMPH_HIDDEN))
			return 0;
		emphasis &= (EMPH_ITAL | EMPH_BOLD);

		int width = 0;
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		pango_layout_set_attributes(priv->layout, attr_lists[emphasis]);
		for (auto itr = str.cbegin(), end = str.cend(); itr != end;)
		{
			int mbl = charlen(itr);
			int deltaw;
			if (*itr < 128)
				deltaw = fontwidths[emphasis][*itr];
			else
			{
				pango_layout_set_text(priv->layout, reinterpret_cast<const char*>(itr), mbl);
				pango_layout_get_pixel_size(priv->layout, &deltaw, nullptr);
			}
			width += deltaw;
			if (mbl < std::distance(itr, end))
				itr += mbl;
			else
				break;
		}

		return width;
	}

	static int backend_get_text_width_slp(GtkXText *xtext, const guchar *str, const std::vector<offlen_t> & slp)
	{
		int width = 0;

		for (const auto & meta : slp)
		{
			width += backend_get_text_width_emph(xtext, ustring_ref(str, meta.len), meta.emph);
			str += meta.len;
		}

		return width;
	}

	static void backend_draw_text_emph(GtkXText *xtext, bool dofill, cairo_t *cr, int x, int y,
		const ustring_ref& str, int str_width, int emphasis)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		pango_layout_set_attributes(priv->layout, attr_lists[emphasis]);
		pango_layout_set_text(priv->layout, reinterpret_cast<const char*>(str.data()), str.length());
		cairo_stack cr_stack{ cr };
		if (dofill)
		{
			cairo_rectangle(cr, x, y, str_width, priv->fontsize);
			gdk_cairo_set_source_color(cr, bridge_get_background(priv->style));
			cairo_fill(cr);
		}
		
		gdk_cairo_set_source_color(cr, bridge_get_foreground(priv->style));
		cairo_move_to(cr, x, y);
		pango_cairo_show_layout(cr, priv->layout);
	}

	static void
		xtext_set_fg(GtkXText *xtext, int index)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		bridge_set_foreground(priv->style, &priv->palette[index]);
	}

	static void
		xtext_set_bg(GtkXText *xtext, int index)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		bridge_set_background(priv->style, &priv->palette[index]);
	}

	static void gtk_xtext_adjustment_set(xtext_buffer *buf,
					     bool fire_signal)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(buf->xtext));
		GtkAdjustment *adj = priv->adj;

		if (priv->buffer != buf)
		{
			return;
		}

		auto lower = 0.0;
		auto upper = static_cast<gdouble>(buf->num_lines);

		if (upper == 0.0)
			upper = 1.0;

		GtkAllocation allocation;
		gtk_widget_get_allocation(GTK_WIDGET(buf->xtext), &allocation);
		auto page_size = static_cast<gdouble>(
		    allocation.height /
		    priv->fontsize);
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

		g_signal_handler_block(adj, priv->vc_signal_tag);
		gtk_adjustment_configure(adj, adj_value, lower, upper, gtk_adjustment_get_step_increment(adj), page_increment, page_size);
		g_signal_handler_unblock(adj, priv->vc_signal_tag);
		if (fire_signal)
			gtk_adjustment_changed(adj);
	}

	static gint
		gtk_xtext_adjustment_timeout(GtkXText * xtext)
	{
		gtk_widget_queue_draw(GTK_WIDGET(xtext));
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		priv->io_tag = 0;
		return 0;
	}

	static void
		gtk_xtext_adjustment_changed(GtkAdjustment * adj, GtkXText * xtext)
	{
		if (!gtk_widget_get_realized(GTK_WIDGET(xtext)))
			return;
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		const auto adj_value = gtk_adjustment_get_value(priv->adj);
		if (priv->buffer->old_value != adj_value)
		{
			const auto page_diff = gtk_adjustment_get_upper(priv->adj) - gtk_adjustment_get_page_size(priv->adj);
			if (adj_value >= page_diff)
				priv->buffer->scrollbar_down = true;
			else
				priv->buffer->scrollbar_down = false;

			if (adj_value + 1.0 == priv->buffer->old_value ||
				adj_value - 1.0 == priv->buffer->old_value)	/* clicked an arrow? */
			{
				if (priv->io_tag)
				{
					g_source_remove(priv->io_tag);
					priv->io_tag = 0;
				}
				gtk_widget_queue_draw(GTK_WIDGET(xtext));
			}
			else
			{
				if (!priv->io_tag)
					priv->io_tag = g_timeout_add(REFRESH_TIMEOUT,
					(GSourceFunc)
					gtk_xtext_adjustment_timeout,
					xtext);
			}
		}
		priv->buffer->old_value = adj_value;
	}
} // end anonymous namespace

static void
gtk_xtext_init(GtkXText * xtext)
{
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
	priv->pixmap = nullptr;
	priv->io_tag = 0;
	priv->add_io_tag = 0;
	priv->scroll_tag = 0;
	priv->max_lines = 0;
	priv->col_back = XTEXT_BG;
	priv->col_fore = XTEXT_FG;
	priv->nc = 0;
	priv->pixel_offset = 0;
	priv->underline = false;
	priv->hidden = false;
	priv->font = nullptr;
	priv->layout = nullptr;
	priv->jump_out_offset = 0;
	priv->jump_in_offset = 0;
	priv->ts_x = 0;
	priv->ts_y = 0;
	priv->clip_x = 0;
	priv->clip_x2 = 1000000;
	priv->clip_y = 0;
	priv->clip_y2 = 1000000;
	priv->urlcheck_function = nullptr;
	priv->color_paste = false;
	priv->skip_border_fills = false;
	priv->skip_stamp = false;
	priv->render_hilights_only = false;
	priv->un_hilight = false;
	priv->dont_render = false;
	priv->dont_render2 = false;
	gtk_xtext_scroll_adjustments(xtext, nullptr, nullptr);

	static const GtkTargetEntry targets[] = {
		{ "UTF8_STRING", 0, TARGET_UTF8_STRING },
		{ "STRING", 0, TARGET_STRING },
		{ "TEXT", 0, TARGET_TEXT },
		{ "COMPOUND_TEXT", 0, TARGET_COMPOUND_TEXT }
	};

	gtk_selection_add_targets(GTK_WIDGET(xtext), GDK_SELECTION_PRIMARY,
		targets, std::extent<decltype(targets)>::value);
}



GtkWidget *gtk_xtext_new(GdkColor palette[], bool separator)
{
	GtkXText *xtext = static_cast<GtkXText *>(
	    g_object_new(gtk_xtext_get_type(), nullptr));
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
	priv->separator = separator;
	priv->wordwrap = true;
	priv->buffer = gtk_xtext_buffer_new(xtext);
	priv->orig_buffer = priv->buffer;
#if !GTK_CHECK_VERSION(3, 0, 0)
	priv->style = gtk_style_context_new();
#endif

	auto widget = GTK_WIDGET(xtext);
	/*gtk_widget_set_double_buffered(widget, false);*/
	gtk_xtext_set_palette(xtext, palette);

	return widget;
}

namespace {
	static void
		gtk_xtext_destroy(GtkObject * object)
	{
		GtkXText *xtext = GTK_XTEXT(object);
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		if (priv->add_io_tag)
		{
			g_source_remove(priv->add_io_tag);
			priv->add_io_tag = 0;
		}

		if (priv->scroll_tag)
		{
			g_source_remove(priv->scroll_tag);
			priv->scroll_tag = 0;
		}

		if (priv->io_tag)
		{
			g_source_remove(priv->io_tag);
			priv->io_tag = 0;
		}

		if (priv->pixmap)
		{
			g_object_unref(priv->pixmap);
			priv->pixmap = nullptr;
		}

		if (priv->font)
		{
			backend_font_close(xtext);
			priv->font = nullptr;
		}

		if (priv->adj)
		{
			g_signal_handlers_disconnect_matched(G_OBJECT(priv->adj),
				G_SIGNAL_MATCH_DATA, 0, 0, nullptr, nullptr, xtext);
			/*	gtk_signal_disconnect_by_data (G_OBJECT (priv->adj), xtext);*/
			g_object_unref(G_OBJECT(priv->adj));
			priv->adj = nullptr;
		}

		if (priv->hand_cursor)
		{
			gdk_cursor_unref(priv->hand_cursor);
			priv->hand_cursor = nullptr;
		}

		if (priv->resize_cursor)
		{
			gdk_cursor_unref(priv->resize_cursor);
			priv->resize_cursor = nullptr;
		}

		if (priv->orig_buffer)
		{
			gtk_xtext_buffer_free(priv->orig_buffer);
			priv->orig_buffer = nullptr;
		}
#if !GTK_CHECK_VERSION(3, 0, 0)
		if (priv->style)
		{
			bridge_style_context_free(priv->style);
			priv->style = nullptr;
		}
#endif

		if (GTK_OBJECT_CLASS(parent_class)->destroy)
			(*GTK_OBJECT_CLASS(parent_class)->destroy) (object);
	}

	static void
		gtk_xtext_unrealize(GtkWidget * widget)
	{
		backend_deinit(GTK_XTEXT(widget));

		/* if there are still events in the queue, this'll avoid segfault */
		gdk_window_set_user_data(gtk_widget_get_window(widget), nullptr);

		if (parent_class->unrealize)
			(*GTK_WIDGET_CLASS(parent_class)->unrealize) (widget);
	}

	static void
		gtk_xtext_realize(GtkWidget * widget)
	{
		GdkWindowAttr attributes;
		GdkGCValues val;
		GdkColormap *cmap;

		gtk_widget_set_realized(widget, true);
		auto xtext = GTK_XTEXT(widget);

		GtkAllocation allocation;
		gtk_widget_get_allocation(widget, &allocation);
		attributes.x = allocation.x;
		attributes.y = allocation.y;
		attributes.width = allocation.width;
		attributes.height = allocation.height;
		attributes.wclass = GDK_INPUT_OUTPUT;
		attributes.window_type = GDK_WINDOW_CHILD;
		attributes.event_mask = gtk_widget_get_events(widget) |
			GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
			| GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK;

		cmap = gtk_widget_get_colormap(widget);
		attributes.colormap = cmap;
		attributes.visual = gtk_widget_get_visual(widget);

		auto window = gdk_window_new(gtk_widget_get_parent_window(widget), &attributes,
			GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL |
			GDK_WA_COLORMAP);
		gtk_widget_set_window(widget, window);

		gdk_window_set_user_data(window, widget);
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		priv->depth = gdk_visual_get_depth(gdk_window_get_visual(window));

		val.subwindow_mode = GDK_INCLUDE_INFERIORS;
		val.graphics_exposures = 0;

		xtext_set_fg(xtext, XTEXT_FG);
		xtext_set_bg(xtext, XTEXT_BG);
		xtext_set_fg(xtext, XTEXT_BG);

		/* draw directly to window */
		priv->draw_buf = window;

		priv->hand_cursor = gdk_cursor_new_for_display(gdk_window_get_display(window), GDK_HAND1);
		priv->resize_cursor = gdk_cursor_new_for_display(gdk_window_get_display(window), GDK_LEFT_SIDE);

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
		bool height_only = false;
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		if (allocation->width == priv->buffer->window_width)
			height_only = true;

		gtk_widget_set_allocation(widget, allocation);
		if (!gtk_widget_get_realized(GTK_WIDGET(widget)))
		{
			return;
		}

		priv->buffer->window_width = allocation->width;
		priv->buffer->window_height = allocation->height;

		gdk_window_move_resize(gtk_widget_get_window(widget), allocation->x,
				       allocation->y, allocation->width,
				       allocation->height);
		dontscroll(priv->buffer); /* force scrolling off */
		if (!height_only)
			gtk_xtext_calc_lines(priv->buffer, false);
		else
		{
			priv->buffer->impl->pagetop_ent = nullptr;
			gtk_xtext_adjustment_set(priv->buffer, false);
		}
		if (priv->buffer->scrollbar_down) {
			gtk_adjustment_set_value(priv->adj,
				gtk_adjustment_get_upper(priv->adj) -
				gtk_adjustment_get_page_size(priv->adj));
		}
	}

	static int
		gtk_xtext_selection_clear(xtext_buffer *buf)
	{
		textentry *ent;
		int ret = 0;

		ent = buf->impl->last_ent_start;
		while (ent)
		{
			if (ent->mark_start != -1)
				ret = 1;
			ent->mark_start = -1;
			ent->mark_end = -1;
			if (ent == buf->impl->last_ent_end)
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
		int off, len, wid, mbl, mbw;

		/* Skip to the first chunk of stuff for the subline */
		std::vector<offlen_t>::const_iterator meta, hid = ent.slp.cend();
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
		off = meta->off;
		len = meta->len;
		if (meta->emph & EMPH_HIDDEN)
			hid = meta;
		while (len > 0)
		{
			if (off >= suboff)
				break;
			mbl = charlen(ent.str.c_str() + off);
			len -= mbl;
			off += mbl;
		}
		if (len < 0)
			return ent.str.size();		/* Bad char -- return max offset. */

		/* Step through characters to find the one at the x position */
		wid = x - indent;
		len = meta->len - (off - meta->off);
		while (wid > 0)
		{
			mbl = charlen(ent.str.c_str() + off);
			mbw = backend_get_text_width_emph(xtext, ustring_ref(ent.str.c_str() + off, mbl), meta->emph);
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
		int line, gboolean *out_of_bounds)
	{
		int indent;
		const unsigned char *str;
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		if (subline < 1)
			indent = ent.indent;
		else
			indent = priv->buffer->indent;

		if (line > gtk_adjustment_get_page_size(priv->adj) || line < 0)
		{
			*out_of_bounds = true;
			return 0;
		}

		str = ent.str.c_str() + gtk_xtext_find_subline(ent, subline);
		if (str >= ent.str.c_str() + ent.str.size())
			return 0;

		/* Let user select left a few pixels to grab hidden text e.g. '<' */
		if (x < indent - priv->space_width)
		{
			*out_of_bounds = true;
			return (str - ent.str.c_str());
		}

		*out_of_bounds = false;

		return find_x(xtext, ent, x, subline, indent);
	}

	static textentry *
		gtk_xtext_find_char(GtkXText * xtext, int x, int y, int *off,
		gboolean *out_of_bounds, int *ret_subline)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		/* Adjust y value for negative rounding, double to int */
		if (y < 0)
			y -= priv->fontsize;

		int subline;
		int line = (y + priv->pixel_offset) / priv->fontsize;
		auto ent = gtk_xtext_nth(xtext, line + (int)gtk_adjustment_get_value(priv->adj), subline);
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
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		if (!priv->separator || !priv->buffer->indent)
			return;
		auto x = priv->buffer->indent - ((priv->space_width + 1) / 2);
		if (x < 1)
			return;

		cairo_stack cr_stack{cr};
		if (priv->moving_separator)
		{
			// light
			// col.red = 0xffff; col.green = 0xffff; col.blue =
			// 0xffff;
			cairo_set_source_rgb(cr, 1.0, 1.0, 1.0);
		}
		else if (priv->thinline)
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
		if (priv->thinline)
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
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		if (!priv->marker) return;

		double render_y;
		if (priv->buffer->impl->marker_pos == ent)
		{
			render_y = y + priv->font->descent;
		}
		else if (priv->buffer->impl->marker_pos == ent->next && ent->next != nullptr)
		{
			render_y = y + priv->font->descent + priv->fontsize * ent->sublines.size();
		}
		else return;

		int x = 0;
		GtkAllocation allocation;
		gtk_widget_get_allocation(GTK_WIDGET(xtext), &allocation);
		int width = -allocation.width;
		cairo_stack stack{ cr };
		gdk_cairo_set_source_color(cr, &priv->palette[XTEXT_MARKER]);
		render_y += 0.5;
		cairo_move_to(cr, x, render_y);
		cairo_line_to(cr, x + width, render_y);
		cairo_set_line_width(cr, 1);
		cairo_stroke(cr);
		//gdk_draw_line(priv->draw_buf, priv->marker_gc, x, render_y, x + width, render_y);

		if (gtk_window_has_toplevel_focus(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(xtext)))))
		{
			priv->buffer->marker_seen = true;
		}
	}

#if !GTK_CHECK_VERSION(3, 0, 0)
	auto gtk_widget_get_style_context(GtkWidget* w) {
		return static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(GTK_XTEXT(w)))->style;
	}
#endif
	static gboolean gtk_xtext_draw(GtkWidget *widget, cairo_t *cr)
	{
		g_return_val_if_fail(GTK_IS_XTEXT(widget), true);

		GtkXText *xtext = GTK_XTEXT(widget);
		GtkAllocation allocation;
		gtk_widget_get_allocation(widget, &allocation);
		gtk_render_background(gtk_widget_get_style_context(widget), cr, allocation.x,
				      allocation.y, allocation.width,
				      allocation.height);
		dontscroll(xtext_get_current_buffer(xtext)); /* force scrolling off */
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
		gtk_xtext_draw(widget, cr.get());

		return false;
	}

	static void
		gtk_xtext_selection_render(GtkXText *xtext, textentry *start_ent, textentry *end_ent)
	{
		auto buffer = xtext_get_current_buffer(xtext);
		buffer->impl->last_ent_start = start_ent;
		buffer->impl->last_ent_end = end_ent;
		buffer->last_offset_start = start_ent->mark_start;
		buffer->last_offset_end = end_ent->mark_end;
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		priv->skip_border_fills = false;
		priv->skip_stamp = false;
		gtk_widget_queue_draw(GTK_WIDGET(xtext));
	}

	static void gtk_xtext_selection_draw(GtkXText * xtext, GdkEventMotion *, bool render)
	{
		int offset_start;
		int offset_end;
		int subline_start;
		int subline_end;
		gboolean oob = false;
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		auto ent_start = gtk_xtext_find_char(xtext, priv->select_start_x, priv->select_start_y, &offset_start, &oob, &subline_start);
		auto ent_end = gtk_xtext_find_char(xtext, priv->select_end_x, priv->select_end_y, &offset_end, &oob, &subline_end);
	
		if ((!ent_start || !ent_end) && !priv->buffer->impl->text_last && gtk_adjustment_get_value(priv->adj) != priv->buffer->old_value)
		{
			gtk_widget_queue_draw(GTK_WIDGET(xtext));
			return;
		}

		if (!ent_start)
		{
			ent_start = priv->buffer->impl->text_last;
			offset_start = ent_start->str.size();
		}

		if (!ent_end)
		{
			ent_end = priv->buffer->impl->text_last;
			offset_end = ent_end->str.size();
		}

		bool marking_up = false;

		if ((ent_start != ent_end && priv->select_start_y > priv->select_end_y) || /* different entries */
			(ent_start == ent_end && subline_start > subline_end) || /* different lines */
			(ent_start == ent_end && subline_start == subline_end && priv->select_start_x > priv->select_end_x)) /* marking to the left */
		{
			marking_up = true;
		}

		/* word selection */
		if (priv->word_select)
		{
			int len_start;
			int len_end;
			/* a word selection cannot be started if the cursor is out of bounds in gtk_xtext_button_press */
			gtk_xtext_get_word(xtext, priv->select_start_x, priv->select_start_y, nullptr, &offset_start, &len_start, nullptr);

			/* in case the cursor is out of bounds we keep offset_end from gtk_xtext_find_char and fix the length */
			if (gtk_xtext_get_word(xtext, priv->select_end_x, priv->select_end_y, nullptr, &offset_end, &len_end, nullptr) == nullptr)
				len_end = offset_end == ent_end->str.size() ? 0 : -1; /* -1 for the space, 0 if at the end */

			if (!marking_up)
				offset_end += len_end;
			else
				offset_start += len_start;
		}
		/* line/ent selection */
		else if (priv->line_select)
		{
			offset_start = marking_up ? ent_start->str.size() : 0;
			offset_end = marking_up ? 0 : ent_end->str.size();
		}

		if (marking_up)
		{
			int temp;

			/* ensure ent_start is above ent_end */
			if (ent_start != ent_end)
			{
				auto ent = ent_start;
				ent_start = ent_end;
				ent_end = ent;
			}

			/* switch offsets as well */
			temp = offset_start;
			offset_start = offset_end;
			offset_end = temp;
		}

		/* set all the old mark_ fields to -1 */
		gtk_xtext_selection_clear(priv->buffer);

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
		int apixes = std::abs(pixes);

		if (apixes < 6) return 100;
		if (apixes < 12) return 50;
		if (apixes < 20) return 20;
		return 10;
	}

	static gboolean gtk_xtext_scrolldown_timeout(GtkXText * xtext)
	{
		int p_y, win_height;
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		xtext_buffer *buf = priv->buffer;
		GtkAdjustment *adj = priv->adj;

		gdk_window_get_pointer(gtk_widget_get_window(GTK_WIDGET(xtext)), nullptr, &p_y, nullptr);
		win_height = gdk_window_get_height(gtk_widget_get_window(GTK_WIDGET(xtext)));

		auto adj_value = gtk_adjustment_get_value(adj);
		if (buf->impl->last_ent_end == nullptr ||	/* If context has changed OR */
			buf->impl->pagetop_ent == nullptr ||	/* pagetop_ent is reset OR */
			p_y <= win_height ||			/* pointer not below bottom margin OR */
			adj_value >= gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj)) 	/* we're scrolled to bottom */
		{
			priv->scroll_tag = 0;
			return false;
		}

		priv->select_start_y -= priv->fontsize;
		priv->select_start_adj++;
		adj_value++;
		gtk_adjustment_set_value(adj, adj_value);
		gtk_xtext_selection_draw(xtext, nullptr, true);
		gtk_widget_queue_draw(GTK_WIDGET(xtext));
		priv->scroll_tag = g_timeout_add(gtk_xtext_timeout_ms(p_y - win_height),
			(GSourceFunc)
			gtk_xtext_scrolldown_timeout,
			xtext);

		return false;
	}

	static gboolean gtk_xtext_scrollup_timeout(GtkXText * xtext)
	{
		int p_y;
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		xtext_buffer *buf = priv->buffer;
		GtkAdjustment *adj = priv->adj;
		int delta_y;

		gdk_window_get_pointer(gtk_widget_get_window(GTK_WIDGET(xtext)), nullptr, &p_y, nullptr);
		auto adj_value = gtk_adjustment_get_value(adj);
		if (buf->impl->last_ent_start == nullptr ||	/* If context has changed OR */
			buf->impl->pagetop_ent == nullptr ||		/* pagetop_ent is reset OR */
			p_y >= 0 ||							/* not above top margin OR */
			adj_value == 0.0)						/* we're scrolled to the top */
		{
			priv->scroll_tag = 0;
			return false;
		}

		if (adj_value < 0.0)
		{
			delta_y = adj_value * priv->fontsize;
			adj_value = 0.0;
		}
		else {
			delta_y = priv->fontsize;
			adj_value--;
		}
		priv->select_start_y += delta_y;
		priv->select_start_adj = static_cast<int>(adj_value);
		gtk_adjustment_set_value(adj, adj_value);
		gtk_xtext_selection_draw(xtext, nullptr, true);
		gtk_widget_queue_draw(GTK_WIDGET(xtext));
		priv->scroll_tag = g_timeout_add(gtk_xtext_timeout_ms(p_y),
			(GSourceFunc)
			gtk_xtext_scrollup_timeout,
			xtext);

		return false;
	}

	static void
		gtk_xtext_selection_update(GtkXText * xtext, GdkEventMotion * event, int p_y, bool render)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		if (priv->scroll_tag)
		{
			return;
		}

		int win_height = gdk_window_get_height(gtk_widget_get_window(GTK_WIDGET(xtext)));
		const auto adj_value = gtk_adjustment_get_value(priv->adj);
		/* selecting past top of window, scroll up! */
		if (p_y < 0 && adj_value >= 0)
		{
			gtk_xtext_scrollup_timeout(xtext);
		}

		/* selecting past bottom of window, scroll down! */
		else if (p_y > win_height &&
			adj_value < (gtk_adjustment_get_upper(priv->adj) - gtk_adjustment_get_page_size(priv->adj)))
		{
			gtk_xtext_scrolldown_timeout(xtext);
		}
		else
		{
			int moved = static_cast<int>(adj_value) - priv->select_start_adj;
			priv->select_start_y -= (moved * priv->fontsize);
			priv->select_start_adj = adj_value;
			gtk_xtext_selection_draw(xtext, event, render);
		}
	}

	static const char *
		gtk_xtext_get_word(GtkXText * xtext, int x, int y, textentry ** ret_ent,
		int *ret_off, int *ret_len, std::vector<offlen_t> *slp)
	{
		int offset;
		gboolean out_of_bounds = false;
		int len_to_offset = 0;

		auto ent = gtk_xtext_find_char(xtext, x, y, &offset, &out_of_bounds, nullptr);
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
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		/* remove color characters from the length */
		gtk_xtext_strip_color(ustring_ref(word, len_to_offset), priv->scratch_buffer, &len_to_offset, nullptr, false);

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

		word = gtk_xtext_strip_color(ustring_ref(word, len), priv->scratch_buffer, nullptr, slp, false);

		/* avoid turning the cursor into a hand for non-url part of the word */
		if (priv->urlcheck_function && priv->urlcheck_function(GTK_WIDGET(xtext), reinterpret_cast<const char*>(word)))
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
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		if (priv->cursor_hand)
		{
			priv->hilight_start = -1;
			priv->hilight_end = -1;
			priv->cursor_hand = false;
			gdk_window_set_cursor(gtk_widget_get_window(widget), nullptr);
			priv->hilight_ent = nullptr;
		}

		if (priv->cursor_resize)
		{
			//gtk_xtext_unrender_hilight(xtext);
			priv->hilight_start = -1;
			priv->hilight_end = -1;
			priv->cursor_resize = false;
			gdk_window_set_cursor(gtk_widget_get_window(widget), nullptr);
			priv->hilight_ent = nullptr;
		}

		gtk_widget_queue_draw(widget);
		return false;
	}

	/* check if we should mark time stamps, and if a redraw is needed */

	static gboolean
		gtk_xtext_check_mark_stamp(GtkXText *xtext, GdkModifierType mask)
	{
		gboolean redraw = false;
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		if (mask & STATE_SHIFT || prefs.hex_text_autocopy_stamp)
		{
			if (!priv->mark_stamp)
			{
				redraw = true;	/* must redraw all */
				priv->mark_stamp = true;
			}
		}
		else
		{
			if (priv->mark_stamp)
			{
				redraw = true;	/* must redraw all */
				priv->mark_stamp = false;
			}
		}
		return redraw;
	}

	static int
		gtk_xtext_get_word_adjust(GtkXText *xtext, int x, int y, textentry **word_ent, int *offset, int *len)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		std::vector<offlen_t> slp;

		auto word = gtk_xtext_get_word(xtext, x, y, word_ent, offset, len, &slp);
		if (!word)
		{
			return 0;
		}

		int laststart, lastend;

		int word_type = priv->urlcheck_function(GTK_WIDGET(xtext), word);
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
		int redraw, tmp, x, y, offset, len, line_x;
		int word_type;

		gdk_window_get_pointer(gtk_widget_get_window(widget), &x, &y, &mask);
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		if (priv->moving_separator)
		{
			GtkAllocation allocation;
			gtk_widget_get_allocation(widget, &allocation);
			if (x < (3 * allocation.width) / 5 && x > 15)
			{
				tmp = priv->buffer->indent;
				priv->buffer->indent = x;
				gtk_xtext_fix_indent(priv->buffer);
				if (tmp != priv->buffer->indent)
				{
					gtk_xtext_recalc_widths(priv->buffer, false);
					if (priv->buffer->scrollbar_down)
						gtk_adjustment_set_value(priv->adj, gtk_adjustment_get_upper(priv->adj) -
						gtk_adjustment_get_page_size(priv->adj));
					if (!priv->io_tag)
						priv->io_tag = g_timeout_add(REFRESH_TIMEOUT,
						(GSourceFunc)
						gtk_xtext_adjustment_timeout,
						xtext);
				}
			}
			return false;
		}

		if (priv->button_down)
		{
			redraw = gtk_xtext_check_mark_stamp(xtext, mask);
			gtk_grab_add(widget);
			/*gdk_pointer_grab (widget->window, true,
			GDK_BUTTON_RELEASE_MASK |
			GDK_BUTTON_MOTION_MASK, nullptr, nullptr, 0);*/
			priv->select_end_x = x;
			priv->select_end_y = y;
			gtk_xtext_selection_update(xtext, event, y, !redraw);
			priv->hilighting = true;

			gtk_widget_queue_draw(widget);
			return false;
		}

		if (priv->separator && priv->buffer->indent)
		{
			line_x = priv->buffer->indent - ((priv->space_width + 1) / 2);
			if (line_x == x || line_x == x + 1 || line_x == x - 1)
			{
				if (!priv->cursor_resize)
				{
					gdk_window_set_cursor(gtk_widget_get_window(GTK_WIDGET(xtext)),
						priv->resize_cursor);
					priv->cursor_hand = false;
					priv->cursor_resize = true;
				}
				return false;
			}
		}

		if (priv->urlcheck_function == nullptr)
			return false;

		textentry *word_ent;
		word_type = gtk_xtext_get_word_adjust(xtext, x, y, &word_ent, &offset, &len);
		if (word_type > 0)
		{
			if (!priv->cursor_hand ||
				priv->hilight_ent != word_ent ||
				priv->hilight_start != offset ||
				priv->hilight_end != offset + len)
			{
				if (!priv->cursor_hand)
				{
					gdk_window_set_cursor(gtk_widget_get_window(GTK_WIDGET(xtext)),
						priv->hand_cursor);
					priv->cursor_hand = true;
					priv->cursor_resize = false;
				}

				priv->hilight_ent = word_ent;
				priv->hilight_start = offset;
				priv->hilight_end = offset + len;

				priv->skip_border_fills = false;
				priv->render_hilights_only = false;
				priv->skip_stamp = false;
				gtk_widget_queue_draw(widget);
			}
			return false;
		}

		gtk_xtext_leave_notify(widget, nullptr);
		return false;
	}

	static void gtk_xtext_set_clip_owner(GtkWidget * xtext, GdkEventButton * event)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(GTK_XTEXT(xtext)));
		if (priv->selection_buffer &&
			priv->selection_buffer != priv->buffer)
			gtk_xtext_selection_clear(priv->selection_buffer);

		priv->selection_buffer = priv->buffer;

		auto str = gtk_xtext_selection_get_text(GTK_XTEXT(xtext));
		if (!str.empty())
		{
			gtk_clipboard_set_text(gtk_widget_get_clipboard(xtext, GDK_SELECTION_CLIPBOARD), str.c_str(), str.size());

			gtk_selection_owner_set(xtext, GDK_SELECTION_PRIMARY, event ? event->time : GDK_CURRENT_TIME);
			gtk_selection_owner_set(xtext, GDK_SELECTION_SECONDARY, event ? event->time : GDK_CURRENT_TIME);
		}
	}
} // end anonymous namespace
void
gtk_xtext_copy_selection(GtkXText *xtext)
{
	gtk_xtext_set_clip_owner(GTK_WIDGET(xtext), nullptr);
}

void gtk_xtext_set_ignore_hidden(GtkXText * xtext, bool ignore_hidden)
{
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
	priv->ignore_hidden = ignore_hidden;
}

namespace {
	void gtk_xtext_unselect(GtkXText &xtext)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(&xtext));
		xtext_buffer *buf = priv->buffer;

		priv->skip_border_fills = true;
		priv->skip_stamp = true;
		if (!buf->impl->last_ent_start)
			return;
		priv->jump_in_offset = buf->impl->last_ent_start->mark_start;
		/* just a single ent was marked? */
		if (buf->impl->last_ent_start == buf->impl->last_ent_end)
		{
			priv->jump_out_offset = buf->impl->last_ent_start->mark_end;
			buf->impl->last_ent_end = nullptr;
		}

		gtk_xtext_selection_clear(priv->buffer);

		/* FIXME: use jump_out on multi-line selects too! */
		priv->jump_in_offset = 0;
		priv->jump_out_offset = 0;
		priv->skip_border_fills = false;
		priv->skip_stamp = false;

		priv->buffer->impl->last_ent_start = nullptr;
		priv->buffer->impl->last_ent_end = nullptr;
		gtk_widget_queue_draw(GTK_WIDGET(&xtext));
	}

	static gboolean
		gtk_xtext_button_release(GtkWidget * widget, GdkEventButton * event)
	{
		GtkXText *xtext = GTK_XTEXT(widget);
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		if (priv->moving_separator)
		{
			priv->moving_separator = false;
			auto old = priv->buffer->indent;
			GtkAllocation allocation;
			gtk_widget_get_allocation(widget, &allocation);
			if (event->x < (4 * allocation.width) / 5 && event->x > 15)
				priv->buffer->indent = event->x;
			gtk_xtext_fix_indent(priv->buffer);
			if (priv->buffer->indent != old)
			{
				gtk_xtext_recalc_widths(priv->buffer, false);
				gtk_xtext_adjustment_set(priv->buffer, true);
			}
			
			gtk_widget_queue_draw(widget);
			return false;
		}

		if (event->button == 1)
		{
			priv->button_down = false;
			if (priv->scroll_tag)
			{
				g_source_remove(priv->scroll_tag);
				priv->scroll_tag = 0;
			}

			gtk_grab_remove(widget);
			/*gdk_pointer_ungrab (0);*/

			/* got a new selection? */
			if (priv->buffer->impl->last_ent_start)
			{
				priv->color_paste = false;
				if (event->state & STATE_CTRL || prefs.hex_text_autocopy_color)
					priv->color_paste = true;
				if (prefs.hex_text_autocopy_text)
				{
					gtk_xtext_set_clip_owner(GTK_WIDGET(xtext), event);
				}
			}

			if (priv->word_select || priv->line_select)
			{
				priv->word_select = false;
				priv->line_select = false;
				gtk_widget_queue_draw(widget);
				return false;
			}

			if (priv->select_start_x == event->x &&
				priv->select_start_y == event->y &&
				priv->buffer->impl->last_ent_start)
			{
				gtk_xtext_unselect(*xtext);
				priv->mark_stamp = false;
				gtk_widget_queue_draw(widget);
				return false;
			}

			if (!priv->hilighting)
			{
				auto word = gtk_xtext_get_word(xtext, event->x, event->y, 0, 0, 0, 0);
				g_signal_emit(G_OBJECT(xtext), xtext_signals[WORD_CLICK], 0, word ? word : nullptr, event);
			}
			else
			{
				priv->hilighting = false;
			}
		}

		return false;
	}

	static gboolean
		gtk_xtext_button_press(GtkWidget * widget, GdkEventButton * event)
	{
		GtkXText *xtext = GTK_XTEXT(widget);
		GdkModifierType mask;
		int line_x, x, y, offset, len;

		gdk_window_get_pointer(gtk_widget_get_window(widget), &x, &y, &mask);

		if (event->button == 3 || event->button == 2) /* right/middle click */
		{
			auto word = gtk_xtext_get_word(xtext, x, y, nullptr, nullptr, nullptr, nullptr);
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
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		if (event->type == GDK_2BUTTON_PRESS)	/* WORD select */
		{
			gtk_xtext_selection_clear(priv->buffer);
			textentry *ent;
			gtk_xtext_check_mark_stamp(xtext, mask);
			if (gtk_xtext_get_word(xtext, x, y, &ent, &offset, &len, 0))
			{
				if (len == 0)
					return false;
				
				ent->mark_start = offset;
				ent->mark_end = offset + len;
				priv->buffer->impl->last_ent_start = ent;
				priv->buffer->impl->last_ent_end = ent;
				priv->word_select = true;
			}
			gtk_widget_queue_draw(widget);
			return false;
		}

		if (event->type == GDK_3BUTTON_PRESS)	/* LINE select */
		{
			textentry *ent;
			gtk_xtext_check_mark_stamp(xtext, mask);
			if (gtk_xtext_get_word(xtext, x, y, &ent, 0, 0, 0))
			{
				gtk_xtext_selection_clear(priv->buffer);
				ent->mark_start = 0;
				ent->mark_end = ent->str.size();
				//gtk_xtext_selection_render(xtext, ent, ent);
				priv->line_select = true;
			}
			gtk_widget_queue_draw(widget);
			return false;
		}

		/* check if it was a separator-bar click */
		if (priv->separator && priv->buffer->indent)
		{
			line_x = priv->buffer->indent - ((priv->space_width + 1) / 2);
			if (line_x == x || line_x == x + 1 || line_x == x - 1)
			{
				priv->moving_separator = true;
				gtk_widget_queue_draw(widget);
				return false;
			}
		}

		priv->button_down = true;
		priv->select_start_x = x;
		priv->select_start_y = y;
		priv->select_start_adj = gtk_adjustment_get_value(priv->adj);
		gtk_widget_queue_draw(widget);
		return false;
	}

	/* another program has claimed the selection */

	gboolean gtk_xtext_selection_kill(GtkXText *xtext, GdkEventSelection *)
	{
#ifndef WIN32
		if (priv->buffer->impl->last_ent_start)
			gtk_xtext_unselect(*xtext);
#else
		UNREFERENCED_PARAMETER(xtext);
#endif
		return true;
	}

	std::string gtk_xtext_selection_get_text(GtkXText *xtext)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		auto buf = priv->selection_buffer;
		if (!buf)
			return{};

		/* first find out how much we need to malloc ... */
		int len = 0;
		auto ent = buf->impl->last_ent_start;
		while (ent)
		{
			if (ent->mark_start != -1)
			{
				/* include timestamp? */
				if (ent->mark_start == 0 && priv->mark_stamp)
				{
					auto time_str = xtext_get_stamp_str(ent->stamp);
					len += time_str.size();
				}

				if (ent->mark_end - ent->mark_start > 0)
					len += (ent->mark_end - ent->mark_start) + 1;
				else
					len++;
			}
			if (ent == buf->impl->last_ent_end)
				break;
			ent = ent->next;
		}

		if (len < 1)
			return{};

		/* now allocate mem and copy buffer */
		std::ostringstream out;
		std::ostream_iterator<char> pos{ out };
		ent = buf->impl->last_ent_start;
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
					if (ent->mark_start == 0 && priv->mark_stamp)
					{
						auto time_str = xtext_get_stamp_str(ent->stamp);
						std::copy(time_str.cbegin(), time_str.cend(), pos);
					}

					std::copy_n(ent->str.cbegin() + ent->mark_start, ent->mark_end - ent->mark_start, pos);
				}
			}
			if (ent == buf->impl->last_ent_end)
				break;
			ent = ent->next;
		}

		std::string stripped = out.str();
		if (!priv->color_paste)
		{
			auto result = gtk_xtext_strip_color(ustring_ref(reinterpret_cast<const unsigned char*>(stripped.c_str()), stripped.size()), nullptr, false);
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
		auto stripped = gtk_xtext_selection_get_text(GTK_XTEXT(widget));
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

		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		if (priv->adj && (priv->adj != vadj))
		{
			g_signal_handlers_disconnect_by_func(priv->adj,
				(void*)gtk_xtext_adjustment_changed,
				xtext);
			g_object_unref(priv->adj);
		}

		if (priv->adj != vadj)
		{
			priv->adj = vadj;
			g_object_ref_sink(priv->adj);

			priv->vc_signal_tag = g_signal_connect(priv->adj, "value-changed",
				G_CALLBACK(gtk_xtext_adjustment_changed),
				xtext);

			gtk_xtext_adjustment_changed(priv->adj, xtext);
		}
	}

	
} // end anonymous namespace
//GType
//gtk_xtext_get_type()
//{
//	static volatile gsize g_define_type_id__volatile = 0;
//	if (g_once_init_enter(&g_define_type_id__volatile))
//	{
//		static const GTypeInfo xtext_info =
//		{
//			sizeof(GtkXTextClass),
//			nullptr,		/* base_init */
//			nullptr,		/* base_finalize */
//			(GClassInitFunc)gtk_xtext_class_init,
//			nullptr,		/* class_finalize */
//			nullptr,		/* class_data */
//			sizeof(GtkXText),
//			0,		/* n_preallocs */
//			(GInstanceInitFunc)gtk_xtext_init,
//		};
//
//		auto g_define_type_id = g_type_register_static(GTK_TYPE_WIDGET, "GtkXText",
//			&xtext_info, GTypeFlags());
//		g_once_init_leave(&g_define_type_id__volatile, g_define_type_id);
//	}
//	
//	return g_define_type_id__volatile;
//}

/* strip MIRC colors and other attribs. */

/* CL: needs to strip hidden when called by gtk_xtext_text_width, but not when copying text */

namespace{

	struct chunk_t {
		std::vector<offlen_t> slp;
		int off1;
		int len1;
		int emph;
		offlen_t meta;
	};

	void xtext_do_chunk(chunk_t &c)
	{
		if (c.len1 == 0)
			return;

		offlen_t meta;
		meta.off = c.off1;
		meta.len = c.len1;
		meta.emph = c.emph;
		meta.width = 0;
		c.slp.emplace_back(meta);

		c.len1 = 0;
	}

	/* deprecated */
	unsigned char * gtk_xtext_strip_color(const ustring_ref & text, unsigned char *outbuf,
		int *newlen, std::vector<offlen_t> * slp, int strip_hidden)
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
			int mbl = charlen(itr); /* multi-byte length */
			if (mbl > std::distance(itr, end))
				break; // bad UTF-8

			if (rcol > 0 && (std::isdigit<char>(*itr, locale) || (*itr == ',' && std::isdigit<char>(itr[1], locale) && !bgcol)))
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

	ustring gtk_xtext_strip_color(const ustring_ref &text,
				      std::vector<offlen_t> *slp,
				      int strip_hidden)
	{
		chunk_t c;
		int rcol = 0, bgcol = 0;
		bool hidden = false;
		auto beginning = text.cbegin();
		using oustringstream = std::basic_stringstream<unsigned char>;
		oustringstream outbuf;
		std::ostream_iterator<unsigned char, unsigned char> out{ outbuf };

		c.off1 = 0;
		c.len1 = 0;
		c.emph = 0;
		std::locale locale;
		for (auto itr = text.cbegin(), end = text.cend(); itr != end;)
		{
			int mbl = charlen(itr); /* multi-byte length */
			if (mbl > std::distance(itr, end))
				break; // bad UTF-8

			if (rcol > 0 &&
			    (std::isdigit<char>(*itr, locale) ||
			     (*itr == ',' &&
			      std::isdigit<char>(itr[1], locale) && !bgcol)))
			{
				if (itr[1] != ',')
					rcol--;
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
					if (strip_hidden == 2 ||
					    (!(hidden && strip_hidden)))
					{
						if (c.len1 == 0)
						{
							c.off1 = std::distance(
								beginning, itr);
						}

						std::copy_n(itr, mbl, out);
						c.len1 += mbl;
					}
				}
			}
			itr += mbl;
		}

		xtext_do_chunk(c);

		if (slp)
			*slp = std::move(c.slp);

		return outbuf.str();
	}

	/* gives width of a string, excluding the mIRC codes */
	static int gtk_xtext_text_width_ent(GtkXText *xtext, textentry &ent)
	{
		ent.slp.clear();
		auto new_buf = gtk_xtext_strip_color(ent.str, &ent.slp, 2);

		auto width =
		    backend_get_text_width_slp(xtext, new_buf.c_str(), ent.slp);

		for (auto &meta : ent.slp)
		{
			meta.width = backend_get_text_width_emph(
			    xtext,
			    ustring_ref(ent.str.c_str() + meta.off, meta.len),
			    meta.emph);
		}
		return width;
	}

	int gtk_xtext_text_width(GtkXText *xtext, const ustring_ref & text)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		std::vector<offlen_t> slp;
		auto new_buf = gtk_xtext_strip_color(text, &slp, !priv->ignore_hidden);

		return backend_get_text_width_slp(xtext, new_buf.c_str(), slp);
	}

	void gtk_xtext_draw_underline(GtkXText *xtext, int dest_x, int dest_y,
				      int x, int y, int str_width,
				      bool drawable, cairo_t *cr)
	{
		cairo_stack cr_stack{ cr };
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		// used to check if the pixmap existed, why does this matter?
		if (drawable)
			y = dest_y + priv->font->ascent + 1;
		else
		{
			y++;
			dest_x = x;
		}
		/* draw directly to window, it's out of the range of our
		* DB */
		gdk_cairo_set_source_color(cr, bridge_get_foreground(priv->style));
		const auto mid_pixel_y = y + 0.5;
		cairo_move_to(cr, dest_x, mid_pixel_y);
		cairo_line_to(cr, dest_x + str_width - 1, mid_pixel_y);
		cairo_set_line_width(cr, 1.0);
		cairo_stroke(cr);
	}

	/* actually draw text to screen (one run with the same color/attribs) */
	static int gtk_xtext_render_flush(GtkXText *xtext, cairo_t*cr, int x, int y,
					  const ustring_ref& str, int *emphasis)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		if (priv->dont_render || str.empty() || priv->hidden)
			return 0;

		auto str_width = backend_get_text_width_emph(
		    xtext, str, *emphasis);

		if (priv->dont_render2)
			return str_width;

		/* roll-your-own clipping (avoiding XftDrawString is always
		 * good!) */
		if (x > priv->clip_x2 || x + str_width < priv->clip_x)
			return str_width;
		if (y - priv->font->ascent > priv->clip_y2 ||
		    (y - priv->font->ascent) + priv->fontsize < priv->clip_y)
			return str_width;

		int dest_x = 0, dest_y = 0;
		if (priv->render_hilights_only)
		{
			if (!priv->in_hilight) /* is it a hilight prefix? */
				return str_width;
			if (!priv->un_hilight) /* doing a hilight? no need to
						   draw the text */
			{
				gtk_xtext_draw_underline(xtext, dest_x, dest_y, x, y, str_width, false, cr);
				return str_width;
			}
		}

		/*auto pix = gdk_pixmap_new(priv->draw_buf, str_width,
				     priv->fontsize, priv->depth);
		if (pix)
		{
			dest_x = x;
			dest_y = y - priv->font->ascent;

			gdk_gc_set_ts_origin(priv->bgc, priv->ts_x - x,
					     priv->ts_y - dest_y);

			x = 0;
			y = priv->font->ascent;
			priv->draw_buf = pix;
		}*/

		bool dofill = true;

		backend_draw_text_emph(xtext, dofill, cr, x, y - priv->font->ascent,
				       str, str_width,
				       *emphasis);

		if (priv->underline)
		{
			gtk_xtext_draw_underline(xtext, dest_x, dest_y, x, y, str_width, false, cr);
		}

		return str_width;
	}

	static void
		gtk_xtext_reset(GtkXText * xtext, bool mark, bool attribs)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		if (attribs)
		{
			priv->underline = false;
			priv->hidden = false;
		}
		if (!mark)
		{
			priv->backcolor = false;
			if (priv->col_fore != XTEXT_FG)
				xtext_set_fg(xtext, XTEXT_FG);
			if (priv->col_back != XTEXT_BG)
				xtext_set_bg(xtext, XTEXT_BG);
		}
		priv->col_fore = XTEXT_FG;
		priv->col_back = XTEXT_BG;
		priv->parsing_color = false;
		priv->parsing_backcolor = false;
		priv->nc = 0;
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

	/* render a single line, which WONT wrap, and parse mIRC colors */

	static int
		gtk_xtext_render_str(GtkXText * xtext, cairo_t* cr, int y, textentry * ent,
		const unsigned char str[], int len, int win_width, int indent,
		int /*line*/, bool left_only, int *x_size_ret, int *emphasis)
	{
		cairo_stack cr_stack{ cr };
		int i = 0, x = indent, j = 0;
		const unsigned char *pstr = str;
		bool mark = false;
		int ret = 1;
		bool srch_underline = false;
		bool srch_mark = false;
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		priv->in_hilight = false;

		auto offset = str - ent->str.c_str();

		if (ent->mark_start != -1 &&
			ent->mark_start <= i + offset && ent->mark_end > i + offset)
		{
			xtext_set_bg(xtext, XTEXT_MARK_BG);
			xtext_set_fg(xtext, XTEXT_MARK_FG);
			priv->backcolor = true;
			mark = true;
		}
		if (priv->hilight_ent == ent &&
			priv->hilight_start <= i + offset && priv->hilight_end > i + offset)
		{
			if (!priv->un_hilight)
			{
				priv->underline = true;
			}
			priv->in_hilight = true;
		}

		if (priv->jump_in_offset > 0 && offset < priv->jump_in_offset)
			priv->dont_render2 = true;
		std::locale locale;
		while (i < len)
		{

			if (priv->hilight_ent == ent && priv->hilight_start == (i + offset))
			{
				x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
				pstr += j;
				j = 0;
				if (!priv->un_hilight)
				{
					priv->underline = true;
				}

				priv->in_hilight = true;
			}

			if ((priv->parsing_color && std::isdigit<char>(str[i], locale) && priv->nc < 2) ||
				(priv->parsing_color && str[i] == ',' && std::isdigit<char>(str[i + 1], locale) && priv->nc < 3 && !priv->parsing_backcolor))
			{
				pstr++;
				if (str[i] == ',')
				{
					priv->parsing_backcolor = true;
					if (priv->nc)
					{
						priv->num[priv->nc] = 0;
						priv->nc = 0;
						auto col_num = atoi(priv->num);
						if (col_num == 99)	/* mIRC lameness */
							col_num = XTEXT_FG;
						else
							if (col_num > XTEXT_MAX_COLOR)
								col_num = col_num % XTEXT_MIRC_COLS;
						priv->col_fore = col_num;
						if (!mark)
							xtext_set_fg(xtext, col_num);
					}
				}
				else
				{
					priv->num[priv->nc] = str[i];
					if (priv->nc < 7)
						priv->nc++;
				}
			}
			else
			{
				if (priv->parsing_color)
				{
					priv->parsing_color = false;
					if (priv->nc)
					{
						priv->num[priv->nc] = 0;
						priv->nc = 0;
						auto col_num = atoi(priv->num);
						if (priv->parsing_backcolor)
						{
							if (col_num == 99)	/* mIRC lameness */
								col_num = XTEXT_BG;
							else
								if (col_num > XTEXT_MAX_COLOR)
									col_num = col_num % XTEXT_MIRC_COLS;
							if (col_num == XTEXT_BG)
								priv->backcolor = false;
							else
								priv->backcolor = true;
							if (!mark)
								xtext_set_bg(xtext, col_num);
							priv->col_back = col_num;
						}
						else
						{
							if (col_num == 99)	/* mIRC lameness */
								col_num = XTEXT_FG;
							else
								if (col_num > XTEXT_MAX_COLOR)
									col_num = col_num % XTEXT_MIRC_COLS;
							if (!mark)
								xtext_set_fg(xtext, col_num);
							priv->col_fore = col_num;
						}
						priv->parsing_backcolor = false;
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
					(k = gtk_xtext_search_offset(priv->buffer, ent, offset + i)))
				{
					x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
					pstr += j;
					j = 0;
					if (!(priv->buffer->search_flags & highlight))
					{
						if (k & GTK_MATCH_CUR)
						{
							xtext_set_bg(xtext, XTEXT_MARK_BG);
							xtext_set_fg(xtext, XTEXT_MARK_FG);
							priv->backcolor = true;
							srch_mark = true;
						}
						else
						{
							xtext_set_bg(xtext, priv->col_back);
							xtext_set_fg(xtext, priv->col_fore);
							priv->backcolor = (priv->col_back != XTEXT_BG) ? true : false;
							srch_mark = false;
						}
					}
					else
					{
						priv->underline = (k & GTK_MATCH_CUR) ? true : false;
						if (k & (GTK_MATCH_START | GTK_MATCH_MID))
						{
							xtext_set_bg(xtext, XTEXT_MARK_BG);
							xtext_set_fg(xtext, XTEXT_MARK_FG);
							priv->backcolor = true;
							srch_mark = true;
						}
						if (k & GTK_MATCH_END)
						{
							xtext_set_bg(xtext, priv->col_back);
							xtext_set_fg(xtext, priv->col_fore);
							priv->backcolor = (priv->col_back != XTEXT_BG) ? true : false;
							srch_mark = false;
							priv->underline = false;
						}
						srch_underline = priv->underline;
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
					tmp = priv->col_fore;
					priv->col_fore = priv->col_back;
					priv->col_back = tmp;
					if (!mark)
					{
						xtext_set_fg(xtext, priv->col_fore);
						xtext_set_bg(xtext, priv->col_back);
					}
					if (priv->col_back != XTEXT_BG)
						priv->backcolor = true;
					else
						priv->backcolor = false;
					break;
				case ATTR_BOLD:
					x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
					*emphasis ^= EMPH_BOLD;
					pstr += j + 1;
					j = 0;
					break;
				case ATTR_UNDERLINE:
					x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
					priv->underline = !priv->underline;
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
					priv->hidden = (!priv->hidden) && (!priv->ignore_hidden);
					pstr += j + 1;
					j = 0;
					break;
				case ATTR_RESET:
					x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
					*emphasis = 0;
					pstr += j + 1;
					j = 0;
					gtk_xtext_reset(xtext, mark, !priv->in_hilight);
					break;
				case ATTR_COLOR:
					x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
					priv->parsing_color = true;
					pstr += j + 1;
					j = 0;
					break;
				default:
					tmp = charlen(str + i);
					/* invalid utf8 safe guard */
					if (tmp + i > len)
						tmp = len - i;
					j += tmp;	/* move to the next utf8 char */
				}
			}
			i += charlen(str + i);	/* move to the next utf8 char */
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
				if ((pstr - str) + j == ent->left_len)
				{
					x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
					pstr += j;
					j = 0;
				}
				else if ((pstr - str) + j == ent->left_len + 1)
				{
					x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
					pstr += j;
					j = 0;
				}
			}

			/* have we been told to stop rendering at this point? */
			if (priv->jump_out_offset > 0 && priv->jump_out_offset <= (i + offset))
			{
				gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
				ret = 0;	/* skip the rest of the lines, we're done. */
				j = 0;
				break;
			}

			if (priv->jump_in_offset > 0 && priv->jump_in_offset == (i + offset))
			{
				x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
				pstr += j;
				j = 0;
				priv->dont_render2 = false;
			}

			if (priv->hilight_ent == ent && priv->hilight_end == (i + offset))
			{
				x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
				pstr += j;
				j = 0;
				priv->underline = false;
				priv->in_hilight = false;
				if (priv->render_hilights_only)
				{
					/* stop drawing this ent */
					ret = 0;
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
				priv->backcolor = true;
				if (srch_underline)
				{
					priv->underline = false;
					srch_underline = false;
				}
				mark = true;
			}

			if (mark && ent->mark_end == (i + offset))
			{
				x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);
				pstr += j;
				j = 0;
				xtext_set_bg(xtext, priv->col_back);
				xtext_set_fg(xtext, priv->col_fore);
				if (priv->col_back != XTEXT_BG)
					priv->backcolor = true;
				else
					priv->backcolor = false;
				mark = false;
			}

		}

		if (j)
			x += gtk_xtext_render_flush(xtext, cr, x, y, ustring_ref(pstr, j), emphasis);

		if (mark || srch_mark)
		{
			xtext_set_bg(xtext, priv->col_back);
			xtext_set_fg(xtext, priv->col_fore);
			if (priv->col_back != XTEXT_BG)
				priv->backcolor = true;
			else
				priv->backcolor = false;
		}

		priv->dont_render2 = false;

		/* return how much we drew in the x direction */
		if (x_size_ret)
			*x_size_ret = x - indent;

		return ret;
	}

	/* walk through str until this line doesn't fit anymore */

	static int
		find_next_wrap(GtkXText * xtext, const textentry & ent, const unsigned char str[],
		int win_width, int indent)
	{
		/* single liners */
		if (win_width >= ent.str_width + ent.indent)
			return ent.str.size();

		auto last_space = str;
		auto orig_str = str;
		int str_width = indent;
		int rcol = 0, bgcol = 0;
		bool hidden = false;
		int ret;
		int limit_offset = 0;
		int emphasis = 0;
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
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
					if (priv->ignore_hidden)
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
						if (priv->wordwrap)
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

	void gtk_xtext_render_stamp(GtkXText * xtext, cairo_t* cr, textentry * ent,
		const boost::string_ref & text, int line, int win_width)
	{

		/* trashing ent here, so make a backup first */
		textentry tmp_ent(*ent);
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		auto jo = priv->jump_out_offset;	/* back these up */
		auto ji = priv->jump_in_offset;
		auto hs = priv->hilight_start;
		priv->jump_out_offset = 0;
		priv->jump_in_offset = 0;
		priv->hilight_start = 0xffff;	/* temp disable */

		if (priv->mark_stamp)
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
			tmp_ent.str = ustring{ text.cbegin(), text.cend() };
		}

		int xsize, emphasis = 0;
		auto y = (priv->fontsize * line) + priv->font->ascent - priv->pixel_offset;
		gtk_xtext_render_str(xtext,cr, y, &tmp_ent, (const unsigned char*)text.data(), text.size(),
			win_width, 2, line, true, &xsize, &emphasis);

		/* restore everything back to how it was */
		priv->jump_out_offset = jo;
		priv->jump_in_offset = ji;
		priv->hilight_start = hs;

		/* with a non-fixed-width font, sometimes we don't draw enough
		background i.e. when this stamp is shorter than priv->stamp_width */
		//xsize += MARGIN;
		//if (xsize < priv->stamp_width)
		//{
		//	y -= priv->font->ascent;
		//	//xtext_draw_bg(xtext,
		//	//	xsize,	/* x */
		//	//	y,			/* y */
		//	//	priv->stamp_width - xsize,	/* width */
		//	//	priv->fontsize					/* height */);
		//}
	}

	/* render a single line, which may wrap to more lines */

	int gtk_xtext_render_line(GtkXText * xtext, cairo_t* cr, textentry * ent, int line,
		int lines_max, int subline, int win_width)
	{
		const unsigned char *str;
		int indent, taken, entline, len, y, start_subline;
		int emphasis = 0;

		entline = taken = 0;
		str = ent->str.c_str();
		indent = ent->indent;
		start_subline = subline;
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		/* draw the timestamp */
		if (priv->auto_indent && priv->buffer->time_stamp &&
			(!priv->skip_stamp || priv->mark_stamp || priv->force_stamp))
		{
			auto time_str = xtext_get_stamp_str(ent->stamp);
			gtk_xtext_render_stamp(xtext, cr, ent, time_str, line, win_width);
		}

		/* draw each line one by one */
		do
		{
			if (entline > 0)
				len = ent->sublines[entline] - ent->sublines[entline - 1];
			else
				len = ent->sublines[entline];

			entline++;

			y = (priv->fontsize * line) + priv->font->ascent - priv->pixel_offset;
			if (!subline)
			{
				if (!gtk_xtext_render_str(xtext,cr, y, ent, str, len, win_width,
					indent, line, false, nullptr, &emphasis))
				{
					/* small optimization */
					gtk_xtext_draw_marker(xtext, cr, ent, y - priv->fontsize * (taken + start_subline + 1));
					return ent->sublines.size() - subline;
				}
			}
			else
			{
				priv->dont_render = true;
				gtk_xtext_render_str(xtext,cr, y, ent, str, len, win_width,
					indent, line, false, nullptr, &emphasis);
				priv->dont_render = false;
				subline--;
				line--;
				taken--;
			}

			indent = priv->buffer->indent;
			line++;
			taken++;
			str += len;

			if (line >= lines_max)
				break;

		} while (str < ent->str.c_str() + ent->str.size());

		gtk_xtext_draw_marker(xtext, cr, ent, y - priv->fontsize * (taken + start_subline));

		return taken;
	}
} // end anonymous namespace

void
gtk_xtext_set_palette(GtkXText * xtext, GdkColor palette[])
{
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
	for (int i = (XTEXT_COLS - 1); i >= 0; i--)
	{
		priv->palette[i] = palette[i];
	}

	if (gtk_widget_get_realized(GTK_WIDGET(xtext)))
	{
		xtext_set_fg(xtext, XTEXT_FG);
		xtext_set_bg(xtext, XTEXT_BG);
		xtext_set_fg(xtext, XTEXT_BG);
	}
	priv->col_fore = XTEXT_FG;
	priv->col_back = XTEXT_BG;
}

namespace {

	static void
		gtk_xtext_fix_indent(xtext_buffer *buf)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(buf->xtext));
		/* make indent a multiple of the space width */
		if (buf->indent && priv->space_width)
		{
			int j = 0;
			while (j < buf->indent)
			{
				j += priv->space_width;
			}
			buf->indent = j;
		}

		dontscroll(buf);	/* force scrolling off */
	}

	static void gtk_xtext_recalc_widths(xtext_buffer *buf,
					    bool do_str_width)
	{
		/* since we have a new font, we have to recalc the text widths
		 */
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(buf->xtext));
		for (auto &ent : buf->impl->entries)
		{
			if (do_str_width)
			{
				ent.str_width =
				    gtk_xtext_text_width_ent(buf->xtext, ent);
			}
			if (ent.left_len != -1)
			{
				ent.indent = (buf->indent -
					      gtk_xtext_text_width(
						  buf->xtext,
						  ustring_ref(ent.str.c_str(),
							      ent.left_len))) -
					     priv->space_width;
				if (ent.indent < MARGIN)
					ent.indent = MARGIN;
			}
		}

		gtk_xtext_calc_lines(buf, false);
	}
} // end anonymous namespace

bool gtk_xtext_set_font(GtkXText *xtext, const char name[])
{
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
	if (priv->font)
		backend_font_close(xtext);

	/* realize now, so that font_open has a XDisplay */
	gtk_widget_realize(GTK_WIDGET(xtext));

	backend_font_open(xtext, name);
	if (priv->font == nullptr)
		return false;

	priv->fontsize = priv->font->ascent + priv->font->descent;

	{
		auto time_str = xtext_get_stamp_str(std::time(nullptr));
		priv->stamp_width =
			gtk_xtext_text_width(xtext, ustring_ref(reinterpret_cast<const unsigned char*>(time_str.c_str()), time_str.size())) + MARGIN;
	}

	gtk_xtext_fix_indent(priv->buffer);

	if (gtk_widget_get_realized(GTK_WIDGET(xtext)))
		gtk_xtext_recalc_widths(priv->buffer, true);

	return true;
}

namespace xtext{
	void save(const GtkXText & xtext, std::ostream & outfile)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(const_cast<GtkXText*>(&xtext)));
		for (const auto & ent : priv->buffer->impl->entries)
		{
			if (!outfile)
			{
				break;
			}
			auto buf = gtk_xtext_strip_color(ent.str, nullptr, false);
			boost::string_ref ref(reinterpret_cast<const char*>(buf.data()), buf.length());
			outfile << ref << '\n';
		}
	}
}

namespace{
	/* count how many lines 'ent' will take (with wraps) */

	int gtk_xtext_lines_taken(xtext_buffer *buf, textentry & ent)
	{
		ent.sublines.clear();
		int win_width = buf->window_width - MARGIN;

		if (win_width >= ent.indent + ent.str_width)
		{
			ent.sublines.push_back(ent.str.size());
			return 1;
		}

		int indent = ent.indent;
		auto str = ent.str.c_str();

		do
		{
			int len = find_next_wrap(buf->xtext, ent, str, win_width, indent);
			ent.sublines.push_back(str + len - ent.str.c_str());
			indent = buf->indent;
			str += len;
		} while (str < ent.str.c_str() + ent.str.size());

		return ent.sublines.size();
	}

	/* Calculate number of actual lines (with wraps), to set adj->lower. *
	* This should only be called when the window resizes.               */
	void gtk_xtext_calc_lines(xtext_buffer *buf, bool fire_signal)
	{
		auto window = gtk_widget_get_window(GTK_WIDGET(buf->xtext));
		int height = gdk_window_get_height(window);
		int width = gdk_window_get_width(window) - MARGIN;
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(buf->xtext));
		if (width < 30 || height < priv->fontsize ||
		    width < buf->indent + 30)
			return;

		int lines = 0;
		for (auto &ent : buf->impl->entries)
		{
			lines += gtk_xtext_lines_taken(buf, ent);
		}

		buf->impl->pagetop_ent = nullptr;
		buf->num_lines = lines;
		gtk_xtext_adjustment_set(buf, fire_signal);
	}

	/* find the n-th line in the linked list, this includes wrap calculations */
	textentry * gtk_xtext_nth(GtkXText *xtext, int line, int &subline)
	{
		int lines = 0;
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		textentry * ent = priv->buffer->impl->text_first;

		/* -- optimization -- try to make a short-cut using the pagetop ent */
		if (priv->buffer->impl->pagetop_ent)
		{
			if (line == priv->buffer->pagetop_line)
			{
				subline = priv->buffer->pagetop_subline;
				return priv->buffer->impl->pagetop_ent;
			}
			if (line > priv->buffer->pagetop_line)
			{
				/* lets start from the pagetop instead of the absolute beginning */
				ent = priv->buffer->impl->pagetop_ent;
				lines = priv->buffer->pagetop_line - priv->buffer->pagetop_subline;
			}
			else if (line > priv->buffer->pagetop_line - line)
			{
				/* move backwards from pagetop */
				ent = priv->buffer->impl->pagetop_ent;
				lines = priv->buffer->pagetop_line - priv->buffer->pagetop_subline;
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
		if (!gtk_widget_get_realized(GTK_WIDGET(xtext)))
		{
			return;
		}
		cairo_stack cr_stack{ cr };
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		if (priv->buffer->indent < MARGIN)
			priv->buffer->indent = MARGIN;	  /* 2 pixels is our left margin */

		GtkWidget * widget = GTK_WIDGET(xtext);
		GtkAllocation allocation;
		gtk_widget_get_allocation(widget, &allocation);

		if (allocation.width < 34 ||
			allocation.height < priv->fontsize ||
			allocation.width < priv->buffer->indent + 32)
		{
			return;
		}
		const auto adj_value = gtk_adjustment_get_value(priv->adj);
		int startline = static_cast<int>(adj_value);
		priv->pixel_offset = (adj_value - startline) * priv->fontsize;

		int subline = 0;
		int line = 0;
		auto ent = priv->buffer->impl->text_first;

		if (startline > 0)
			ent = gtk_xtext_nth(xtext, startline, subline);

		priv->buffer->impl->pagetop_ent = ent;
		priv->buffer->pagetop_subline = subline;
		priv->buffer->pagetop_line = startline;

		if (priv->buffer->num_lines <= gtk_adjustment_get_page_size(priv->adj))
			dontscroll(priv->buffer);

		int pos = adj_value * priv->fontsize;
		auto overlap = priv->buffer->last_pixel_pos - pos;
		priv->buffer->last_pixel_pos = pos;

#ifndef __APPLE__
		if (!priv->pixmap && std::abs(overlap) < allocation.height)
		{
			GdkRectangle area;
			cairo_stack cr_stack{ cr };
			if (overlap < 1)	/* DOWN */
			{
				gdk_cairo_set_source_pixmap(cr, priv->pixmap, 0.0, 0.0);
				auto remainder = ((allocation.height - priv->font->descent) % priv->fontsize) +
					priv->font->descent;
				area.y = (allocation.height + overlap) - remainder;
				area.height = remainder - overlap;
			}
			else
			{
				gdk_cairo_set_source_color(cr, &priv->palette[XTEXT_FG]);
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
		auto width = allocation.width;
		auto height = allocation.height;
		width -= MARGIN;
		auto lines_max = ((height + priv->pixel_offset) / priv->fontsize) + 1;

		while (ent)
		{
			gtk_xtext_reset(xtext, false, true);
			line += gtk_xtext_render_line(xtext, cr, ent, line, lines_max,
				subline, width);
			subline = 0;

			if (line >= lines_max)
				break;

			ent = ent->next;
		}

		line = (priv->fontsize * line) - priv->pixel_offset;

		/* draw the separator line */
		gtk_xtext_draw_sep(xtext, cr, allocation.height);
	}
} // end anonymous namespace

void
gtk_xtext_refresh(GtkXText * xtext)
{
	if (gtk_widget_get_realized(GTK_WIDGET(xtext)))
	{
		gtk_widget_queue_draw(GTK_WIDGET(xtext));
	}
}

namespace{

	bool gtk_xtext_kill_ent(xtext_buffer *buffer, textentry *ent)
	{
		/* Set visible to true if this is the current buffer */
		/* and this ent shows up on the screen now */
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(buffer->xtext));
		bool visible = priv->buffer == buffer &&
			gtk_xtext_check_ent_visibility(buffer->xtext, ent, 0);

		if (ent == buffer->impl->pagetop_ent)
			buffer->impl->pagetop_ent = nullptr;

		if (ent == buffer->impl->last_ent_start)
		{
			buffer->impl->last_ent_start = ent->next;
			buffer->last_offset_start = 0;
		}

		if (ent == buffer->impl->last_ent_end)
		{
			buffer->impl->last_ent_start = nullptr;
			buffer->impl->last_ent_end = nullptr;
		}

		if (buffer->impl->marker_pos == ent)
		{
			/* Allow for "Marker line reset because exceeded scrollback limit. to appear. */
			buffer->impl->marker_pos = ent->next;
			buffer->impl->marker_state = MARKER_RESET_BY_KILL;
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
		auto ent = buffer->impl->text_first;
		if (!ent)
			return;
		if (buffer->impl->entries.empty())
		{
			throw std::runtime_error("attempt to remove without anything in the store!");
		}
		buffer->num_lines -= ent->sublines.size();
		buffer->pagetop_line -= ent->sublines.size();
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(buffer->xtext));
		buffer->last_pixel_pos -= (ent->sublines.size() * priv->fontsize);
		buffer->impl->text_first = ent->next;
		if (buffer->impl->text_first)
			buffer->impl->text_first->prev = nullptr;
		else
			buffer->impl->text_last = nullptr;

		buffer->old_value -= ent->sublines.size();
		if (priv->buffer ==
		    buffer) /* is it the current buffer? */
		{
			gtk_adjustment_set_value(
			    priv->adj,
			    gtk_adjustment_get_value(priv->adj) -
				ent->sublines.size());
			priv->select_start_adj -= ent->sublines.size();
		}

		if (gtk_xtext_kill_ent(buffer, ent))
		{
			if (!priv->add_io_tag)
			{
				/* remove scrolling events */
				if (priv->io_tag)
				{
					g_source_remove(priv->io_tag);
					priv->io_tag = 0;
				}
				priv->force_render = true;
				priv->add_io_tag = g_timeout_add(REFRESH_TIMEOUT * 2,
					(GSourceFunc)
					gtk_xtext_render_page_timeout,
					buffer->xtext);
			}
		}
		buffer->impl->entries.pop_front();
	}

	static void
		gtk_xtext_remove_bottom(xtext_buffer *buffer)
	{
		textentry *ent;

		ent = buffer->impl->text_last;
		if (!ent)
			return;
		if (buffer->impl->entries.empty())
		{
			throw std::runtime_error("attempt to remove without anything in the store!");
		}
		buffer->num_lines -= ent->sublines.size();
		buffer->impl->text_last = ent->prev;
		if (buffer->impl->text_last)
			buffer->impl->text_last->next = nullptr;
		else
			buffer->impl->text_first = nullptr;

		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(buffer->xtext));
		if (gtk_xtext_kill_ent(buffer, ent))
		{
			if (!priv->add_io_tag)
			{
				/* remove scrolling events */
				if (priv->io_tag)
				{
					g_source_remove(priv->io_tag);
					priv->io_tag = 0;
				}
				priv->force_render = true;
				priv->add_io_tag = g_timeout_add(REFRESH_TIMEOUT * 2,
					(GSourceFunc)
					gtk_xtext_render_page_timeout,
					buffer->xtext);
			}
		}
		buffer->impl->entries.pop_back();
	}

} // end anonymous namespace

/* If lines=0 => clear all */

void
gtk_xtext_clear(xtext_buffer *buf, int lines)
{
	bool marker_reset = false;
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(buf->xtext));
	if (lines != 0)
	{
		if (lines < 0)
		{
			/* delete lines from bottom */
			lines *= -1;
			while (lines)
			{
				if (buf->impl->text_last == buf->impl->marker_pos)
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
				if (buf->impl->text_first == buf->impl->marker_pos)
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
		if (priv->auto_indent)
			buf->indent = MARGIN;
		buf->scrollbar_down = true;
		buf->impl->last_ent_start = nullptr;
		buf->impl->last_ent_end = nullptr;
		buf->impl->marker_pos = nullptr;
		if (buf->impl->text_first)
			marker_reset = true;
		dontscroll(buf);
		buf->impl->entries.clear();
		buf->impl->text_first = nullptr;
		/*while (buf->text_first)
		{
			auto next = buf->text_first->next;
			delete buf->text_first;
			buf->text_first = next;
		}*/
		buf->impl->text_last = nullptr;
	}

	if (priv->buffer == buf)
	{
		gtk_xtext_calc_lines(buf, true);
		gtk_xtext_refresh(buf->xtext);
	}
	else
	{
		gtk_xtext_calc_lines(buf, false);
	}

	if (marker_reset)
		buf->impl->marker_state = MARKER_RESET_BY_CLEAR;
}

namespace{
	static bool
		gtk_xtext_check_ent_visibility(GtkXText * xtext, textentry *find_ent, int add)
	{
		if (!find_ent)
		{
			return false;
		}

		auto height = gdk_window_get_height(gtk_widget_get_window(GTK_WIDGET(xtext)));
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		xtext_buffer *buf = priv->buffer;
		auto ent = buf->impl->pagetop_ent;
		/* If top line not completely displayed return false */
		if (ent == find_ent && buf->pagetop_subline > 0)
		{
			return false;
		}
		/* Loop through line positions looking for find_ent */
		auto lines = ((height + priv->pixel_offset) / priv->fontsize) + buf->pagetop_subline + add;
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
} // end anonymous namespace

void
gtk_xtext_check_marker_visibility(GtkXText * xtext)
{
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
	if (gtk_xtext_check_ent_visibility(xtext, priv->buffer->impl->marker_pos, 1))
		priv->buffer->marker_seen = true;
}

namespace{

	static void gtk_xtext_unstrip_color(gint start, gint end, const std::vector<offlen_t>& slp, GList **gl, gint maxo)
	{
		gint off1, off2, curlen;
		offsets_t marks;

		off1 = 0;
		curlen = 0;
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

		off2 = off1;
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
		std::vector<offlen_t> slp;
		/* text string to be searched */
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(buf->xtext));
		auto str = (gchar*)gtk_xtext_strip_color(ent.str, priv->scratch_buffer,
			&lstr, &slp, !priv->ignore_hidden);
		GList *gl = nullptr;
		/* Regular-expression matching --- */
		if (buf->search_flags & regexp)
		{
			GMatchInfo *gmi;
			gint start, end;

			if (buf->search_re == nullptr)
			{
				return gl;
			}
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
			gint lhay, off, len;
			gint match = buf->search_flags & case_match;

			glib_string hay(match ? g_strdup(str) : g_utf8_casefold(str, lstr));
			lhay = std::strlen(hay.get());
			off = 0;

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
		if (buf->impl->pagetop_ent == ent)
		{
			buf->impl->pagetop_ent = nullptr;
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

} // end anonymous namespace

#define BACKWARD (flags & backward)
#define FIRSTLAST(lp)  (BACKWARD? g_list_last(lp): g_list_first(lp))
#define NEXTPREVIOUS(lp) (BACKWARD? g_list_previous(lp): g_list_next(lp))
textentry *
gtk_xtext_search(GtkXText * xtext, const gchar *text, gtk_xtext_search_flags flags, GError **perr)
{
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
	xtext_buffer *buf = priv->buffer;

	if (buf->impl->text_first == nullptr)
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
			ent = static_cast<textentry *>(gl ? gl->data : buf->impl->text_first);
			for (; ent; ent = ent->next)
			{
				auto gl = gtk_xtext_search_textentry(buf, *ent);
				gtk_xtext_search_textentry_add(buf, ent, gl, false);
			}
		}
		buf->search_flags = flags;
		ent = buf->impl->pagetop_ent;
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
			for (ent = buf->impl->text_first; ent; ent = ent->next)
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
		GtkAdjustment *adj = priv->adj;
		gdouble value;

		buf->impl->pagetop_ent = nullptr;
		for (value = 0.0, ent = buf->impl->text_first;
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

	return buf->hintsearch;
}
#undef BACKWARD
#undef FIRSTLAST
#undef NEXTPREVIOUS

namespace {

	static int
		gtk_xtext_render_page_timeout(GtkXText * xtext)
	{
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
		GtkAdjustment *adj = priv->adj;

		priv->add_io_tag = 0;

		/* less than a complete page? */
		const auto page_size = gtk_adjustment_get_page_size(adj);
		if (priv->buffer->num_lines <= page_size)
		{
			priv->buffer->old_value = 0.0;
			g_signal_handler_block(priv->adj, priv->vc_signal_tag);
			gtk_adjustment_set_value(adj, 0.0);
			g_signal_handler_unblock(priv->adj, priv->vc_signal_tag);
		}
		else if (priv->buffer->scrollbar_down)
		{
			g_signal_handler_block(priv->adj, priv->vc_signal_tag);
			gtk_xtext_adjustment_set(priv->buffer, false);
			gtk_adjustment_set_value(adj, gtk_adjustment_get_upper(adj) - page_size);
			g_signal_handler_unblock(priv->adj, priv->vc_signal_tag);
			priv->buffer->old_value = gtk_adjustment_get_value(adj);
		}
		else
		{
			gtk_xtext_adjustment_set(priv->buffer, true);
			if (priv->force_render)
			{
				priv->force_render = false;
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
		ent.str_width = gtk_xtext_text_width_ent(buf->xtext, ent);
		ent.mark_start = -1;
		ent.mark_end = -1;
		ent.next = nullptr;
		ent.marks = nullptr;

		if (ent.indent < MARGIN)
			ent.indent = MARGIN;	  /* 2 pixels is the left margin */

		buf->impl->entries.emplace_back(std::move(ent));
		textentry * ent_ptr = &buf->impl->entries.back();
		/* append to our linked list */
		if (buf->impl->text_last)
			buf->impl->text_last->next = ent_ptr;
		else
			buf->impl->text_first = ent_ptr;
		ent_ptr->prev = buf->impl->text_last;
		buf->impl->text_last = ent_ptr;

		buf->num_lines += gtk_xtext_lines_taken(buf, *ent_ptr);
		auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(buf->xtext));
		if ((buf->impl->marker_pos == nullptr || buf->marker_seen) && (priv->buffer != buf ||
			!gtk_window_has_toplevel_focus(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(buf->xtext))))))
		{
			buf->impl->marker_pos = ent_ptr;
			buf->impl->marker_state = MARKER_IS_SET;
			dontscroll(buf); /* force scrolling off */
			buf->marker_seen = false;
		}

		if (priv->max_lines > 2 && priv->max_lines < buf->num_lines)
		{
			gtk_xtext_remove_top(buf);
		}
		const auto page_size = gtk_adjustment_get_page_size(priv->adj);
		if (priv->buffer == buf)
		{
			/* this could be improved */
			if ((buf->num_lines - 1) <= page_size)
				dontscroll(buf);

			if (!priv->add_io_tag)
			{
				/* remove scrolling events */
				if (priv->io_tag)
				{
					g_source_remove(priv->io_tag);
					priv->io_tag = 0;
				}
				priv->add_io_tag = g_timeout_add(REFRESH_TIMEOUT * 2,
					(GSourceFunc)
					gtk_xtext_render_page_timeout,
					buf->xtext);
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
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(buf->xtext));
	if (right_text.size() >= sizeof(priv->scratch_buffer))
		right_text = right_text.substr(0, sizeof(priv->scratch_buffer) - 1);

	if (right_text.back() == '\n')
		right_text.remove_suffix(1);

	textentry ent;
	ent.str.reserve(left_text.length() + right_text.length() + 2);
	ent.str.append(left_text.cbegin(), left_text.cend());
	ent.str.push_back(' ');
	ent.str.append(right_text.cbegin(), right_text.cend());

	auto left_width =
	    gtk_xtext_text_width(buf->xtext, left_text);

	ent.left_len = left_text.length();
	ent.indent = (buf->indent - left_width) - priv->space_width;

	auto space = buf->time_stamp ? priv->stamp_width : 0;

	/* do we need to auto adjust the separator position? */
	if (priv->auto_indent && ent.indent < MARGIN + space)
	{
		auto tempindent =
		    MARGIN + space + priv->space_width + left_width;

		if (tempindent > buf->indent)
			buf->indent = tempindent;

		if (buf->indent > priv->max_auto_indent)
			buf->indent = priv->max_auto_indent;

		gtk_xtext_fix_indent(buf);
		gtk_xtext_recalc_widths(buf, false);

		ent.indent =
		    (buf->indent - left_width) - priv->space_width;
		priv->force_render = true;
	}

	gtk_xtext_append_entry(buf, std::move(ent), stamp);
}

void gtk_xtext_append(xtext_buffer *buf, boost::string_ref text, time_t stamp)
{
	if (text.back() == '\n')
		text.remove_suffix(text.size() - 1);

	if (text.size() >= sizeof(GtkXTextPrivate::scratch_buffer))
		text.remove_suffix(sizeof(GtkXTextPrivate::scratch_buffer) - 1);

	textentry ent;
	if (!text.empty())
	{
		ent.str = ustring{text.cbegin(), text.cend()};
	}
	ent.indent = 0;
	ent.left_len = -1;

	gtk_xtext_append_entry(buf, std::move(ent), stamp);
}

bool gtk_xtext_is_empty(const xtext_buffer &buf)
{
	return buf.impl->text_first == nullptr;
}

int gtk_xtext_lastlog(xtext_buffer *out, xtext_buffer *search_area)
{
	int matches = 0;
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(search_area->xtext));
	for (const auto &ent : search_area->impl->entries)
	{
		auto gl = gtk_xtext_search_textentry(out, ent);
		if (!gl)
		{
			continue;
		}
		++matches;
		/* copy the text over */
		if (priv->auto_indent)
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

		out->impl->text_last->stamp = ent.stamp;
		gtk_xtext_search_textentry_add(out, out->impl->text_last, gl,
					       true);
	}
	out->search_found = g_list_reverse(out->search_found);

	return matches;
}

void
gtk_xtext_set_indent(GtkXText *xtext, gboolean indent)
{
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
	priv->auto_indent = !!indent;
}

void
gtk_xtext_set_max_indent(GtkXText *xtext, int max_auto_indent)
{
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
	priv->max_auto_indent = max_auto_indent;
}

void
gtk_xtext_set_max_lines(GtkXText *xtext, int max_lines)
{
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
	priv->max_lines = max_lines;
}

void
gtk_xtext_set_show_marker(GtkXText *xtext, gboolean show_marker)
{
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
	priv->marker = !!show_marker;
}

void
gtk_xtext_set_show_separator(GtkXText *xtext, gboolean show_separator)
{
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
	priv->separator = !!show_separator;
}

void
gtk_xtext_set_thin_separator(GtkXText *xtext, gboolean thin_separator)
{
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
	priv->thinline = !!thin_separator;
}

void
gtk_xtext_set_time_stamp(xtext_buffer *buf, gboolean time_stamp)
{
	buf->time_stamp = !!time_stamp;
}

void
gtk_xtext_set_urlcheck_function(GtkXText *xtext, int(*urlcheck_function) (GtkWidget *, const char *))
{
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
	priv->urlcheck_function = urlcheck_function;
}

void
gtk_xtext_set_wordwrap(GtkXText *xtext, gboolean wordwrap)
{
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
	priv->wordwrap = !!wordwrap;
}

void
gtk_xtext_set_marker_last(session *sess)
{
	xtext_buffer *buf = static_cast<xtext_buffer *>(sess->res->buffer);

	buf->impl->marker_pos = buf->impl->text_last;
	buf->impl->marker_state = MARKER_IS_SET;
}

void
gtk_xtext_reset_marker_pos(GtkXText *xtext)
{
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
	if (priv->buffer->impl->marker_pos)
	{
		priv->buffer->impl->marker_pos = nullptr;
		dontscroll(priv->buffer); /* force scrolling off */
		gtk_widget_queue_draw(GTK_WIDGET(xtext));
		priv->buffer->impl->marker_state = MARKER_RESET_MANUALLY;
	}
}

int
gtk_xtext_moveto_marker_pos(GtkXText *xtext)
{
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
	xtext_buffer *buf = priv->buffer;
	if (buf->impl->marker_pos == nullptr)
		return buf->impl->marker_state;

	if (gtk_xtext_check_ent_visibility(xtext, buf->impl->marker_pos, 1) == false)
	{
		GtkAdjustment *adj = priv->adj;
		std::size_t ivalue = 0;
		for(const auto & ent : buf->impl->entries)
		{
			if (&ent == buf->impl->marker_pos)
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
	if (buf->impl->marker_pos == buf->impl->text_first &&
		buf->impl->marker_state == MARKER_RESET_BY_KILL)
		return MARKER_RESET_BY_KILL;
	else
		return MARKER_IS_SET;
}

void
gtk_xtext_buffer_show(GtkXText *xtext, xtext_buffer *buf, bool render)
{
	buf->xtext = xtext;
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext));
	if (priv->buffer == buf)
		return;

	/*printf("text_buffer_show: xtext=%p buffer=%p\n", xtext, buf);*/

	if (priv->add_io_tag)
	{
		g_source_remove(priv->add_io_tag);
		priv->add_io_tag = 0;
	}

	if (priv->io_tag)
	{
		g_source_remove(priv->io_tag);
		priv->io_tag = 0;
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
	priv->buffer = buf;
	dontscroll(buf);	/* force scrolling off */
	auto value = buf->old_value;
	auto upper = static_cast<gdouble>(buf->num_lines);
	const auto page_size = gtk_adjustment_get_page_size(priv->adj);
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

	gtk_adjustment_set_upper(priv->adj, upper);
	gtk_adjustment_set_value(priv->adj, value);

	if (render)
	{
		/* did the window change size since this buffer was last shown? */
		if (buf->window_width != w)
		{
			buf->window_width = w;
			gtk_xtext_calc_lines(buf, false);
			if (buf->scrollbar_down)
				gtk_adjustment_set_value(priv->adj, gtk_adjustment_get_upper(priv->adj) -
				gtk_adjustment_get_page_size(priv->adj));
		}
		else if (buf->window_height != h)
		{
			buf->window_height = h;
			buf->impl->pagetop_ent = nullptr;
			if (buf->scrollbar_down)
				gtk_adjustment_set_value(priv->adj, gtk_adjustment_get_upper(priv->adj));
			gtk_xtext_adjustment_set(buf, false);
		}

		gtk_widget_queue_draw(widget);
		gtk_adjustment_changed(priv->adj);
	}
}

xtext_buffer *
gtk_xtext_buffer_new(GtkXText *xtext)
{
	xtext_buffer *buf = new xtext_buffer(xtext);
	dontscroll(buf);

	return buf;
}

xtext_buffer::xtext_buffer(GtkXText* parent)
	:impl(new xtext_impl),
	xtext(parent),     /* attached to this widget */
	old_value(-1.0), /* last known adj->value */

	last_offset_start(), last_offset_end(),

	last_pixel_pos(),

	pagetop_line(), pagetop_subline(),

	num_lines(), indent(static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(xtext))->space_width * 2), /* position of separator (pixels) from left */

	window_width(), /* window size when last rendered. */
	window_height(),

	time_stamp(), scrollbar_down(true), needs_recalc(), marker_seen(),

	search_found(), /* list of textentries where search found strings */
	search_flags(), /* match, bwd, highlight */
	cursearch(),    /* GList whose 'data' pts to current textentry */
	curmark(),      /* current item in ent->marks */
	curdata(),      /* current offset info, from *curmark */
	search_re(),    /* Compiled regular expression */
	hintsearch()    /* textentry found for last search */
{
}

xtext_buffer::~xtext_buffer() NOEXCEPT
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
	auto priv = static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(buf->xtext));
	if (priv->buffer == buf)
		priv->buffer = priv->orig_buffer;

	if (priv->selection_buffer == buf)
		priv->selection_buffer = nullptr;
}

xtext_buffer* xtext_get_current_buffer(GtkXText* self) {
	return static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(self))->buffer;
}

GtkAdjustment* xtext_get_adjustments(GtkXText*self) {
	return static_cast<GtkXTextPrivate*>(gtk_xtext_get_instance_private(self))->adj;
}

static void gtk_xtext_class_init(GtkXTextClass * text_class)
{
	GtkObjectClass *object_class;
	GtkWidgetClass *widget_class;
	GtkXTextClass *xtext_class;

	object_class = (GtkObjectClass *)text_class;
	widget_class = (GtkWidgetClass *)text_class;
	xtext_class = (GtkXTextClass *)text_class;

	parent_class = static_cast<GtkWidgetClass *>(g_type_class_peek(gtk_widget_get_type()));

	xtext_signals[WORD_CLICK] =
		g_signal_new("word_click",
			G_TYPE_FROM_CLASS(object_class),
			G_SIGNAL_RUN_FIRST | G_SIGNAL_ACTION,
			G_STRUCT_OFFSET(GtkXTextClass, word_click),
			nullptr, nullptr,
			_hexchat_marshal_VOID__POINTER_POINTER,
			G_TYPE_NONE,
			2, G_TYPE_POINTER, G_TYPE_POINTER);
	xtext_signals[SET_SCROLL_ADJUSTMENTS] =
		g_signal_new("set_scroll_adjustments",
			G_OBJECT_CLASS_TYPE(object_class),
			G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			G_STRUCT_OFFSET(GtkXTextClass, set_scroll_adjustments),
			nullptr, nullptr,
			_hexchat_marshal_VOID__OBJECT_OBJECT,
			G_TYPE_NONE,
			2, GTK_TYPE_ADJUSTMENT, GTK_TYPE_ADJUSTMENT);

	object_class->destroy = gtk_xtext_destroy;

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

	xtext_class->word_click = nullptr;
	xtext_class->set_scroll_adjustments = gtk_xtext_scroll_adjustments;
}