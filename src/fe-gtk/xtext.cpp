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

#define GDK_MULTIHEAD_SAFE
enum{ MARGIN = 2 };					/* dont touch. */
#define REFRESH_TIMEOUT 20
#define WORDWRAP_LIMIT 24

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <iterator>
#include <locale>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>


#include <boost/utility/string_ref.hpp>

#include "../../config.h"
#include "../common/hexchat.hpp"
#include "../common/fe.hpp"
#include "../common/util.hpp"
#include "../common/hexchatc.hpp"
#include "../common/url.hpp"
#include "../common/marshal.h"
#include "../common/session.hpp"
#include "fe-gtk.hpp"
#include "xtext.hpp"
#include "fkeys.hpp"
#include "gtk_helpers.hpp"

#define charlen(str) g_utf8_skip[*(guchar *)(str)]

#ifdef WIN32
#include <io.h>
#else
#include <unistd.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif
#endif

char *nocasestrstr(const char *text, const char *tofind);	/* util.c */
std::string xtext_get_stamp_str(time_t);

typedef std::basic_string<unsigned char> ustring;
typedef boost::basic_string_ref<unsigned char> ustring_ref;

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
		:next(),
		prev(),
		stamp(),
		str_width(),
		mark_start(),
		mark_end(),
		indent(),
		left_len(),
		tag(),
		marks(){}

	textentry *next;
	textentry *prev;
	ustring str;
	time_t stamp;
	gint16 str_width;
	gint16 mark_start;
	gint16 mark_end;
	gint16 indent;
	gint16 left_len;
	std::vector<offlen_t> slp;
	std::vector<int> sublines;
	guchar tag;
	GList *marks;	/* List of found strings */
};

struct xtext_impl
{
	textentry *text_first;
	textentry *text_last;

	textentry *last_ent_start;	  /* this basically describes the last rendered */
	textentry *last_ent_end;	  /* selection. */
	textentry *pagetop_ent;			/* what's at xtext->adj->value */

	textentry *marker_pos;
	marker_reset_reason marker_state;
	std::deque<textentry> entries;

	xtext_impl()
		:text_first(), text_last(),

		last_ent_start(), /* this basically describes the last rendered */
		last_ent_end(),   /* selection. */
		pagetop_ent(), /* what's at xtext->adj->value */
		marker_pos(), marker_state()
	{}
};

namespace
{

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


	static void gtk_xtext_render_page(GtkXText * xtext);
	static void gtk_xtext_calc_lines(xtext_buffer *buf, bool);
	static std::string gtk_xtext_selection_get_text(GtkXText *xtext);
	static textentry *gtk_xtext_nth(GtkXText *xtext, int line, int *subline);
	static void gtk_xtext_adjustment_changed(GtkAdjustment * adj,
		GtkXText * xtext);
	static void gtk_xtext_scroll_adjustments(GtkXText *xtext, GtkAdjustment *hadj,
		GtkAdjustment *vadj);
	static int gtk_xtext_render_ents(GtkXText * xtext, textentry *, textentry *);
	static void gtk_xtext_recalc_widths(xtext_buffer *buf, bool);
	static void gtk_xtext_fix_indent(xtext_buffer *buf);
	static int gtk_xtext_find_subline(GtkXText *xtext, textentry *ent, int line);
	/* static char *gtk_xtext_conv_color (unsigned char *text, int len, int *newlen); */
	static unsigned char *
		gtk_xtext_strip_color(const ustring_ref & text, unsigned char *outbuf,
		int *newlen, std::vector<offlen_t> * slp, int strip_hidden);
	static bool gtk_xtext_check_ent_visibility(GtkXText * xtext, textentry *find_ent, int add);
	static int gtk_xtext_render_page_timeout(GtkXText * xtext);
	static int gtk_xtext_search_offset(xtext_buffer *buf, textentry *ent, unsigned int off);
	static GList * gtk_xtext_search_textentry(xtext_buffer *, const textentry &);
	static void gtk_xtext_search_textentry_add(xtext_buffer *, textentry *, GList *, bool);
	static void gtk_xtext_search_textentry_del(xtext_buffer *, textentry *);
	static void gtk_xtext_search_textentry_fini(gpointer, gpointer);
	static void gtk_xtext_search_fini(xtext_buffer *);
	static bool gtk_xtext_search_init(xtext_buffer *buf, const gchar *text, gtk_xtext_search_flags flags, GError **perr);
	static char * gtk_xtext_get_word(GtkXText * xtext, int x, int y, textentry ** ret_ent, int *ret_off, int *ret_len, std::vector<offlen_t> *slp);

	/* Avoid warning messages for this unused function */
#if 0
	/* gives width of a 8bit string - with no mIRC codes in it */

	static int
		gtk_xtext_text_width_8bit(GtkXText *xtext, unsigned char *str, int len)
	{
		int width = 0;

		while (len)
		{
			width += xtext->fontwidth[*str];
			str++;
			len--;
		}

		return width;
	}
#endif

#define xtext_draw_bg(xt,x,y,w,h) gdk_draw_rectangle(xt->draw_buf, xt->bgc, 1, x, y, w, h);

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
	CUSTOM_PTR(PangoFontMetrics, pango_font_metrics_unref)

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
			int mbl = charlen(itr);
			int deltaw;
			if (*itr < 128)
				deltaw = fontwidths[emphasis][*itr];
			else
			{
				pango_layout_set_text(xtext->layout, reinterpret_cast<const char*>(itr), mbl);
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

	/* simplified version of gdk_draw_layout_line_with_colors() */

	static void
		xtext_draw_layout_line(GdkDrawable      *drawable,
		GdkGC            *gc,
		gint              x,
		gint              y,
		PangoLayoutLine  *line)
	{
		GSList *tmp_list = line->runs;
		PangoRectangle logical_rect;
		gint x_off = 0;

		while (tmp_list)
		{
			PangoLayoutRun *run = static_cast<PangoLayoutRun*>(tmp_list->data);

			pango_glyph_string_extents(run->glyphs, run->item->analysis.font,
				nullptr, &logical_rect);

			gdk_draw_glyphs(drawable, gc, run->item->analysis.font,
				x + x_off / PANGO_SCALE, y, run->glyphs);

			x_off += logical_rect.width;
			tmp_list = tmp_list->next;
		}
	}

	static void backend_draw_text_emph(GtkXText *xtext, bool dofill, GdkGC *gc, int x, int y,
		const char *str, int len, int str_width, int emphasis)
	{
		pango_layout_set_attributes(xtext->layout, attr_lists[emphasis]);
		pango_layout_set_text(xtext->layout, str, len);

		if (dofill)
		{
			GdkGCValues val;
			gdk_gc_get_values(gc, &val);
			GdkColor col;
			col.pixel = val.background.pixel;
			gdk_gc_set_foreground(gc, &col);
			gdk_draw_rectangle(xtext->draw_buf, gc, 1, x, y -
				xtext->font->ascent, str_width, xtext->fontsize);
			col.pixel = val.foreground.pixel;
			gdk_gc_set_foreground(gc, &col);
		}

		auto line = static_cast<PangoLayoutLine*>(pango_layout_get_lines(xtext->layout)->data);

		xtext_draw_layout_line(xtext->draw_buf, gc, x, y, line);
	}

	static void
		xtext_set_fg(GtkXText *xtext, GdkGC *gc, int index)
	{
		gdk_gc_set_foreground(gc, &xtext->palette[index]);
	}

	static void
		xtext_set_bg(GtkXText *xtext, GdkGC *gc, int index)
	{
		gdk_gc_set_background(gc, &xtext->palette[index]);
	}

	static void
		gtk_xtext_init(GtkXText * xtext)
	{
		xtext->pixmap = nullptr;
		xtext->io_tag = 0;
		xtext->add_io_tag = 0;
		xtext->scroll_tag = 0;
		xtext->max_lines = 0;
		xtext->col_back = XTEXT_BG;
		xtext->col_fore = XTEXT_FG;
		xtext->nc = 0;
		xtext->pixel_offset = 0;
		xtext->underline = false;
		xtext->hidden = false;
		xtext->font = nullptr;
		xtext->layout = nullptr;
		xtext->jump_out_offset = 0;
		xtext->jump_in_offset = 0;
		xtext->ts_x = 0;
		xtext->ts_y = 0;
		xtext->clip_x = 0;
		xtext->clip_x2 = 1000000;
		xtext->clip_y = 0;
		xtext->clip_y2 = 1000000;
		xtext->urlcheck_function = nullptr;
		xtext->color_paste = false;
		xtext->skip_border_fills = false;
		xtext->skip_stamp = false;
		xtext->render_hilights_only = false;
		xtext->un_hilight = false;
		xtext->dont_render = false;
		xtext->dont_render2 = false;
		gtk_xtext_scroll_adjustments(xtext, nullptr, nullptr);

		{
			static const GtkTargetEntry targets[] = {
				{ "UTF8_STRING", 0, TARGET_UTF8_STRING },
				{ "STRING", 0, TARGET_STRING },
				{ "TEXT", 0, TARGET_TEXT },
				{ "COMPOUND_TEXT", 0, TARGET_COMPOUND_TEXT }
			};
			static const gint n_targets = sizeof(targets) / sizeof(targets[0]);

			gtk_selection_add_targets(GTK_WIDGET(xtext), GDK_SELECTION_PRIMARY,
				targets, n_targets);
		}
	}

	static void
		gtk_xtext_adjustment_set(xtext_buffer *buf, bool fire_signal)
	{
		GtkAdjustment *adj = buf->xtext->adj;

		if (buf->xtext->buffer == buf)
		{
			adj->lower = 0;
			adj->upper = buf->num_lines;

			if (adj->upper == 0)
				adj->upper = 1;

			adj->page_size = GTK_WIDGET(buf->xtext)->allocation.height /
				buf->xtext->fontsize;
			adj->page_increment = adj->page_size;

			if (adj->value > adj->upper - adj->page_size)
			{
				buf->scrollbar_down = true;
				adj->value = adj->upper - adj->page_size;
			}

			if (adj->value < 0)
				adj->value = 0;

			if (fire_signal)
				gtk_adjustment_changed(adj);
		}
	}

	static gint
		gtk_xtext_adjustment_timeout(GtkXText * xtext)
	{
		gtk_xtext_render_page(xtext);
		xtext->io_tag = 0;
		return 0;
	}

	static void
		gtk_xtext_adjustment_changed(GtkAdjustment * adj, GtkXText * xtext)
	{
		if (!gtk_widget_get_realized(GTK_WIDGET(xtext)))
			return;

		if (xtext->buffer->old_value != xtext->adj->value)
		{
			if (xtext->adj->value >= xtext->adj->upper - xtext->adj->page_size)
				xtext->buffer->scrollbar_down = true;
			else
				xtext->buffer->scrollbar_down = false;

			if (xtext->adj->value + 1.0 == xtext->buffer->old_value ||
				xtext->adj->value - 1.0 == xtext->buffer->old_value)	/* clicked an arrow? */
			{
				if (xtext->io_tag)
				{
					g_source_remove(xtext->io_tag);
					xtext->io_tag = 0;
				}
				gtk_xtext_render_page(xtext);
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
		xtext->buffer->old_value = adj->value;
	}
} // end anonymous namespace

GtkWidget *
gtk_xtext_new(GdkColor palette[], bool separator)
{
	GtkXText *xtext;

	xtext = static_cast<GtkXText*>(g_object_new(gtk_xtext_get_type(), nullptr));
	xtext->separator = separator;
	xtext->wordwrap = true;
	xtext->buffer = gtk_xtext_buffer_new(xtext);
	xtext->orig_buffer = xtext->buffer;

	gtk_widget_set_double_buffered(GTK_WIDGET(xtext), false);
	gtk_xtext_set_palette(xtext, palette);

	return GTK_WIDGET(xtext);
}

namespace {
	static void
		gtk_xtext_destroy(GtkObject * object)
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

		if (xtext->bgc)
		{
			g_object_unref(xtext->bgc);
			xtext->bgc = nullptr;
		}

		if (xtext->fgc)
		{
			g_object_unref(xtext->fgc);
			xtext->fgc = nullptr;
		}

		if (xtext->light_gc)
		{
			g_object_unref(xtext->light_gc);
			xtext->light_gc = nullptr;
		}

		if (xtext->dark_gc)
		{
			g_object_unref(xtext->dark_gc);
			xtext->dark_gc = nullptr;
		}

		if (xtext->thin_gc)
		{
			g_object_unref(xtext->thin_gc);
			xtext->thin_gc = nullptr;
		}

		if (xtext->marker_gc)
		{
			g_object_unref(xtext->marker_gc);
			xtext->marker_gc = nullptr;
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

		if (GTK_OBJECT_CLASS(parent_class)->destroy)
			(*GTK_OBJECT_CLASS(parent_class)->destroy) (object);
	}

	static void
		gtk_xtext_unrealize(GtkWidget * widget)
	{
		backend_deinit(GTK_XTEXT(widget));

		/* if there are still events in the queue, this'll avoid segfault */
		gdk_window_set_user_data(widget->window, nullptr);

		if (parent_class->unrealize)
			(*GTK_WIDGET_CLASS(parent_class)->unrealize) (widget);
	}

	static void
		gtk_xtext_realize(GtkWidget * widget)
	{
		GtkXText *xtext;
		GdkWindowAttr attributes;
		GdkGCValues val;
		GdkColor col;
		GdkColormap *cmap;

		gtk_widget_set_realized(widget, true);
		xtext = GTK_XTEXT(widget);

		attributes.x = widget->allocation.x;
		attributes.y = widget->allocation.y;
		attributes.width = widget->allocation.width;
		attributes.height = widget->allocation.height;
		attributes.wclass = GDK_INPUT_OUTPUT;
		attributes.window_type = GDK_WINDOW_CHILD;
		attributes.event_mask = gtk_widget_get_events(widget) |
			GDK_EXPOSURE_MASK | GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK
			| GDK_POINTER_MOTION_MASK | GDK_LEAVE_NOTIFY_MASK;

		cmap = gtk_widget_get_colormap(widget);
		attributes.colormap = cmap;
		attributes.visual = gtk_widget_get_visual(widget);

		widget->window = gdk_window_new(widget->parent->window, &attributes,
			GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL |
			GDK_WA_COLORMAP);

		gdk_window_set_user_data(widget->window, widget);

		xtext->depth = gdk_window_get_visual(widget->window)->depth;

		val.subwindow_mode = GDK_INCLUDE_INFERIORS;
		val.graphics_exposures = 0;

		xtext->bgc = gdk_gc_new_with_values(widget->window, &val,
			GDK_GC_EXPOSURES | GDK_GC_SUBWINDOW);
		xtext->fgc = gdk_gc_new_with_values(widget->window, &val,
			GDK_GC_EXPOSURES | GDK_GC_SUBWINDOW);
		xtext->light_gc = gdk_gc_new_with_values(widget->window, &val,
			GDK_GC_EXPOSURES | GDK_GC_SUBWINDOW);
		xtext->dark_gc = gdk_gc_new_with_values(widget->window, &val,
			GDK_GC_EXPOSURES | GDK_GC_SUBWINDOW);
		xtext->thin_gc = gdk_gc_new_with_values(widget->window, &val,
			GDK_GC_EXPOSURES | GDK_GC_SUBWINDOW);
		xtext->marker_gc = gdk_gc_new_with_values(widget->window, &val,
			GDK_GC_EXPOSURES | GDK_GC_SUBWINDOW);

		/* for the separator bar (light) */
		col.red = 0xffff; col.green = 0xffff; col.blue = 0xffff;
		gdk_colormap_alloc_color(cmap, &col, false, true);
		gdk_gc_set_foreground(xtext->light_gc, &col);

		/* for the separator bar (dark) */
		col.red = 0x1111; col.green = 0x1111; col.blue = 0x1111;
		gdk_colormap_alloc_color(cmap, &col, false, true);
		gdk_gc_set_foreground(xtext->dark_gc, &col);

		/* for the separator bar (thinline) */
		col.red = 0x8e38; col.green = 0x8e38; col.blue = 0x9f38;
		gdk_colormap_alloc_color(cmap, &col, false, true);
		gdk_gc_set_foreground(xtext->thin_gc, &col);

		/* for the marker bar (marker) */
		gdk_gc_set_foreground(xtext->marker_gc, &xtext->palette[XTEXT_MARKER]);

		xtext_set_fg(xtext, xtext->fgc, XTEXT_FG);
		xtext_set_bg(xtext, xtext->fgc, XTEXT_BG);
		xtext_set_fg(xtext, xtext->bgc, XTEXT_BG);

		/* draw directly to window */
		xtext->draw_buf = widget->window;

		if (xtext->pixmap)
		{
			gdk_gc_set_tile(xtext->bgc, xtext->pixmap);
			gdk_gc_set_ts_origin(xtext->bgc, 0, 0);
			xtext->ts_x = xtext->ts_y = 0;
			gdk_gc_set_fill(xtext->bgc, GDK_TILED);
		}

		xtext->hand_cursor = gdk_cursor_new_for_display(gdk_window_get_display(widget->window), GDK_HAND1);
		xtext->resize_cursor = gdk_cursor_new_for_display(gdk_window_get_display(widget->window), GDK_LEFT_SIDE);

		gdk_window_set_back_pixmap(widget->window, nullptr, false);
		widget->style = gtk_style_attach(widget->style, widget->window);

		backend_init(xtext);
	}

	static void
		gtk_xtext_size_request(GtkWidget *, GtkRequisition * requisition)
	{
		requisition->width = 200;
		requisition->height = 90;
	}

	static void
		gtk_xtext_size_allocate(GtkWidget * widget, GtkAllocation * allocation)
	{
		GtkXText *xtext = GTK_XTEXT(widget);
		bool height_only = false;

		if (allocation->width == xtext->buffer->window_width)
			height_only = true;

		widget->allocation = *allocation;
		if (gtk_widget_get_realized(GTK_WIDGET(widget)))
		{
			xtext->buffer->window_width = allocation->width;
			xtext->buffer->window_height = allocation->height;

			gdk_window_move_resize(widget->window, allocation->x, allocation->y,
				allocation->width, allocation->height);
			dontscroll(xtext->buffer);	/* force scrolling off */
			if (!height_only)
				gtk_xtext_calc_lines(xtext->buffer, false);
			else
			{
				xtext->buffer->impl->pagetop_ent = nullptr;
				gtk_xtext_adjustment_set(xtext->buffer, false);
			}
			if (xtext->buffer->scrollbar_down)
				gtk_adjustment_set_value(xtext->adj, xtext->adj->upper -
				xtext->adj->page_size);
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
		find_x(GtkXText *xtext, textentry *ent, int x, int subline, int indent)
	{
		int xx = indent;
		int suboff;
		int off, len, wid, mbl, mbw;

		/* Skip to the first chunk of stuff for the subline */
		std::vector<offlen_t>::const_iterator meta, hid = ent->slp.cend();
		if (subline > 0)
		{
			suboff = ent->sublines[subline - 1];
			decltype(meta) end = ent->slp.cend();
			for (meta = ent->slp.cbegin(); meta != end; ++meta)
			{
				if (meta->off + meta->len > suboff)
					break;
			}
		}
		else
		{
			suboff = 0;
			meta = ent->slp.cbegin();
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
			mbl = charlen(ent->str.c_str() + off);
			len -= mbl;
			off += mbl;
		}
		if (len < 0)
			return ent->str.size();		/* Bad char -- return max offset. */

		/* Step through characters to find the one at the x position */
		wid = x - indent;
		len = meta->len - (off - meta->off);
		while (wid > 0)
		{
			mbl = charlen(ent->str.c_str() + off);
			mbw = backend_get_text_width_emph(xtext, ustring_ref(ent->str.c_str() + off, mbl), meta->emph);
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
				if (meta == ent->slp.cend())
					return ent->str.size();
				off = meta->off;
				len = meta->len;
			}
		}

		/* If previous chunk exists and is marked hidden, regard it as unhidden */
		if (hid != ent->slp.cend() && meta != ent->slp.cend() && hid + 1 == meta)
		{
			off = hid->off;
		}

		/* Return offset of character at x within subline */
		return off;
	}

	static int
		gtk_xtext_find_x(GtkXText * xtext, int x, textentry * ent, int subline,
		int line, gboolean *out_of_bounds)
	{
		int indent;
		const unsigned char *str;

		if (subline < 1)
			indent = ent->indent;
		else
			indent = xtext->buffer->indent;

		if (line > xtext->adj->page_size || line < 0)
		{
			*out_of_bounds = true;
			return 0;
		}

		str = ent->str.c_str() + gtk_xtext_find_subline(xtext, ent, subline);
		if (str >= ent->str.c_str() + ent->str.size())
			return 0;

		/* Let user select left a few pixels to grab hidden text e.g. '<' */
		if (x < indent - xtext->space_width)
		{
			*out_of_bounds = true;
			return (str - ent->str.c_str());
		}

		*out_of_bounds = false;

		return find_x(xtext, ent, x, subline, indent);
	}

	static textentry *
		gtk_xtext_find_char(GtkXText * xtext, int x, int y, int *off,
		gboolean *out_of_bounds, int *ret_subline)
	{
		/* Adjust y value for negative rounding, double to int */
		if (y < 0)
			y -= xtext->fontsize;

		int subline;
		int line = (y + xtext->pixel_offset) / xtext->fontsize;
		auto ent = gtk_xtext_nth(xtext, line + (int)xtext->adj->value, &subline);
		if (!ent)
			return nullptr;

		if (off)
			*off = gtk_xtext_find_x(xtext, x, ent, subline, line, out_of_bounds);

		if (ret_subline)
			*ret_subline = subline;

		return ent;
	}

	static void
		gtk_xtext_draw_sep(GtkXText * xtext, int y)
	{
		int x, height;
		GdkGC *light, *dark;

		if (y == -1)
		{
			y = 0;
			height = GTK_WIDGET(xtext)->allocation.height;
		}
		else
		{
			height = xtext->fontsize;
		}

		/* draw the separator line */
		if (xtext->separator && xtext->buffer->indent)
		{
			light = xtext->light_gc;
			dark = xtext->dark_gc;

			x = xtext->buffer->indent - ((xtext->space_width + 1) / 2);
			if (x < 1)
				return;

			if (xtext->thinline)
			{
				if (xtext->moving_separator)
					gdk_draw_line(xtext->draw_buf, light, x, y, x, y + height);
				else
					gdk_draw_line(xtext->draw_buf, xtext->thin_gc, x, y, x, y + height);
			}
			else
			{
				if (xtext->moving_separator)
				{
					gdk_draw_line(xtext->draw_buf, light, x - 1, y, x - 1, y + height);
					gdk_draw_line(xtext->draw_buf, dark, x, y, x, y + height);
				}
				else
				{
					gdk_draw_line(xtext->draw_buf, dark, x - 1, y, x - 1, y + height);
					gdk_draw_line(xtext->draw_buf, light, x, y, x, y + height);
				}
			}
		}
	}

	static void
		gtk_xtext_draw_marker(GtkXText * xtext, textentry * ent, int y)
	{
		if (!xtext->marker) return;

		int render_y;
		if (xtext->buffer->impl->marker_pos == ent)
		{
			render_y = y + xtext->font->descent;
		}
		else if (xtext->buffer->impl->marker_pos == ent->next && ent->next != nullptr)
		{
			render_y = y + xtext->font->descent + xtext->fontsize * ent->sublines.size();
		}
		else return;

		int x = 0;
		int width = GTK_WIDGET(xtext)->allocation.width;

		gdk_draw_line(xtext->draw_buf, xtext->marker_gc, x, render_y, x + width, render_y);

		if (gtk_window_has_toplevel_focus(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(xtext)))))
		{
			xtext->buffer->marker_seen = true;
		}
	}

	static void
		gtk_xtext_paint(GtkWidget *widget, GdkRectangle *area)
	{
		GtkXText *xtext = GTK_XTEXT(widget);

		if (area->x == 0 && area->y == 0 &&
			area->height == widget->allocation.height &&
			area->width == widget->allocation.width)
		{
			dontscroll(xtext->buffer);	/* force scrolling off */
			gtk_xtext_render_page(xtext);
			return;
		}

		textentry *ent_end;
		int x, y;
		auto ent_start = gtk_xtext_find_char(xtext, area->x, area->y, nullptr, nullptr, nullptr);
		if (!ent_start)
		{
			xtext_draw_bg(xtext, area->x, area->y, area->width, area->height);
			goto xit;
		}
		ent_end = gtk_xtext_find_char(xtext, area->x + area->width,
			area->y + area->height, nullptr, nullptr, nullptr);
		if (!ent_end)
			ent_end = xtext->buffer->impl->text_last;

		/* can't set a clip here, because fgc/bgc are used to draw the DB too */
		/*	backend_set_clip (xtext, area);*/
		xtext->clip_x = area->x;
		xtext->clip_x2 = area->x + area->width;
		xtext->clip_y = area->y;
		xtext->clip_y2 = area->y + area->height;

		/* y is the last pixel y location it rendered text at */
		y = gtk_xtext_render_ents(xtext, ent_start, ent_end);

		if (y && y < widget->allocation.height && !ent_end->next)
		{
			GdkRectangle rect;

			rect.x = 0;
			rect.y = y;
			rect.width = widget->allocation.width;
			rect.height = widget->allocation.height - y;

			/* fill any space below the last line that also intersects with
			the exposure rectangle */
			if (gdk_rectangle_intersect(area, &rect, &rect))
			{
				xtext_draw_bg(xtext, rect.x, rect.y, rect.width, rect.height);
			}
		}

		/*backend_clear_clip (xtext);*/
		xtext->clip_x = 0;
		xtext->clip_x2 = 1000000;
		xtext->clip_y = 0;
		xtext->clip_y2 = 1000000;

	xit:
		x = xtext->buffer->indent - ((xtext->space_width + 1) / 2);
		if (area->x <= x)
			gtk_xtext_draw_sep(xtext, -1);
	}

	static gboolean
		gtk_xtext_expose(GtkWidget * widget, GdkEventExpose * event)
	{
		gtk_xtext_paint(widget, &event->area);
		return false;
	}

	/* render a selection that has extended or contracted upward */

	static void
		gtk_xtext_selection_up(GtkXText *xtext, textentry *start, textentry *end,
		int start_offset)
	{
		/* render all the complete lines */
		if (start->next == end)
			gtk_xtext_render_ents(xtext, end, nullptr);
		else
			gtk_xtext_render_ents(xtext, start->next, end);

		/* now the incomplete upper line */
		if (start == xtext->buffer->impl->last_ent_start)
			xtext->jump_in_offset = xtext->buffer->last_offset_start;
		else
			xtext->jump_in_offset = start_offset;
		gtk_xtext_render_ents(xtext, start, nullptr);
		xtext->jump_in_offset = 0;
	}

	/* render a selection that has extended or contracted downward */

	static void
		gtk_xtext_selection_down(GtkXText *xtext, textentry *start, textentry *end,
		int end_offset)
	{
		/* render all the complete lines */
		if (end->prev == start)
			gtk_xtext_render_ents(xtext, start, nullptr);
		else
			gtk_xtext_render_ents(xtext, start, end->prev);

		/* now the incomplete bottom line */
		if (end == xtext->buffer->impl->last_ent_end)
			xtext->jump_out_offset = xtext->buffer->last_offset_end;
		else
			xtext->jump_out_offset = end_offset;
		gtk_xtext_render_ents(xtext, end, nullptr);
		xtext->jump_out_offset = 0;
	}

	static void
		gtk_xtext_selection_render(GtkXText *xtext, textentry *start_ent, textentry *end_ent)
	{
		textentry *ent;
		int start_offset = start_ent->mark_start;
		int end_offset = end_ent->mark_end;
		int start, end;

		xtext->skip_border_fills = true;
		xtext->skip_stamp = true;

		/* force an optimized render if there was no previous selection */
		if (xtext->buffer->impl->last_ent_start == nullptr && start_ent == end_ent)
		{
			xtext->buffer->last_offset_start = start_offset;
			xtext->buffer->last_offset_end = end_offset;
			goto lamejump;
		}

		/* mark changed within 1 ent only? */
		if (xtext->buffer->impl->last_ent_start == start_ent &&
			xtext->buffer->impl->last_ent_end == end_ent)
		{
			/* when only 1 end of the selection is changed, we can really
			save on rendering */
			if (xtext->buffer->last_offset_start == start_offset ||
				xtext->buffer->last_offset_end == end_offset)
			{
			lamejump:
				ent = end_ent;
				/* figure out where to start and end the rendering */
				if (end_offset > xtext->buffer->last_offset_end)
				{
					end = end_offset;
					start = xtext->buffer->last_offset_end;
				}
				else if (end_offset < xtext->buffer->last_offset_end)
				{
					end = xtext->buffer->last_offset_end;
					start = end_offset;
				}
				else if (start_offset < xtext->buffer->last_offset_start)
				{
					end = xtext->buffer->last_offset_start;
					start = start_offset;
					ent = start_ent;
				}
				else if (start_offset > xtext->buffer->last_offset_start)
				{
					end = start_offset;
					start = xtext->buffer->last_offset_start;
					ent = start_ent;
				}
				else
				{	/* WORD selects end up here */
					end = end_offset;
					start = start_offset;
				}
			}
			else
			{
				/* LINE selects end up here */
				/* so which ent actually changed? */
				ent = start_ent;
				if (xtext->buffer->last_offset_start == start_offset)
					ent = end_ent;

				end = std::max(xtext->buffer->last_offset_end, end_offset);
				start = std::min(xtext->buffer->last_offset_start, start_offset);
			}

			xtext->jump_out_offset = end;
			xtext->jump_in_offset = start;
			gtk_xtext_render_ents(xtext, ent, nullptr);
			xtext->jump_out_offset = 0;
			xtext->jump_in_offset = 0;
		}
		/* marking downward? */
		else if (xtext->buffer->impl->last_ent_start == start_ent &&
			xtext->buffer->last_offset_start == start_offset)
		{
			/* find the range that covers both old and new selection */
			ent = start_ent;
			while (ent)
			{
				if (ent == xtext->buffer->impl->last_ent_end)
				{
					gtk_xtext_selection_down(xtext, ent, end_ent, end_offset);
					/*gtk_xtext_render_ents (xtext, ent, end_ent);*/
					break;
				}
				if (ent == end_ent)
				{
					gtk_xtext_selection_down(xtext, ent, xtext->buffer->impl->last_ent_end, end_offset);
					/*gtk_xtext_render_ents (xtext, ent, xtext->buffer->last_ent_end);*/
					break;
				}
				ent = ent->next;
			}
		}
		/* marking upward? */
		else if (xtext->buffer->impl->last_ent_end == end_ent &&
			xtext->buffer->last_offset_end == end_offset)
		{
			ent = end_ent;
			while (ent)
			{
				if (ent == start_ent)
				{
					gtk_xtext_selection_up(xtext, xtext->buffer->impl->last_ent_start, ent, start_offset);
					/*gtk_xtext_render_ents (xtext, xtext->buffer->last_ent_start, ent);*/
					break;
				}
				if (ent == xtext->buffer->impl->last_ent_start)
				{
					gtk_xtext_selection_up(xtext, start_ent, ent, start_offset);
					/*gtk_xtext_render_ents (xtext, start_ent, ent);*/
					break;
				}
				ent = ent->prev;
			}
		}
		else	/* cross-over mark (stretched or shrunk at both ends) */
		{
			/* unrender the old mark */
			gtk_xtext_render_ents(xtext, xtext->buffer->impl->last_ent_start, xtext->buffer->impl->last_ent_end);
			/* now render the new mark, but skip overlaps */
			if (start_ent == xtext->buffer->impl->last_ent_start)
			{
				/* if the new mark is a sub-set of the old, do nothing */
				if (start_ent != end_ent)
					gtk_xtext_render_ents(xtext, start_ent->next, end_ent);
			}
			else if (end_ent == xtext->buffer->impl->last_ent_end)
			{
				/* if the new mark is a sub-set of the old, do nothing */
				if (start_ent != end_ent)
					gtk_xtext_render_ents(xtext, start_ent, end_ent->prev);
			}
			else
				gtk_xtext_render_ents(xtext, start_ent, end_ent);
		}

		xtext->buffer->impl->last_ent_start = start_ent;
		xtext->buffer->impl->last_ent_end = end_ent;
		xtext->buffer->last_offset_start = start_offset;
		xtext->buffer->last_offset_end = end_offset;

		xtext->skip_border_fills = false;
		xtext->skip_stamp = false;
	}

	static void gtk_xtext_selection_draw(GtkXText * xtext, GdkEventMotion *, bool render)
	{
		int offset_start;
		int offset_end;
		int subline_start;
		int subline_end;
		gboolean oob = false;

		auto ent_start = gtk_xtext_find_char(xtext, xtext->select_start_x, xtext->select_start_y, &offset_start, &oob, &subline_start);
		auto ent_end = gtk_xtext_find_char(xtext, xtext->select_end_x, xtext->select_end_y, &offset_end, &oob, &subline_end);

		if ((!ent_start || !ent_end) && !xtext->buffer->impl->text_last && xtext->adj->value != xtext->buffer->old_value)
		{
			gtk_xtext_render_page(xtext);
			return;
		}

		if (!ent_start)
		{
			ent_start = xtext->buffer->impl->text_last;
			offset_start = ent_start->str.size();
		}

		if (!ent_end)
		{
			ent_end = xtext->buffer->impl->text_last;
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
		int apixes = std::abs(pixes);

		if (apixes < 6) return 100;
		if (apixes < 12) return 50;
		if (apixes < 20) return 20;
		return 10;
	}

	static gboolean gtk_xtext_scrolldown_timeout(GtkXText * xtext)
	{
		int p_y, win_height;
		xtext_buffer *buf = xtext->buffer;
		GtkAdjustment *adj = xtext->adj;

		gdk_window_get_pointer(GTK_WIDGET(xtext)->window, nullptr, &p_y, nullptr);
		win_height = gdk_window_get_height(gtk_widget_get_window(GTK_WIDGET(xtext)));

		if (buf->impl->last_ent_end == nullptr ||	/* If context has changed OR */
			buf->impl->pagetop_ent == nullptr ||	/* pagetop_ent is reset OR */
			p_y <= win_height ||			/* pointer not below bottom margin OR */
			adj->value >= adj->upper - adj->page_size) 	/* we're scrolled to bottom */
		{
			xtext->scroll_tag = 0;
			return false;
		}

		xtext->select_start_y -= xtext->fontsize;
		xtext->select_start_adj++;
		adj->value++;
		gtk_adjustment_value_changed(adj);
		gtk_xtext_selection_draw(xtext, nullptr, true);
		gtk_xtext_render_ents(xtext, buf->impl->pagetop_ent->next, buf->impl->last_ent_end);
		xtext->scroll_tag = g_timeout_add(gtk_xtext_timeout_ms(p_y - win_height),
			(GSourceFunc)
			gtk_xtext_scrolldown_timeout,
			xtext);

		return false;
	}

	static gboolean gtk_xtext_scrollup_timeout(GtkXText * xtext)
	{
		int p_y;
		xtext_buffer *buf = xtext->buffer;
		GtkAdjustment *adj = xtext->adj;
		int delta_y;

		gdk_window_get_pointer(GTK_WIDGET(xtext)->window, nullptr, &p_y, nullptr);

		if (buf->impl->last_ent_start == nullptr ||	/* If context has changed OR */
			buf->impl->pagetop_ent == nullptr ||		/* pagetop_ent is reset OR */
			p_y >= 0 ||							/* not above top margin OR */
			adj->value == 0.0)						/* we're scrolled to the top */
		{
			xtext->scroll_tag = 0;
			return false;
		}

		if (adj->value < 0.0)
		{
			delta_y = adj->value * xtext->fontsize;
			adj->value = 0.0;
		}
		else {
			delta_y = xtext->fontsize;
			adj->value--;
		}
		xtext->select_start_y += delta_y;
		xtext->select_start_adj = static_cast<int>(adj->value);
		gtk_adjustment_value_changed(adj);
		gtk_xtext_selection_draw(xtext, nullptr, true);
		gtk_xtext_render_ents(xtext, buf->impl->pagetop_ent->prev, buf->impl->last_ent_end);
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

		int win_height = gdk_window_get_height(gtk_widget_get_window(GTK_WIDGET(xtext)));

		/* selecting past top of window, scroll up! */
		if (p_y < 0 && xtext->adj->value >= 0)
		{
			gtk_xtext_scrollup_timeout(xtext);
		}

		/* selecting past bottom of window, scroll down! */
		else if (p_y > win_height &&
			xtext->adj->value < (xtext->adj->upper - xtext->adj->page_size))
		{
			gtk_xtext_scrolldown_timeout(xtext);
		}
		else
		{
			int moved = (int)xtext->adj->value - xtext->select_start_adj;
			xtext->select_start_y -= (moved * xtext->fontsize);
			xtext->select_start_adj = xtext->adj->value;
			gtk_xtext_selection_draw(xtext, event, render);
		}
	}

	static char *
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

		/* remove color characters from the length */
		gtk_xtext_strip_color(ustring_ref(word, len_to_offset), xtext->scratch_buffer, &len_to_offset, nullptr, false);

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

		return (char*)word;
	}

	static void
		gtk_xtext_unrender_hilight(GtkXText *xtext)
	{
		xtext->render_hilights_only = true;
		xtext->skip_border_fills = true;
		xtext->skip_stamp = true;
		xtext->un_hilight = true;

		gtk_xtext_render_ents(xtext, xtext->hilight_ent, nullptr);

		xtext->render_hilights_only = false;
		xtext->skip_border_fills = false;
		xtext->skip_stamp = false;
		xtext->un_hilight = false;
	}

	static gboolean
		gtk_xtext_leave_notify(GtkWidget * widget, GdkEventCrossing *)
	{
		GtkXText *xtext = GTK_XTEXT(widget);

		if (xtext->cursor_hand)
		{
			gtk_xtext_unrender_hilight(xtext);
			xtext->hilight_start = -1;
			xtext->hilight_end = -1;
			xtext->cursor_hand = false;
			gdk_window_set_cursor(widget->window, 0);
			xtext->hilight_ent = nullptr;
		}

		if (xtext->cursor_resize)
		{
			gtk_xtext_unrender_hilight(xtext);
			xtext->hilight_start = -1;
			xtext->hilight_end = -1;
			xtext->cursor_resize = false;
			gdk_window_set_cursor(widget->window, 0);
			xtext->hilight_ent = nullptr;
		}

		return false;
	}

	/* check if we should mark time stamps, and if a redraw is needed */

	static gboolean
		gtk_xtext_check_mark_stamp(GtkXText *xtext, GdkModifierType mask)
	{
		gboolean redraw = false;

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
		unsigned char *word;
		int word_type = 0;
		std::vector<offlen_t> slp;

		word = (unsigned char*)gtk_xtext_get_word(xtext, x, y, word_ent, offset, len, &slp);
		if (word)
		{
			int laststart, lastend;

			word_type = xtext->urlcheck_function(GTK_WIDGET(xtext), reinterpret_cast<const char*>(word));
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
		}
		return word_type;
	}

	static gboolean
		gtk_xtext_motion_notify(GtkWidget * widget, GdkEventMotion * event)
	{
		GtkXText *xtext = GTK_XTEXT(widget);
		GdkModifierType mask;
		int redraw, tmp, x, y, offset, len, line_x;
		textentry *word_ent;
		int word_type;

		gdk_window_get_pointer(widget->window, &x, &y, &mask);

		if (xtext->moving_separator)
		{
			if (x < (3 * widget->allocation.width) / 5 && x > 15)
			{
				tmp = xtext->buffer->indent;
				xtext->buffer->indent = x;
				gtk_xtext_fix_indent(xtext->buffer);
				if (tmp != xtext->buffer->indent)
				{
					gtk_xtext_recalc_widths(xtext->buffer, false);
					if (xtext->buffer->scrollbar_down)
						gtk_adjustment_set_value(xtext->adj, xtext->adj->upper -
						xtext->adj->page_size);
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
			redraw = gtk_xtext_check_mark_stamp(xtext, mask);
			gtk_grab_add(widget);
			/*gdk_pointer_grab (widget->window, true,
			GDK_BUTTON_RELEASE_MASK |
			GDK_BUTTON_MOTION_MASK, nullptr, nullptr, 0);*/
			xtext->select_end_x = x;
			xtext->select_end_y = y;
			gtk_xtext_selection_update(xtext, event, y, !redraw);
			xtext->hilighting = true;

			/* user has pressed or released SHIFT, must redraw entire selection */
			if (redraw)
			{
				xtext->force_stamp = true;
				gtk_xtext_render_ents(xtext, xtext->buffer->impl->last_ent_start,
					xtext->buffer->impl->last_ent_end);
				xtext->force_stamp = false;
			}
			return false;
		}

		if (xtext->separator && xtext->buffer->indent)
		{
			line_x = xtext->buffer->indent - ((xtext->space_width + 1) / 2);
			if (line_x == x || line_x == x + 1 || line_x == x - 1)
			{
				if (!xtext->cursor_resize)
				{
					gdk_window_set_cursor(GTK_WIDGET(xtext)->window,
						xtext->resize_cursor);
					xtext->cursor_hand = false;
					xtext->cursor_resize = true;
				}
				return false;
			}
		}

		if (xtext->urlcheck_function == nullptr)
			return false;

		word_type = gtk_xtext_get_word_adjust(xtext, x, y, &word_ent, &offset, &len);
		if (word_type > 0)
		{
			if (!xtext->cursor_hand ||
				xtext->hilight_ent != word_ent ||
				xtext->hilight_start != offset ||
				xtext->hilight_end != offset + len)
			{
				if (!xtext->cursor_hand)
				{
					gdk_window_set_cursor(GTK_WIDGET(xtext)->window,
						xtext->hand_cursor);
					xtext->cursor_hand = true;
					xtext->cursor_resize = false;
				}

				/* un-render the old hilight */
				if (xtext->hilight_ent)
					gtk_xtext_unrender_hilight(xtext);

				xtext->hilight_ent = word_ent;
				xtext->hilight_start = offset;
				xtext->hilight_end = offset + len;

				xtext->skip_border_fills = true;
				xtext->render_hilights_only = true;
				xtext->skip_stamp = true;

				gtk_xtext_render_ents(xtext, word_ent, nullptr);

				xtext->skip_border_fills = false;
				xtext->render_hilights_only = false;
				xtext->skip_stamp = false;
			}
			return false;
		}

		gtk_xtext_leave_notify(widget, nullptr);

		return false;
	}

	static void gtk_xtext_set_clip_owner(GtkWidget * xtext, GdkEventButton * event)
	{
		if (GTK_XTEXT(xtext)->selection_buffer &&
			GTK_XTEXT(xtext)->selection_buffer != GTK_XTEXT(xtext)->buffer)
			gtk_xtext_selection_clear(GTK_XTEXT(xtext)->selection_buffer);

		GTK_XTEXT(xtext)->selection_buffer = GTK_XTEXT(xtext)->buffer;

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

namespace {
	void gtk_xtext_unselect(GtkXText &xtext)
	{
		xtext_buffer *buf = xtext.buffer;

		xtext.skip_border_fills = true;
		xtext.skip_stamp = true;

		xtext.jump_in_offset = buf->impl->last_ent_start->mark_start;
		/* just a single ent was marked? */
		if (buf->impl->last_ent_start == buf->impl->last_ent_end)
		{
			xtext.jump_out_offset = buf->impl->last_ent_start->mark_end;
			buf->impl->last_ent_end = nullptr;
		}

		gtk_xtext_selection_clear(xtext.buffer);

		/* FIXME: use jump_out on multi-line selects too! */
		xtext.jump_in_offset = 0;
		xtext.jump_out_offset = 0;
		gtk_xtext_render_ents(&xtext, buf->impl->last_ent_start, buf->impl->last_ent_end);

		xtext.skip_border_fills = false;
		xtext.skip_stamp = false;

		xtext.buffer->impl->last_ent_start = nullptr;
		xtext.buffer->impl->last_ent_end = nullptr;
	}

	static gboolean
		gtk_xtext_button_release(GtkWidget * widget, GdkEventButton * event)
	{
		GtkXText *xtext = GTK_XTEXT(widget);

		if (xtext->moving_separator)
		{
			xtext->moving_separator = false;
			auto old = xtext->buffer->indent;
			if (event->x < (4 * widget->allocation.width) / 5 && event->x > 15)
				xtext->buffer->indent = event->x;
			gtk_xtext_fix_indent(xtext->buffer);
			if (xtext->buffer->indent != old)
			{
				gtk_xtext_recalc_widths(xtext->buffer, false);
				gtk_xtext_adjustment_set(xtext->buffer, true);
				gtk_xtext_render_page(xtext);
			}
			else
				gtk_xtext_draw_sep(xtext, -1);
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
			if (xtext->buffer->impl->last_ent_start)
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
				return false;
			}

			if (xtext->select_start_x == event->x &&
				xtext->select_start_y == event->y &&
				xtext->buffer->impl->last_ent_start)
			{
				gtk_xtext_unselect(*xtext);
				xtext->mark_stamp = false;
				return false;
			}

			if (!xtext->hilighting)
			{
				auto word = (unsigned char*)gtk_xtext_get_word(xtext, event->x, event->y, 0, 0, 0, 0);
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
		textentry *ent;
		char *word;
		int line_x, x, y, offset, len;

		gdk_window_get_pointer(widget->window, &x, &y, &mask);

		if (event->button == 3 || event->button == 2) /* right/middle click */
		{
			word = gtk_xtext_get_word(xtext, x, y, 0, 0, 0, 0);
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
			gtk_xtext_check_mark_stamp(xtext, mask);
			if (gtk_xtext_get_word(xtext, x, y, &ent, &offset, &len, 0))
			{
				if (len == 0)
					return false;
				gtk_xtext_selection_clear(xtext->buffer);
				ent->mark_start = offset;
				ent->mark_end = offset + len;
				gtk_xtext_selection_render(xtext, ent, ent);
				xtext->word_select = true;
			}

			return false;
		}

		if (event->type == GDK_3BUTTON_PRESS)	/* LINE select */
		{
			gtk_xtext_check_mark_stamp(xtext, mask);
			if (gtk_xtext_get_word(xtext, x, y, &ent, 0, 0, 0))
			{
				gtk_xtext_selection_clear(xtext->buffer);
				ent->mark_start = 0;
				ent->mark_end = ent->str.size();
				gtk_xtext_selection_render(xtext, ent, ent);
				xtext->line_select = true;
			}

			return false;
		}

		/* check if it was a separator-bar click */
		if (xtext->separator && xtext->buffer->indent)
		{
			line_x = xtext->buffer->indent - ((xtext->space_width + 1) / 2);
			if (line_x == x || line_x == x + 1 || line_x == x - 1)
			{
				xtext->moving_separator = true;
				/* draw the separator line */
				gtk_xtext_draw_sep(xtext, -1);
				return false;
			}
		}

		xtext->button_down = true;
		xtext->select_start_x = x;
		xtext->select_start_y = y;
		xtext->select_start_adj = xtext->adj->value;

		return false;
	}

	/* another program has claimed the selection */

	gboolean gtk_xtext_selection_kill(GtkXText *xtext, GdkEventSelection *)
	{
#ifndef WIN32
		if (xtext->buffer->impl->last_ent_start)
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
		auto ent = buf->impl->last_ent_start;
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
					if (ent->mark_start == 0 && xtext->mark_stamp)
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
		if (!xtext->color_paste)
		{
			glib_string res((char*)gtk_xtext_strip_color(ustring_ref(reinterpret_cast<const unsigned char*>(stripped.c_str()), stripped.size()), nullptr, &len, nullptr, false));
			stripped = std::string(res.get(), len);
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
			GdkDisplay *display = gdk_window_get_display(widget->window);
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
		GtkXText *xtext = GTK_XTEXT(widget);
		if (event->direction == GDK_SCROLL_UP)		/* mouse wheel pageUp */
		{
			auto new_value = xtext->adj->value - (xtext->adj->page_increment / 10.0);
			if (new_value < xtext->adj->lower)
				new_value = xtext->adj->lower;
			gtk_adjustment_set_value(xtext->adj, new_value);
		}
		else if (event->direction == GDK_SCROLL_DOWN)	/* mouse wheel pageDn */
		{
			auto new_value = xtext->adj->value + (xtext->adj->page_increment / 10.0);
			if (new_value >(xtext->adj->upper - xtext->adj->page_size))
				new_value = xtext->adj->upper - xtext->adj->page_size;
			gtk_adjustment_set_value(xtext->adj, new_value);
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

	void gtk_xtext_class_init(GtkXTextClass * text_class)
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
		widget_class->expose_event = gtk_xtext_expose;
		widget_class->scroll_event = gtk_xtext_scroll;
		widget_class->leave_notify_event = gtk_xtext_leave_notify;
		widget_class->set_scroll_adjustments_signal = xtext_signals[SET_SCROLL_ADJUSTMENTS];

		xtext_class->word_click = nullptr;
		xtext_class->set_scroll_adjustments = gtk_xtext_scroll_adjustments;
	}
} // end anonymous namespace
GType
gtk_xtext_get_type(void)
{
	static GType xtext_type = 0;

	if (!xtext_type)
	{
		static const GTypeInfo xtext_info =
		{
			sizeof(GtkXTextClass),
			nullptr,		/* base_init */
			nullptr,		/* base_finalize */
			(GClassInitFunc)gtk_xtext_class_init,
			nullptr,		/* class_finalize */
			nullptr,		/* class_data */
			sizeof(GtkXText),
			0,		/* n_preallocs */
			(GInstanceInitFunc)gtk_xtext_init,
		};

		xtext_type = g_type_register_static(GTK_TYPE_WIDGET, "GtkXText",
			&xtext_info, GTypeFlags());
	}

	return xtext_type;
}

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

	/* gives width of a string, excluding the mIRC codes */

	static int gtk_xtext_text_width_ent(GtkXText *xtext, textentry *ent)
	{
		ent->slp.clear();
		auto new_buf = gtk_xtext_strip_color(ent->str, xtext->scratch_buffer,
			nullptr, &ent->slp, 2);

		auto width = backend_get_text_width_slp(xtext, new_buf, ent->slp);

		for (auto & meta : ent->slp)
		{
			meta.width = backend_get_text_width_emph(xtext, ustring_ref(ent->str.c_str() + meta.off, meta.len), meta.emph);
		}
		return width;
	}

	int gtk_xtext_text_width(GtkXText *xtext, const ustring_ref & text)
	{
		int new_len;
		std::vector<offlen_t> slp;
		auto new_buf = gtk_xtext_strip_color(text, xtext->scratch_buffer,
			&new_len, &slp, !xtext->ignore_hidden);

		return backend_get_text_width_slp(xtext, new_buf, slp);
	}

	/* actually draw text to screen (one run with the same color/attribs) */

	static int
		gtk_xtext_render_flush(GtkXText * xtext, int x, int y, const unsigned char str[],
		int len, GdkGC *gc, int *emphasis)
	{
		if (xtext->dont_render || len < 1 || xtext->hidden)
			return 0;

		auto str_width = backend_get_text_width_emph(xtext, ustring_ref(str, len), *emphasis);

		if (xtext->dont_render2)
			return str_width;

		/* roll-your-own clipping (avoiding XftDrawString is always good!) */
		if (x > xtext->clip_x2 || x + str_width < xtext->clip_x)
			return str_width;
		if (y - xtext->font->ascent > xtext->clip_y2 || (y - xtext->font->ascent) + xtext->fontsize < xtext->clip_y)
			return str_width;

		GdkDrawable *pix = nullptr;
		int dest_x = 0, dest_y = 0;
		bool dofill;
		if (xtext->render_hilights_only)
		{
			if (!xtext->in_hilight)	/* is it a hilight prefix? */
				return str_width;
			if (!xtext->un_hilight)	/* doing a hilight? no need to draw the text */
				goto dounder;
		}

		pix = gdk_pixmap_new(xtext->draw_buf, str_width, xtext->fontsize, xtext->depth);
		if (pix)
		{
			dest_x = x;
			dest_y = y - xtext->font->ascent;

			gdk_gc_set_ts_origin(xtext->bgc, xtext->ts_x - x, xtext->ts_y - dest_y);

			x = 0;
			y = xtext->font->ascent;
			xtext->draw_buf = pix;
		}

		dofill = true;

		/* backcolor is always handled by XDrawImageString */
		if (!xtext->backcolor && xtext->pixmap)
		{
			/* draw the background pixmap behind the text - CAUSES FLICKER HERE!! */
			xtext_draw_bg(xtext, x, y - xtext->font->ascent, str_width,
				xtext->fontsize);
			dofill = false;	/* already drawn the background */
		}

		backend_draw_text_emph(xtext, dofill, gc, x, y, (const char*)str, len, str_width, *emphasis);

		if (pix)
		{
			GdkRectangle clip;
			GdkRectangle dest;

			gdk_gc_set_ts_origin(xtext->bgc, xtext->ts_x, xtext->ts_y);
			xtext->draw_buf = GTK_WIDGET(xtext)->window;
			clip.x = xtext->clip_x;
			clip.y = xtext->clip_y;
			clip.width = xtext->clip_x2 - xtext->clip_x;
			clip.height = xtext->clip_y2 - xtext->clip_y;

			dest.x = dest_x;
			dest.y = dest_y;
			dest.width = str_width;
			dest.height = xtext->fontsize;

			if (gdk_rectangle_intersect(&clip, &dest, &dest))
				/* dump the DB to window, but only within the clip_x/x2/y/y2 */
				gdk_draw_drawable(xtext->draw_buf, xtext->bgc, pix,
				dest.x - dest_x, dest.y - dest_y,
				dest.x, dest.y, dest.width, dest.height);
			g_object_unref(pix);
		}

		if (xtext->underline)
		{
		dounder:

			if (pix)
				y = dest_y + xtext->font->ascent + 1;
			else
			{
				y++;
				dest_x = x;
			}
			/* draw directly to window, it's out of the range of our DB */
			gdk_draw_line(xtext->draw_buf, gc, dest_x, y, dest_x + str_width - 1, y);
		}

		return str_width;
	}

	static void
		gtk_xtext_reset(GtkXText * xtext, int mark, int attribs)
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
				xtext_set_fg(xtext, xtext->fgc, XTEXT_FG);
			if (xtext->col_back != XTEXT_BG)
				xtext_set_bg(xtext, xtext->fgc, XTEXT_BG);
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

	/* render a single line, which WONT wrap, and parse mIRC colors */

#define RENDER_FLUSH x += gtk_xtext_render_flush (xtext, x, y, pstr, j, gc, emphasis)

	static int
		gtk_xtext_render_str(GtkXText * xtext, int y, textentry * ent,
		const unsigned char str[], int len, int win_width, int indent,
		int /*line*/, bool left_only, int *x_size_ret, int *emphasis)
	{
		GdkGC *gc;
		int i = 0, x = indent, j = 0;
		const unsigned char *pstr = str;
		int col_num, tmp;
		int offset;
		bool mark = false;
		int ret = 1;
		int k;
		bool srch_underline = false;
		bool srch_mark = false;

		xtext->in_hilight = false;

		offset = str - ent->str.c_str();

		gc = xtext->fgc;				  /* our foreground GC */

		if (ent->mark_start != -1 &&
			ent->mark_start <= i + offset && ent->mark_end > i + offset)
		{
			xtext_set_bg(xtext, gc, XTEXT_MARK_BG);
			xtext_set_fg(xtext, gc, XTEXT_MARK_FG);
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

		if (!xtext->skip_border_fills && !xtext->dont_render)
		{
			/* draw background to the left of the text */
			if (str == ent->str && indent > MARGIN && xtext->buffer->time_stamp)
			{
				/* don't overwrite the timestamp */
				if (indent > xtext->stamp_width)
				{
					xtext_draw_bg(xtext, xtext->stamp_width, y - xtext->font->ascent,
						indent - xtext->stamp_width, xtext->fontsize);
				}
			}
			else
			{
				/* fill the indent area with background gc */
				if (indent >= xtext->clip_x)
				{
					xtext_draw_bg(xtext, 0, y - xtext->font->ascent,
						std::min(indent, xtext->clip_x2), xtext->fontsize);
				}
			}
		}

		if (xtext->jump_in_offset > 0 && offset < xtext->jump_in_offset)
			xtext->dont_render2 = true;
		std::locale locale;
		while (i < len)
		{

			if (xtext->hilight_ent == ent && xtext->hilight_start == (i + offset))
			{
				RENDER_FLUSH;
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
						col_num = atoi(xtext->num);
						if (col_num == 99)	/* mIRC lameness */
							col_num = XTEXT_FG;
						else
							if (col_num > XTEXT_MAX_COLOR)
								col_num = col_num % XTEXT_MIRC_COLS;
						xtext->col_fore = col_num;
						if (!mark)
							xtext_set_fg(xtext, gc, col_num);
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
						col_num = atoi(xtext->num);
						if (xtext->parsing_backcolor)
						{
							if (col_num == 99)	/* mIRC lameness */
								col_num = XTEXT_BG;
							else
								if (col_num > XTEXT_MAX_COLOR)
									col_num = col_num % XTEXT_MIRC_COLS;
							if (col_num == XTEXT_BG)
								xtext->backcolor = false;
							else
								xtext->backcolor = true;
							if (!mark)
								xtext_set_bg(xtext, gc, col_num);
							xtext->col_back = col_num;
						}
						else
						{
							if (col_num == 99)	/* mIRC lameness */
								col_num = XTEXT_FG;
							else
								if (col_num > XTEXT_MAX_COLOR)
									col_num = col_num % XTEXT_MIRC_COLS;
							if (!mark)
								xtext_set_fg(xtext, gc, col_num);
							xtext->col_fore = col_num;
						}
						xtext->parsing_backcolor = false;
					}
					else
					{
						/* got a \003<non-digit>... i.e. reset colors */
						RENDER_FLUSH;
						pstr += j;
						j = 0;
						gtk_xtext_reset(xtext, mark, false);
					}
				}

				if (!left_only && !mark &&
					(k = gtk_xtext_search_offset(xtext->buffer, ent, offset + i)))
				{
					RENDER_FLUSH;
					pstr += j;
					j = 0;
					if (!(xtext->buffer->search_flags & highlight))
					{
						if (k & GTK_MATCH_CUR)
						{
							xtext_set_bg(xtext, gc, XTEXT_MARK_BG);
							xtext_set_fg(xtext, gc, XTEXT_MARK_FG);
							xtext->backcolor = true;
							srch_mark = true;
						}
						else
						{
							xtext_set_bg(xtext, gc, xtext->col_back);
							xtext_set_fg(xtext, gc, xtext->col_fore);
							xtext->backcolor = (xtext->col_back != XTEXT_BG) ? true : false;
							srch_mark = false;
						}
					}
					else
					{
						xtext->underline = (k & GTK_MATCH_CUR) ? true : false;
						if (k & (GTK_MATCH_START | GTK_MATCH_MID))
						{
							xtext_set_bg(xtext, gc, XTEXT_MARK_BG);
							xtext_set_fg(xtext, gc, XTEXT_MARK_FG);
							xtext->backcolor = true;
							srch_mark = true;
						}
						if (k & GTK_MATCH_END)
						{
							xtext_set_bg(xtext, gc, xtext->col_back);
							xtext_set_fg(xtext, gc, xtext->col_fore);
							xtext->backcolor = (xtext->col_back != XTEXT_BG) ? true : false;
							srch_mark = false;
							xtext->underline = false;
						}
						srch_underline = xtext->underline;
					}
				}

				switch (str[i])
				{
				case '\n':
					/*case ATTR_BEEP:*/
					break;
				case ATTR_REVERSE:
					RENDER_FLUSH;
					pstr += j + 1;
					j = 0;
					tmp = xtext->col_fore;
					xtext->col_fore = xtext->col_back;
					xtext->col_back = tmp;
					if (!mark)
					{
						xtext_set_fg(xtext, gc, xtext->col_fore);
						xtext_set_bg(xtext, gc, xtext->col_back);
					}
					if (xtext->col_back != XTEXT_BG)
						xtext->backcolor = true;
					else
						xtext->backcolor = false;
					break;
				case ATTR_BOLD:
					RENDER_FLUSH;
					*emphasis ^= EMPH_BOLD;
					pstr += j + 1;
					j = 0;
					break;
				case ATTR_UNDERLINE:
					RENDER_FLUSH;
					xtext->underline = !xtext->underline;
					pstr += j + 1;
					j = 0;
					break;
				case ATTR_ITALICS:
					RENDER_FLUSH;
					*emphasis ^= EMPH_ITAL;
					pstr += j + 1;
					j = 0;
					break;
				case ATTR_HIDDEN:
					RENDER_FLUSH;
					xtext->hidden = (!xtext->hidden) && (!xtext->ignore_hidden);
					pstr += j + 1;
					j = 0;
					break;
				case ATTR_RESET:
					RENDER_FLUSH;
					*emphasis = 0;
					pstr += j + 1;
					j = 0;
					gtk_xtext_reset(xtext, mark, !xtext->in_hilight);
					break;
				case ATTR_COLOR:
					RENDER_FLUSH;
					xtext->parsing_color = true;
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
					RENDER_FLUSH;
					pstr += j;
					j = 0;
				}
				else if ((pstr - str) + j == ent->left_len + 1)
				{
					RENDER_FLUSH;
					pstr += j;
					j = 0;
				}
			}

			/* have we been told to stop rendering at this point? */
			if (xtext->jump_out_offset > 0 && xtext->jump_out_offset <= (i + offset))
			{
				gtk_xtext_render_flush(xtext, x, y, pstr, j, gc, emphasis);
				ret = 0;	/* skip the rest of the lines, we're done. */
				j = 0;
				break;
			}

			if (xtext->jump_in_offset > 0 && xtext->jump_in_offset == (i + offset))
			{
				RENDER_FLUSH;
				pstr += j;
				j = 0;
				xtext->dont_render2 = false;
			}

			if (xtext->hilight_ent == ent && xtext->hilight_end == (i + offset))
			{
				RENDER_FLUSH;
				pstr += j;
				j = 0;
				xtext->underline = false;
				xtext->in_hilight = false;
				if (xtext->render_hilights_only)
				{
					/* stop drawing this ent */
					ret = 0;
					break;
				}
			}

			if (!mark && ent->mark_start == (i + offset))
			{
				RENDER_FLUSH;
				pstr += j;
				j = 0;
				xtext_set_bg(xtext, gc, XTEXT_MARK_BG);
				xtext_set_fg(xtext, gc, XTEXT_MARK_FG);
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
				RENDER_FLUSH;
				pstr += j;
				j = 0;
				xtext_set_bg(xtext, gc, xtext->col_back);
				xtext_set_fg(xtext, gc, xtext->col_fore);
				if (xtext->col_back != XTEXT_BG)
					xtext->backcolor = true;
				else
					xtext->backcolor = false;
				mark = false;
			}

		}

		if (j)
			RENDER_FLUSH;

		if (mark || srch_mark)
		{
			xtext_set_bg(xtext, gc, xtext->col_back);
			xtext_set_fg(xtext, gc, xtext->col_fore);
			if (xtext->col_back != XTEXT_BG)
				xtext->backcolor = true;
			else
				xtext->backcolor = false;
		}

		/* draw background to the right of the text */
		if (!left_only && !xtext->dont_render)
		{
			/* draw separator now so it doesn't appear to flicker */
			gtk_xtext_draw_sep(xtext, y - xtext->font->ascent);
			if (!xtext->skip_border_fills && xtext->clip_x2 >= x)
			{
				int xx = std::max(x, xtext->clip_x);

				xtext_draw_bg(xtext,
					xx,	/* x */
					y - xtext->font->ascent, /* y */
					std::min(xtext->clip_x2 - xx, (win_width + MARGIN) - xx), /* width */
					xtext->fontsize);		/* height */
			}
		}

		xtext->dont_render2 = false;

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
	int gtk_xtext_find_subline(GtkXText *, textentry *ent, int line)
	{
		int rlen = 0;

		if (line > 0)
		{
			rlen = ent->sublines[line - 1];
			if (rlen == 0)
				rlen = ent->str.size();
		}
		return rlen;
	}

	/* horrible hack for drawing time stamps */

	void gtk_xtext_render_stamp(GtkXText * xtext, textentry * ent,
		const boost::string_ref & text, int line, int win_width)
	{

		int jo, ji, hs;
		int xsize, y, emphasis;

		/* trashing ent here, so make a backup first */
		textentry tmp_ent(*ent);
		jo = xtext->jump_out_offset;	/* back these up */
		ji = xtext->jump_in_offset;
		hs = xtext->hilight_start;
		xtext->jump_out_offset = 0;
		xtext->jump_in_offset = 0;
		xtext->hilight_start = 0xffff;	/* temp disable */
		emphasis = 0;

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
			tmp_ent.str = ustring{ text.cbegin(), text.cend() };
		}

		y = (xtext->fontsize * line) + xtext->font->ascent - xtext->pixel_offset;
		gtk_xtext_render_str(xtext, y, &tmp_ent, (const unsigned char*)text.data(), text.size(),
			win_width, 2, line, true, &xsize, &emphasis);

		/* restore everything back to how it was */
		//(*ent) = std::move(tmp_ent);
		xtext->jump_out_offset = jo;
		xtext->jump_in_offset = ji;
		xtext->hilight_start = hs;

		/* with a non-fixed-width font, sometimes we don't draw enough
		background i.e. when this stamp is shorter than xtext->stamp_width */
		xsize += MARGIN;
		if (xsize < xtext->stamp_width)
		{
			y -= xtext->font->ascent;
			xtext_draw_bg(xtext,
				xsize,	/* x */
				y,			/* y */
				xtext->stamp_width - xsize,	/* width */
				xtext->fontsize					/* height */);
		}
	}

	/* render a single line, which may wrap to more lines */

	int gtk_xtext_render_line(GtkXText * xtext, textentry * ent, int line,
		int lines_max, int subline, int win_width)
	{
		const unsigned char *str;
		int indent, taken, entline, len, y, start_subline;
		int emphasis = 0;

		entline = taken = 0;
		str = ent->str.c_str();
		indent = ent->indent;
		start_subline = subline;

		/* draw the timestamp */
		if (xtext->auto_indent && xtext->buffer->time_stamp &&
			(!xtext->skip_stamp || xtext->mark_stamp || xtext->force_stamp))
		{
			auto time_str = xtext_get_stamp_str(ent->stamp);
			gtk_xtext_render_stamp(xtext, ent, time_str, line, win_width);
		}

		/* draw each line one by one */
		do
		{
			if (entline > 0)
				len = ent->sublines[entline] - ent->sublines[entline - 1];
			else
				len = ent->sublines[entline];

			entline++;

			y = (xtext->fontsize * line) + xtext->font->ascent - xtext->pixel_offset;
			if (!subline)
			{
				if (!gtk_xtext_render_str(xtext, y, ent, str, len, win_width,
					indent, line, false, nullptr, &emphasis))
				{
					/* small optimization */
					gtk_xtext_draw_marker(xtext, ent, y - xtext->fontsize * (taken + start_subline + 1));
					return ent->sublines.size() - subline;
				}
			}
			else
			{
				xtext->dont_render = true;
				gtk_xtext_render_str(xtext, y, ent, str, len, win_width,
					indent, line, false, nullptr, &emphasis);
				xtext->dont_render = false;
				subline--;
				line--;
				taken--;
			}

			indent = xtext->buffer->indent;
			line++;
			taken++;
			str += len;

			if (line >= lines_max)
				break;

		} while (str < ent->str.c_str() + ent->str.size());

		gtk_xtext_draw_marker(xtext, ent, y - xtext->fontsize * (taken + start_subline));

		return taken;
	}
} // end anonymous namespace

void
gtk_xtext_set_palette(GtkXText * xtext, GdkColor palette[])
{
	for (int i = (XTEXT_COLS - 1); i >= 0; i--)
	{
		xtext->palette[i] = palette[i];
	}

	if (gtk_widget_get_realized(GTK_WIDGET(xtext)))
	{
		xtext_set_fg(xtext, xtext->fgc, XTEXT_FG);
		xtext_set_bg(xtext, xtext->fgc, XTEXT_BG);
		xtext_set_fg(xtext, xtext->bgc, XTEXT_BG);

		gdk_gc_set_foreground(xtext->marker_gc, &xtext->palette[XTEXT_MARKER]);
	}
	xtext->col_fore = XTEXT_FG;
	xtext->col_back = XTEXT_BG;
}

namespace {

	static void
		gtk_xtext_fix_indent(xtext_buffer *buf)
	{
		/* make indent a multiple of the space width */
		if (buf->indent && buf->xtext->space_width)
		{
			int j = 0;
			while (j < buf->indent)
			{
				j += buf->xtext->space_width;
			}
			buf->indent = j;
		}

		dontscroll(buf);	/* force scrolling off */
	}

	static void
		gtk_xtext_recalc_widths(xtext_buffer *buf, bool do_str_width)
	{
		/* since we have a new font, we have to recalc the text widths */
		auto ent = buf->impl->text_first;
		while (ent)
		{
			if (do_str_width)
			{
				ent->str_width = gtk_xtext_text_width_ent(buf->xtext, ent);
			}
			if (ent->left_len != -1)
			{
				ent->indent =
					(buf->indent -
					gtk_xtext_text_width(buf->xtext, ustring_ref(ent->str.c_str(),
					ent->left_len))) - buf->xtext->space_width;
				if (ent->indent < MARGIN)
					ent->indent = MARGIN;
			}
			ent = ent->next;
		}

		gtk_xtext_calc_lines(buf, false);
	}
} // end anonymous namespace

bool gtk_xtext_set_font(GtkXText *xtext, const char name[])
{

	if (xtext->font)
		backend_font_close(xtext);

	/* realize now, so that font_open has a XDisplay */
	gtk_widget_realize(GTK_WIDGET(xtext));

	backend_font_open(xtext, name);
	if (xtext->font == nullptr)
		return false;

	xtext->fontsize = xtext->font->ascent + xtext->font->descent;

	{
		auto time_str = xtext_get_stamp_str(time(0));
		xtext->stamp_width =
			gtk_xtext_text_width(xtext, ustring_ref(reinterpret_cast<const unsigned char*>(time_str.c_str()), time_str.size())) + MARGIN;
	}

	gtk_xtext_fix_indent(xtext->buffer);

	if (gtk_widget_get_realized(GTK_WIDGET(xtext)))
		gtk_xtext_recalc_widths(xtext->buffer, true);

	return true;
}

void
gtk_xtext_set_background(GtkXText * xtext, GdkPixmap * pixmap)
{
	GdkGCValues val;

	if (xtext->pixmap)
	{
		g_object_unref(xtext->pixmap);
		xtext->pixmap = nullptr;
	}

	dontscroll(xtext->buffer);
	xtext->pixmap = pixmap;

	if (pixmap != 0)
	{
		g_object_ref(pixmap);
		if (gtk_widget_get_realized(GTK_WIDGET(xtext)))
		{
			gdk_gc_set_tile(xtext->bgc, pixmap);
			gdk_gc_set_ts_origin(xtext->bgc, 0, 0);
			xtext->ts_x = xtext->ts_y = 0;
			gdk_gc_set_fill(xtext->bgc, GDK_TILED);
		}
	}
	else if (gtk_widget_get_realized(GTK_WIDGET(xtext)))
	{
		g_object_unref(xtext->bgc);
		val.subwindow_mode = GDK_INCLUDE_INFERIORS;
		val.graphics_exposures = 0;
		xtext->bgc = gdk_gc_new_with_values(GTK_WIDGET(xtext)->window,
			&val, GDK_GC_EXPOSURES | GDK_GC_SUBWINDOW);
		xtext_set_fg(xtext, xtext->bgc, XTEXT_BG);
	}
}

void
gtk_xtext_save(GtkXText * xtext, int fh)
{
	textentry *ent;
	int newlen;

	ent = xtext->buffer->impl->text_first;
	while (ent)
	{
		glib_string buf((char*)gtk_xtext_strip_color(ent->str, nullptr,
			&newlen, nullptr, false));
		write(fh, buf.get(), newlen);
		write(fh, "\n", 1);
		ent = ent->next;
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
		int height = gdk_window_get_height(gtk_widget_get_window(GTK_WIDGET(buf->xtext)));
		int width = gdk_window_get_width(gtk_widget_get_window(GTK_WIDGET(buf->xtext)));
		width -= MARGIN;

		if (width < 30 || height < buf->xtext->fontsize || width < buf->indent + 30)
			return;

		int lines = 0;
		auto ent = buf->impl->text_first;
		while (ent)
		{
			lines += gtk_xtext_lines_taken(buf, *ent);
			ent = ent->next;
		}

		buf->impl->pagetop_ent = nullptr;
		buf->num_lines = lines;
		gtk_xtext_adjustment_set(buf, fire_signal);
	}

	/* find the n-th line in the linked list, this includes wrap calculations */
	textentry * gtk_xtext_nth(GtkXText *xtext, int line, int *subline)
	{
		int lines = 0;
		textentry * ent = xtext->buffer->impl->text_first;

		/* -- optimization -- try to make a short-cut using the pagetop ent */
		if (xtext->buffer->impl->pagetop_ent)
		{
			if (line == xtext->buffer->pagetop_line)
			{
				*subline = xtext->buffer->pagetop_subline;
				return xtext->buffer->impl->pagetop_ent;
			}
			if (line > xtext->buffer->pagetop_line)
			{
				/* lets start from the pagetop instead of the absolute beginning */
				ent = xtext->buffer->impl->pagetop_ent;
				lines = xtext->buffer->pagetop_line - xtext->buffer->pagetop_subline;
			}
			else if (line > xtext->buffer->pagetop_line - line)
			{
				/* move backwards from pagetop */
				ent = xtext->buffer->impl->pagetop_ent;
				lines = xtext->buffer->pagetop_line - xtext->buffer->pagetop_subline;
				for (;;)
				{
					if (lines <= line)
					{
						*subline = line - lines;
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
				*subline = ent->sublines.size() - (lines - line);
				return ent;
			}
			ent = ent->next;
		}
		return nullptr;
	}

	/* render enta (or an inclusive range enta->entb) */
	int gtk_xtext_render_ents(GtkXText * xtext, textentry * enta, textentry * entb)
	{
		if (xtext->buffer->indent < MARGIN)
			xtext->buffer->indent = MARGIN;	  /* 2 pixels is our left margin */

		int height = gdk_window_get_height(gtk_widget_get_window(GTK_WIDGET(xtext)));
		int width = gdk_window_get_width(gtk_widget_get_window(GTK_WIDGET(xtext)));
		width -= MARGIN;

		if (width < 32 || height < xtext->fontsize || width < xtext->buffer->indent + 30)
			return 0;

		int lines_max = ((height + xtext->pixel_offset) / xtext->fontsize) + 1;
		int line = 0;
		auto orig_ent = xtext->buffer->impl->pagetop_ent;
		int subline = xtext->buffer->pagetop_subline;

		/* used before a complete page is in buffer */
		if (!orig_ent)
			orig_ent = xtext->buffer->impl->text_first;

		/* check if enta is before the start of this page */
		bool drawing = false;
		if (entb)
		{
			auto tmp_ent = orig_ent;
			while (tmp_ent)
			{
				if (tmp_ent == enta)
					break;
				if (tmp_ent == entb)
				{
					drawing = true;
					break;
				}
				tmp_ent = tmp_ent->next;
			}
		}

		auto ent = orig_ent;
		while (ent)
		{
			if (entb && ent == enta)
				drawing = true;

			if (drawing || ent == entb || ent == enta)
			{
				gtk_xtext_reset(xtext, false, true);
				line += gtk_xtext_render_line(xtext, ent, line, lines_max,
					subline, width);
				subline = 0;
				xtext->jump_in_offset = 0;	/* jump_in_offset only for the 1st */
			}
			else
			{
				if (ent == orig_ent)
				{
					line -= subline;
					subline = 0;
				}
				line += ent->sublines.size();
			}

			if (ent == entb)
				break;

			if (line >= lines_max)
				break;

			ent = ent->next;
		}

		/* space below last line */
		return (xtext->fontsize * line) - xtext->pixel_offset;
	}

	/* render a whole page/window, starting from 'startline' */
	void gtk_xtext_render_page(GtkXText * xtext)
	{
		int startline = xtext->adj->value;

		if (!gtk_widget_get_realized(GTK_WIDGET(xtext)))
			return;

		if (xtext->buffer->indent < MARGIN)
			xtext->buffer->indent = MARGIN;	  /* 2 pixels is our left margin */

		int width;
		int height;
		gdk_drawable_get_size(GTK_WIDGET(xtext)->window, &width, &height);

		if (width < 34 || height < xtext->fontsize || width < xtext->buffer->indent + 32)
			return;

		xtext->pixel_offset = (xtext->adj->value - startline) * xtext->fontsize;

		int subline = 0;
		int line = 0;
		auto ent = xtext->buffer->impl->text_first;

		if (startline > 0)
			ent = gtk_xtext_nth(xtext, startline, &subline);

		xtext->buffer->impl->pagetop_ent = ent;
		xtext->buffer->pagetop_subline = subline;
		xtext->buffer->pagetop_line = startline;

		if (xtext->buffer->num_lines <= xtext->adj->page_size)
			dontscroll(xtext->buffer);

		int pos = xtext->adj->value * xtext->fontsize;
		auto overlap = xtext->buffer->last_pixel_pos - pos;
		xtext->buffer->last_pixel_pos = pos;

#ifndef __APPLE__
		if (!xtext->pixmap && std::abs(overlap) < height)
		{
			GdkRectangle area;

			/* so the obscured regions are exposed */
			gdk_gc_set_exposures(xtext->fgc, true);
			if (overlap < 1)	/* DOWN */
			{
				gdk_draw_drawable(xtext->draw_buf, xtext->fgc, xtext->draw_buf,
					0, -overlap, 0, 0, width, height + overlap);
				auto remainder = ((height - xtext->font->descent) % xtext->fontsize) +
					xtext->font->descent;
				area.y = (height + overlap) - remainder;
				area.height = remainder - overlap;
			}
			else
			{
				gdk_draw_drawable(xtext->draw_buf, xtext->fgc, xtext->draw_buf,
					0, 0, 0, overlap, width, height - overlap);
				area.y = 0;
				area.height = overlap;
			}
			gdk_gc_set_exposures(xtext->fgc, false);

			if (area.height > 0)
			{
				area.x = 0;
				area.width = width;
				gtk_xtext_paint(GTK_WIDGET(xtext), &area);
			}

			return;
		}
#endif

		width -= MARGIN;
		auto lines_max = ((height + xtext->pixel_offset) / xtext->fontsize) + 1;

		while (ent)
		{
			gtk_xtext_reset(xtext, false, true);
			line += gtk_xtext_render_line(xtext, ent, line, lines_max,
				subline, width);
			subline = 0;

			if (line >= lines_max)
				break;

			ent = ent->next;
		}

		line = (xtext->fontsize * line) - xtext->pixel_offset;
		/* fill any space below the last line with our background GC */
		xtext_draw_bg(xtext, 0, line, width + MARGIN, height - line);

		/* draw the separator line */
		gtk_xtext_draw_sep(xtext, -1);
	}
} // end anonymous namespace

void
gtk_xtext_refresh(GtkXText * xtext)
{
	if (gtk_widget_get_realized(GTK_WIDGET(xtext)))
	{
		gtk_xtext_render_page(xtext);
	}
}

namespace{

	bool gtk_xtext_kill_ent(xtext_buffer *buffer, textentry *ent)
	{
		//std::unique_ptr<textentry> entry(ent);
		/* Set visible to true if this is the current buffer */
		/* and this ent shows up on the screen now */
		bool visible = buffer->xtext->buffer == buffer &&
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
		buffer->last_pixel_pos -= (ent->sublines.size() * buffer->xtext->fontsize);
		buffer->impl->text_first = ent->next;
		if (buffer->impl->text_first)
			buffer->impl->text_first->prev = nullptr;
		else
			buffer->impl->text_last = nullptr;

		buffer->old_value -= ent->sublines.size();
		if (buffer->xtext->buffer == buffer)	/* is it the current buffer? */
		{
			buffer->xtext->adj->value -= ent->sublines.size();
			buffer->xtext->select_start_adj -= ent->sublines.size();
		}

		if (gtk_xtext_kill_ent(buffer, ent))
		{
			if (!buffer->xtext->add_io_tag)
			{
				/* remove scrolling events */
				if (buffer->xtext->io_tag)
				{
					g_source_remove(buffer->xtext->io_tag);
					buffer->xtext->io_tag = 0;
				}
				buffer->xtext->force_render = true;
				buffer->xtext->add_io_tag = g_timeout_add(REFRESH_TIMEOUT * 2,
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

		if (gtk_xtext_kill_ent(buffer, ent))
		{
			if (!buffer->xtext->add_io_tag)
			{
				/* remove scrolling events */
				if (buffer->xtext->io_tag)
				{
					g_source_remove(buffer->xtext->io_tag);
					buffer->xtext->io_tag = 0;
				}
				buffer->xtext->force_render = true;
				buffer->xtext->add_io_tag = g_timeout_add(REFRESH_TIMEOUT * 2,
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
		if (buf->xtext->auto_indent)
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

	if (buf->xtext->buffer == buf)
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
		textentry *ent;
		int lines;
		xtext_buffer *buf = xtext->buffer;
		int height;

		if (!find_ent)
		{
			return false;
		}

		height = gdk_window_get_height(gtk_widget_get_window(GTK_WIDGET(xtext)));

		ent = buf->impl->pagetop_ent;
		/* If top line not completely displayed return false */
		if (ent == find_ent && buf->pagetop_subline > 0)
		{
			return false;
		}
		/* Loop through line positions looking for find_ent */
		lines = ((height + xtext->pixel_offset) / xtext->fontsize) + buf->pagetop_subline + add;
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
	if (gtk_xtext_check_ent_visibility(xtext, xtext->buffer->impl->marker_pos, 1))
		xtext->buffer->marker_seen = true;
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
		auto meta = slp.cbegin();
		for (; meta != slp.cend(); ++meta)
		{
			if (end < meta->len)
			{
				off2 = meta->off + end;
				break;
			}
			curlen += meta->len;
			end -= meta->len;
		}
		if (meta == slp.cend())
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
		auto str = (gchar*)gtk_xtext_strip_color(ent.str, buf->xtext->scratch_buffer,
			&lstr, &slp, !buf->xtext->ignore_hidden);
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

	xtext_buffer *buf = xtext->buffer;


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
		GtkAdjustment *adj = xtext->adj;
		gdouble value;

		buf->impl->pagetop_ent = nullptr;
		for (value = 0.0, ent = buf->impl->text_first;
			ent && ent != buf->hintsearch; ent = ent->next)
		{
			value += ent->sublines.size();
		}
		if (value > adj->upper - adj->page_size)
		{
			value = adj->upper - adj->page_size;
		}
		else if ((flags & backward) && ent)
		{
			value -= adj->page_size - ent->sublines.size();
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
		GtkAdjustment *adj = xtext->adj;

		xtext->add_io_tag = 0;

		/* less than a complete page? */
		if (xtext->buffer->num_lines <= adj->page_size)
		{
			xtext->buffer->old_value = 0.0;
			adj->value = 0.0;
			gtk_xtext_render_page(xtext);
		}
		else if (xtext->buffer->scrollbar_down)
		{
			g_signal_handler_block(xtext->adj, xtext->vc_signal_tag);
			gtk_xtext_adjustment_set(xtext->buffer, false);
			gtk_adjustment_set_value(adj, adj->upper - adj->page_size);
			g_signal_handler_unblock(xtext->adj, xtext->vc_signal_tag);
			xtext->buffer->old_value = adj->value;
			gtk_xtext_render_page(xtext);
		}
		else
		{
			gtk_xtext_adjustment_set(xtext->buffer, true);
			if (xtext->force_render)
			{
				xtext->force_render = false;
				gtk_xtext_render_page(xtext);
			}
		}

		return 0;
	}

	/* append a textentry to our linked list */

	static void gtk_xtext_append_entry(xtext_buffer *buf, textentry* ent, time_t stamp)
	{
		/* we don't like tabs */
		std::replace(ent->str.begin(), ent->str.end(), '\t', ' ');

		ent->stamp = stamp;
		if (stamp == 0)
			ent->stamp = time(nullptr);
		ent->str_width = gtk_xtext_text_width_ent(buf->xtext, ent);
		ent->mark_start = -1;
		ent->mark_end = -1;
		ent->next = nullptr;
		ent->marks = nullptr;

		if (ent->indent < MARGIN)
			ent->indent = MARGIN;	  /* 2 pixels is the left margin */

		/* append to our linked list */
		if (buf->impl->text_last)
			buf->impl->text_last->next = ent;
		else
			buf->impl->text_first = ent;
		ent->prev = buf->impl->text_last;
		buf->impl->text_last = ent;

		buf->num_lines += gtk_xtext_lines_taken(buf, *ent);

		if ((buf->impl->marker_pos == nullptr || buf->marker_seen) && (buf->xtext->buffer != buf ||
			!gtk_window_has_toplevel_focus(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(buf->xtext))))))
		{
			buf->impl->marker_pos = ent;
			buf->impl->marker_state = MARKER_IS_SET;
			dontscroll(buf); /* force scrolling off */
			buf->marker_seen = false;
		}

		if (buf->xtext->max_lines > 2 && buf->xtext->max_lines < buf->num_lines)
		{
			gtk_xtext_remove_top(buf);
		}

		if (buf->xtext->buffer == buf)
		{
			/* this could be improved */
			if ((buf->num_lines - 1) <= buf->xtext->adj->page_size)
				dontscroll(buf);

			if (!buf->xtext->add_io_tag)
			{
				/* remove scrolling events */
				if (buf->xtext->io_tag)
				{
					g_source_remove(buf->xtext->io_tag);
					buf->xtext->io_tag = 0;
				}
				buf->xtext->add_io_tag = g_timeout_add(REFRESH_TIMEOUT * 2,
					(GSourceFunc)
					gtk_xtext_render_page_timeout,
					buf->xtext);
			}
		}
		if (buf->scrollbar_down)
		{
			buf->old_value = buf->num_lines - buf->xtext->adj->page_size;
			if (buf->old_value < 0.0)
				buf->old_value = 0.0;
		}
		if (buf->search_flags & follow)
		{
			auto gl = gtk_xtext_search_textentry(buf, *ent);
			gtk_xtext_search_textentry_add(buf, ent, gl, false);
		}
	}

} // end anonymous namespace

/* the main two public functions */

void
gtk_xtext_append_indent(xtext_buffer *buf,
const unsigned char left_text[], int left_len,
const unsigned char right_text[], int right_len,
time_t stamp)
{
	int space;
	int tempindent;
	int left_width;

	if (left_len == -1)
		left_len = std::char_traits<unsigned char>::length(left_text);

	if (right_len == -1)
		right_len = std::char_traits<unsigned char>::length(right_text);

	if (right_len >= sizeof(buf->xtext->scratch_buffer))
		right_len = sizeof(buf->xtext->scratch_buffer) - 1;

	if (right_text[right_len - 1] == '\n')
		right_len--;

	buf->impl->entries.emplace_back();
	textentry * ent = &buf->impl->entries.back();
	//std::unique_ptr<textentry> ent{ new textentry };
	ent->str.resize(left_len + right_len + 1, '\0');
	auto str = ent->str.begin();
	if (left_text)
		std::copy_n(left_text, left_len, str);
	str[left_len] = ' ';
	std::copy_n(right_text, right_len, str + left_len + 1);

	left_width = gtk_xtext_text_width(buf->xtext, ustring_ref(left_text, left_len));

	ent->left_len = left_len;
	ent->indent = (buf->indent - left_width) - buf->xtext->space_width;

	if (buf->time_stamp)
		space = buf->xtext->stamp_width;
	else
		space = 0;

	/* do we need to auto adjust the separator position? */
	if (buf->xtext->auto_indent && ent->indent < MARGIN + space)
	{
		tempindent = MARGIN + space + buf->xtext->space_width + left_width;

		if (tempindent > buf->indent)
			buf->indent = tempindent;

		if (buf->indent > buf->xtext->max_auto_indent)
			buf->indent = buf->xtext->max_auto_indent;

		gtk_xtext_fix_indent(buf);
		gtk_xtext_recalc_widths(buf, false);

		ent->indent = (buf->indent - left_width) - buf->xtext->space_width;
		buf->xtext->force_render = true;
	}

	gtk_xtext_append_entry(buf, ent, stamp);
}

void
gtk_xtext_append(xtext_buffer *buf, boost::string_ref text, time_t stamp)
{
	if (text.back() == '\n')
		text.remove_suffix(text.size() - 1);

	if (text.size() >= sizeof(buf->xtext->scratch_buffer))
		text.remove_suffix(sizeof(buf->xtext->scratch_buffer) - 1);

	buf->impl->entries.emplace_back();
	textentry * ent = &buf->impl->entries.back();
	//std::unique_ptr<textentry> ent{ new textentry };
	if (!text.empty())
	{
		ent->str = ustring{ text.cbegin(), text.cend() };
	}
	ent->indent = 0;
	ent->left_len = -1;

	gtk_xtext_append_entry(buf, ent, stamp);
}

bool gtk_xtext_is_empty(const xtext_buffer &buf)
{
	return buf.impl->text_first == nullptr;
}


int
gtk_xtext_lastlog(xtext_buffer *out, xtext_buffer *search_area)
{
	auto ent = search_area->impl->text_first;
	int matches = 0;

	while (ent)
	{
		auto gl = gtk_xtext_search_textentry(out, *ent);
		if (gl)
		{
			matches++;
			/* copy the text over */
			if (search_area->xtext->auto_indent)
			{
				gtk_xtext_append_indent(out, ent->str.c_str(), ent->left_len,
					ent->str.c_str() + ent->left_len + 1,
					ent->str.size() - ent->left_len - 1, 0);
			}
			else
			{
				gtk_xtext_append(out, boost::string_ref{ reinterpret_cast<const char*>(ent->str.c_str()), ent->str.size() }, 0);
			}

			out->impl->text_last->stamp = ent->stamp;
			gtk_xtext_search_textentry_add(out, out->impl->text_last, gl, true);
		}
		ent = ent->next;
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
gtk_xtext_set_time_stamp(xtext_buffer *buf, gboolean time_stamp)
{
	buf->time_stamp = !!time_stamp;
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

	buf->impl->marker_pos = buf->impl->text_last;
	buf->impl->marker_state = MARKER_IS_SET;
}

void
gtk_xtext_reset_marker_pos(GtkXText *xtext)
{
	if (xtext->buffer->impl->marker_pos)
	{
		xtext->buffer->impl->marker_pos = nullptr;
		dontscroll(xtext->buffer); /* force scrolling off */
		gtk_xtext_render_page(xtext);
		xtext->buffer->impl->marker_state = MARKER_RESET_MANUALLY;
	}
}

int
gtk_xtext_moveto_marker_pos(GtkXText *xtext)
{
	xtext_buffer *buf = xtext->buffer;
	if (buf->impl->marker_pos == nullptr)
		return buf->impl->marker_state;

	if (gtk_xtext_check_ent_visibility(xtext, buf->impl->marker_pos, 1) == false)
	{
		textentry *ent = buf->impl->text_first;
		GtkAdjustment *adj = xtext->adj;
		gdouble value = 0.0;
		while (ent)
		{
			if (ent == buf->impl->marker_pos)
				break;
			value += ent->sublines.size();
			ent = ent->next;
		}
		if (value >= adj->value && value < adj->value + adj->page_size)
			return MARKER_IS_SET;
		value -= adj->page_size / 2.0;
		if (value < 0.0)
			value = 0.0;
		if (value > adj->upper - adj->page_size)
			value = adj->upper - adj->page_size;
		gtk_adjustment_set_value(adj, value);
		gtk_xtext_render_page(xtext);
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

	if (!gtk_widget_get_realized(GTK_WIDGET(xtext)))
		gtk_widget_realize(GTK_WIDGET(xtext));

	int h = gdk_window_get_height(gtk_widget_get_window(GTK_WIDGET(xtext)));
	int w = gdk_window_get_width(gtk_widget_get_window(GTK_WIDGET(xtext)));

	/* after a font change */
	if (buf->needs_recalc)
	{
		buf->needs_recalc = false;
		gtk_xtext_recalc_widths(buf, true);
	}

	/* now change to the new buffer */
	xtext->buffer = buf;
	dontscroll(buf);	/* force scrolling off */
	xtext->adj->value = buf->old_value;
	xtext->adj->upper = buf->num_lines;
	if (xtext->adj->upper == 0.0)
		xtext->adj->upper = 1.0;
	/* sanity check */
	else if (xtext->adj->value > xtext->adj->upper - xtext->adj->page_size)
	{
		/*buf->pagetop_ent = nullptr;*/
		xtext->adj->value = xtext->adj->upper - xtext->adj->page_size;
		if (xtext->adj->value < 0.0)
			xtext->adj->value = 0.0;
	}

	if (render)
	{
		/* did the window change size since this buffer was last shown? */
		if (buf->window_width != w)
		{
			buf->window_width = w;
			gtk_xtext_calc_lines(buf, false);
			if (buf->scrollbar_down)
				gtk_adjustment_set_value(xtext->adj, xtext->adj->upper -
				xtext->adj->page_size);
		}
		else if (buf->window_height != h)
		{
			buf->window_height = h;
			buf->impl->pagetop_ent = nullptr;
			if (buf->scrollbar_down)
				xtext->adj->value = xtext->adj->upper;
			gtk_xtext_adjustment_set(buf, false);
		}

		gtk_xtext_render_page(xtext);
		gtk_adjustment_changed(xtext->adj);
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

	num_lines(), indent(parent->space_width * 2), /* position of separator (pixels) from left */

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

	/*auto ent = this->text_first;
	while (ent)
	{
		auto next = ent->next;
		std::unique_ptr<textentry> ent_ptr(ent);
		ent = next;
	}*/
}

void
gtk_xtext_buffer_free(xtext_buffer *buf)
{
	std::unique_ptr<xtext_buffer> buf_ptr(buf);
	if (buf->xtext->buffer == buf)
		buf->xtext->buffer = buf->xtext->orig_buffer;

	if (buf->xtext->selection_buffer == buf)
		buf->xtext->selection_buffer = nullptr;
}
