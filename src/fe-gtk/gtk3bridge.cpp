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

#include "precompile.hpp"
#include "gtk3bridge.hpp"
#include "gtk_helpers.hpp"

struct BridgeStyleContext{
#if GTK_CHECK_VERSION(3, 0, 0)
	int foo;
#else
	GdkColor*fg_color;
	GdkColor*bg_color;
#endif
};
#if !GTK_CHECK_VERSION(3, 0, 0)
/// half this code is blatantly taken from GTK3 and is only being used as a bridge
/// until we can upgrade properly, hence the layout of parameters is preserved
namespace {
static void
prepare_context_for_layout(cairo_t *cr,
gdouble x,
gdouble y,
PangoLayout *layout) noexcept
{
	auto matrix = pango_context_get_matrix(pango_layout_get_context(layout));

	cairo_move_to(cr, x, y);

	if (matrix)
	{
		cairo_matrix_t cairo_matrix;

		cairo_matrix_init(&cairo_matrix,
			matrix->xx, matrix->yx,
			matrix->xy, matrix->yy,
			matrix->x0, matrix->y0);

		cairo_transform(cr, &cairo_matrix);
	}
}

static void
do_render_layout(BridgeStyleContext *context,
cairo_t         *cr,
gdouble          x,
gdouble          y,
PangoLayout     *layout) noexcept
{
	cairo_stack stack{ cr };
	const auto fg_color = context->fg_color;

	prepare_context_for_layout(cr, x, y, layout);

	/*_gtk_css_shadows_value_paint_layout(_gtk_style_context_peek_property(context, GTK_CSS_PROPERTY_TEXT_SHADOW),
		cr, layout);*/

	gdk_cairo_set_source_color(cr, fg_color);
	pango_cairo_show_layout(cr, layout);
}
}

BridgeStyleContext*
gtk_style_context_new(void)
{
	return new BridgeStyleContext();
}

void bridge_style_context_free(BridgeStyleContext* context) noexcept
{
	delete context;
}

void bridge_set_foreground(BridgeStyleContext* context, GdkColor * col) noexcept
{
	context->fg_color = col;
}

void bridge_set_background(BridgeStyleContext* context, GdkColor * col) noexcept
{
	context->bg_color = col;
}

const GdkColor* bridge_get_foreground(const BridgeStyleContext* context) noexcept
{
	return context->fg_color;
}

const GdkColor* bridge_get_background(const BridgeStyleContext* context) noexcept
{
	return context->bg_color;
}

void
gtk_render_layout(BridgeStyleContext *context,
cairo_t         *cr,
gdouble          x,
gdouble          y,
PangoLayout     *layout) noexcept
{
	g_return_if_fail(PANGO_IS_LAYOUT(layout));
	g_return_if_fail(cr != NULL);

	cairo_stack stack{ cr };
	cairo_new_path(cr);

	do_render_layout(context, cr, x, y, layout);
}



void
gtk_render_background(BridgeStyleContext *context,
cairo_t         *cr,
gdouble          x,
gdouble          y,
gdouble          width,
gdouble          height) noexcept
{
	g_return_if_fail(cr != NULL);

	if (width <= 0 || height <= 0)
		return;

	cairo_stack stack{ cr };
	cairo_new_path(cr);

	{
		cairo_stack stack_1{ cr };
		cairo_translate(cr, x, y);
		gdk_cairo_set_source_color(cr, context->bg_color);
		cairo_rectangle(cr, 0.0, 0.0, width, height);
		cairo_fill(cr);
	}
}
#endif