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
#include <string_view>
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
	using ustring_ref = std::basic_string_view<unsigned char>; //boost::basic_string_ref<unsigned char>;

	/* For use by gtk_xtext_strip_color() and its callers -- */
	struct offlen_t {
		guint16 off;
		guint16 len;
		guint16 emph;
		guint16 width;
	};

	struct text_range {
		std::uint32_t start;
		std::uint32_t end;
	};
	enum align {
		left,
		center,
		right
	};

	struct point2d {
		std::uint32_t x;
		std::uint32_t y;
	};
	struct xtext_backend;
	/**
	 Represents a single rectangle paragraph of text
	 */
	struct layout {
		virtual ~layout() = default;
		virtual std::uint32_t width() const noexcept = 0;
		virtual std::uint32_t line_count() const noexcept = 0;
		virtual int index_for_location(::xtext::point2d loc) = 0;
		virtual int line_from_index(std::uint32_t index) = 0;
		virtual std::string_view text() const noexcept = 0;
		virtual void set_width(std::uint32_t new_width) = 0;
		virtual void set_marks(gsl::span<text_range> marks) = 0;
		virtual void set_alignment(align align_to) = 0;
		virtual void clear_marks() = 0;
		virtual void invalidate(gsl::not_null<::xtext::xtext_backend*> backend) = 0;
	};
	
	struct renderer {
		virtual ~renderer() = default;
		virtual void begin_rendering() = 0;
		virtual void end_rendering() = 0;
		virtual void render_layout_at(point2d loc, layout* target) = 0;
	};

	struct xtext_backend
	{
		virtual ~xtext_backend() = default;

		// Sets the default font for use on laying out text
		virtual bool set_default_font(const std::string_view& defaultFont) = 0;
		// Gets the width of text laid out
		virtual int get_string_width(const xtext::ustring_ref& text, int strip_hidden) = 0;

		virtual void set_palette(const gsl::span<GdkColor, XTEXT_COLS> colors) = 0;

		virtual int render_at(cairo_t* cr, int x, int y, int width, int indent, int mark_start, int mark_end, align alignment, const xtext::ustring_ref& text) = 0;
		virtual std::unique_ptr<renderer> make_renderer(cairo_t * cr) = 0;
		virtual std::unique_ptr<layout> make_layout(const xtext::ustring_ref text, std::uint32_t max_width) = 0;
	};

	std::unique_ptr<xtext_backend> create_backend(const std::string_view & defaultFont, GtkWidget* parentWidget);
	ustring strip_color(const ustring_ref &text,
		std::vector<offlen_t> *slp,
		int strip_hidden);
}
#endif // HEXCHAT_XTEXT_BACKEND_HPP
