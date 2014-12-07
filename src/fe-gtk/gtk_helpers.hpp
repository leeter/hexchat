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
inline GtkAttachOptions operator|(GtkAttachOptions a, GtkAttachOptions b)
{
	return static_cast<GtkAttachOptions>(static_cast<int>(a) | static_cast<int>(b));
}

inline GdkGCValuesMask operator|(GdkGCValuesMask a, GdkGCValuesMask b)
{
	return static_cast<GdkGCValuesMask>(static_cast<int>(a) | static_cast<int>(b));
}

inline GSignalFlags operator|(GSignalFlags a, GSignalFlags b)
{
	return static_cast<GSignalFlags>(static_cast<int>(a) | static_cast<int>(b));
}

struct gtk_tree_iter_deleter
{
	void operator()(GtkTreeIter * itr)
	{
		if (itr)
			gtk_tree_iter_free(itr);
	}
};

typedef std::unique_ptr<GtkTreeIter, gtk_tree_iter_deleter> GtkTreeIterPtr;

#endif