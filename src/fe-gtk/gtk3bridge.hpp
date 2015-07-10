/* HexChat
* Copyright (C) 2015 Leetsoftwerx.
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

#ifndef HEXCHAT_GTK_3_BRIDGE_HPP
#define HEXCHAT_GTK_3_BRIDGE_HPP

#include <gtk/gtk.h>
struct BridgeStyleContext;

BridgeStyleContext *
gtk_style_context_new(void);

void bridge_style_context_free(BridgeStyleContext*);
void bridge_set_foreground(BridgeStyleContext* context, GdkColor * col);
void bridge_set_background(BridgeStyleContext* context, GdkColor * col);

void
gtk_render_layout(BridgeStyleContext *context,
cairo_t         *cr,
gdouble          x,
gdouble          y,
PangoLayout     *layout);

void
gtk_render_background(BridgeStyleContext *context,
cairo_t         *cr,
gdouble          x,
gdouble          y,
gdouble          width,
gdouble          height);


#endif // HEXCHAT_GTK_3_BRIDGE_HPP