/* Hexchat
* Copyright (C) 2017 leetsoftwerx.
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

#ifndef HEXCHAT_PRECOMPILED_HEADER
#define HEXCHAT_PRECOMPILED_HEADER

#pragma once
#define _FILE_OFFSET_BITS 64 /* allow selection of large files */
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <io.h>
#else
#include <unistd.h>
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define GDK_MULTIHEAD_SAFE

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <exception>
#include <fcntl.h>
#include <functional>
#include <iterator>
#include <limits>
#include <locale>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/types.h>
#include <sys/stat.h>
#include <type_traits>
#include <vector>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/format.hpp>
#include <boost/optional.hpp>
#include <boost/utility/string_ref.hpp>
#include <boost/algorithm/string/iter_find.hpp>
#include <boost/algorithm/string/finder.hpp>
#include <boost/system/error_code.hpp>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <libintl.h>
#include <glib-object.h>
#include <cairo.h>
#include "gtk_helpers.hpp"

CUSTOM_PTR(cairo_t, cairo_destroy)
CUSTOM_PTR(cairo_surface_t, cairo_surface_destroy)
CUSTOM_PTR(GtkWidget, gtk_widget_destroy)

struct cairo_stack {
	cairo_t* const _cr;
	cairo_stack(cairo_stack&) = delete;
	cairo_stack(cairo_t* cr) noexcept
		:_cr(cr)
	{
		cairo_save(_cr);
	}
	~cairo_stack() noexcept
	{
		cairo_restore(_cr);
	}
};

#if defined (WIN32) || defined (__APPLE__)
#include <pango/pangocairo.h>
#endif

#endif // HEXCHAT_PRECOMPILED_HEADER