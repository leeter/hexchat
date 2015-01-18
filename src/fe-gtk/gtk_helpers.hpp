/* HexChat
* Copyright (C) 2014 Leetsoftwerx.
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

#ifndef HEXCHAT_GTK_HELPERS_HPP
#define HEXCHAT_GTK_HELPERS_HPP
#include <memory>
#include <gtk/gtk.h>

#ifndef NOEXCEPT
#if defined(_MSC_VER) && _MSC_VER < 1900
#define NOEXCEPT throw()
#else
#define NOEXCEPT noexcept
#endif
#endif

#ifndef CONSTEXPR
#if defined(_MSC_VER) && _MSC_VER < 1900
#define CONSTEXPR
#else
#define CONSTEXPR constexpr
#endif
#endif

CONSTEXPR inline GtkAttachOptions operator|(GtkAttachOptions a, GtkAttachOptions b) NOEXCEPT
{
	return static_cast<GtkAttachOptions>(static_cast<std::underlying_type<GtkAttachOptions>::type >(a) | static_cast<std::underlying_type<GtkAttachOptions>::type>(b));
}

CONSTEXPR inline GdkGCValuesMask operator|(GdkGCValuesMask a, GdkGCValuesMask b) NOEXCEPT
{
	return static_cast<GdkGCValuesMask>(static_cast<int>(a) | static_cast<int>(b));
}

CONSTEXPR inline GSignalFlags operator|(GSignalFlags a, GSignalFlags b) NOEXCEPT
{
	return static_cast<GSignalFlags>(static_cast<int>(a) | static_cast<int>(b));
}

#define CUSTOM_PTR_DELETER(type, del) \
	struct type##deleter \
	{\
		void operator()(type * ptr) NOEXCEPT\
		{\
			del(ptr); \
		}\
	};

#define CUSTOM_PTR(type, del) CUSTOM_PTR_DELETER(type, del) \
typedef std::unique_ptr<type, type##deleter> type ## Ptr;

CUSTOM_PTR(GtkTreeIter, gtk_tree_iter_free)
CUSTOM_PTR(GtkTreePath, gtk_tree_path_free)

#endif