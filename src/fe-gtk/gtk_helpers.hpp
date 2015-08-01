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
#include <type_traits>
#include "../common/bitmask_operators.hpp"
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

#ifndef CONSTEXPR_OR_CONST
#if defined(_MSC_VER) && _MSC_VER < 1900
#define CONSTEXPR_OR_CONST const
#else
#define CONSTEXPR_OR_CONST constexpr
#endif
#endif



#if !GTK_CHECK_VERSION(3, 0, 0)
template<> struct enable_bitmask_operators<GtkAttachOptions>{
	static CONSTEXPR_OR_CONST bool enable = true;
};
template<> struct enable_bitmask_operators<GdkGCValuesMask>{
	static CONSTEXPR_OR_CONST bool enable = true;
};
#endif
template<> struct enable_bitmask_operators<GSignalFlags>{
	static CONSTEXPR_OR_CONST bool enable = true;
};

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