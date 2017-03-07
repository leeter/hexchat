/* HexChat
* Copyright (C) 2017 Leetsoftwerx
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

#ifndef HEXCHAT_XTEXT_BACKEND_HPP
#define HEXCHAT_XTEXT_BACKEND_HPP

#ifdef _MSC_VER
#pragma once
#endif

#include <cstddef>
#include <memory>
#include <vector>
#include <string>
#include <tuple>
#include <boost/utility/string_ref_fwd.hpp>
#include <gsl.h>
#include <gdk/gdk.h>
#include <pango/pango.h>
#include <cairo.h>
#include "xtext.hpp"
#include "gtk_helpers.hpp"

namespace xtext {
	CUSTOM_PTR(PangoAttrList, pango_attr_list_unref);
	CUSTOM_PTR(PangoLayout, g_object_unref);
	enum text_attr: unsigned char {
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
	enum emph {
		EMPH_ITAL = 1 << 0,
		EMPH_BOLD = 1 << 1,
		EMPH_HIDDEN = 1 << 2
	};
	using applied_emph = int;
	using ustring = std::basic_string<unsigned char>;
	using ustring_ref = boost::basic_string_ref<unsigned char>;

	/* For use by gtk_xtext_strip_color() and its callers -- */
	struct offlen_t {
		guint16 off;
		guint16 len;
		guint16 emph;
		guint16 width;
	};
	struct xtext_backend
	{
		enum align {
			left,
			center,
			right
		};
		virtual ~xtext_backend() = default;

		// Sets the default font for use on laying out text
		virtual bool set_default_font(const boost::string_ref& defaultFont) = 0;
		// Gets the width of text laid out
		virtual int get_string_width(const ustring_ref& text, int strip_hidden) = 0;

		virtual void set_palette(const gsl::span<GdkColor, XTEXT_COLS> colors) = 0;

		virtual int render_at(cairo_t* cr, int x, int y, int width, int indent, int mark_start, int mark_end, align alignment, const ustring_ref& text) = 0;
		virtual bool set_target(GdkWindow* window, GdkRegion* target_region, GdkRectangle rect) = 0;
	};

	std::unique_ptr<xtext_backend> create_backend(const boost::string_ref& defaultFont, GtkWidget* parentWidget);
	ustring strip_color(const ustring_ref &text,
		std::vector<offlen_t> *slp,
		int strip_hidden);
}
#endif // HEXCHAT_XTEXT_BACKEND_HPP
