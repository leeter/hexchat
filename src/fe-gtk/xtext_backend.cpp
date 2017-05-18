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

#include "precompile.hpp"

#ifdef WIN32
#include "COMAssist.hpp"
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#endif

#include "xtext_backend.hpp"
#include "../common/charset_helpers.hpp"
#include "../common/bitmask_operators.hpp"

#define charlen(str) g_utf8_skip[(guchar)(*str)]
namespace
{
CUSTOM_PTR(PangoFontDescription, pango_font_description_free);
CUSTOM_PTR(PangoFontMetrics, pango_font_metrics_unref);
CUSTOM_PTR(PangoAttribute, pango_attribute_destroy);

std::tuple<int, int, std::ptrdiff_t>
parse_color_string(const std::string &color_str)
{
	std::locale locale;
	if (color_str.empty() || !std::isdigit(color_str[0], locale))
	{
		return std::make_tuple(XTEXT_FG, XTEXT_FG, 0);
	}
	char* fg_out = nullptr;
	const auto fg_index = std::strtol(color_str.c_str(), &fg_out, 10);
	const auto fg_dist = std::distance(color_str.c_str(), const_cast<const char*>(fg_out));
	if (static_cast<std::size_t>(fg_dist + 1) >= color_str.size() || color_str[fg_dist] != ',')
	{
		return std::make_tuple(fg_index, XTEXT_BG, fg_dist);
	}
	const auto bg_str = color_str.substr(fg_dist + 1);
	char*bg_out = nullptr;
	const auto bg_index = std::strtol(bg_str.c_str(), &bg_out, 10);
	const auto bg_dist = std::distance(bg_str.c_str(), const_cast<const char*>(bg_out));
	return std::make_tuple(fg_index, bg_index, fg_dist + bg_dist);
}

int handle_mirc_oddness(int color, int default) {
	if (color == 99) {	/* mIRC lameness */
		return default;
	}
	else if (color > XTEXT_MAX_COLOR) {
		return color % XTEXT_MIRC_COLS;
	}
	return color;
}

enum effect : std::uint32_t {
	none = 0,
	underline = 1 << 0,
	italic = 1 << 1, 
	bold = 1 << 2,
	fg_color = 1 << 3,
	bg_color = 1 << 4
};

template<> struct enable_bitmask_operators<effect> {
	static CONSTEXPR_OR_CONST bool enable = true;
};

struct text_effect {
	effect t_effect = effect::none;
	std::uint32_t start_index = 0;
	std::uint32_t length = 0;
	std::uint32_t fg_color = 0;
	std::uint32_t bg_color = 0;
};

void update_effects(text_effect& effect, std::vector<text_effect> & active_effects) {
	if (effect.length == 0) {
		return;
	}

	active_effects.push_back(effect);
	effect.length = 0;
}

std::pair<std::string, std::vector<text_effect>> strip_and_parse_effects(std::string_view text) 
{
	std::ostringstream outbuf;
	std::ostream_iterator<char> out{ outbuf };
	std::ptrdiff_t dist_from_start = 0;
	std::vector<text_effect> active_attrs;
	text_effect current = {};
	for (auto itr = text.cbegin(), end = text.cend(); itr != end;)
	{
		const int mbl = charlen(itr); /* multi-byte length */
		if (mbl > std::distance(itr, end))
			break; // bad UTF-8
		if (current.length == 0)
		{
			current.start_index = dist_from_start;
		}
		switch (*itr)
		{
		case xtext::ATTR_COLOR:
		{
			update_effects(current, active_attrs);
			++itr;
			if (itr == end)
			{
				break;
			}
			const auto color_str = std::string{ itr, end };
			int fg_color, bg_color;
			std::ptrdiff_t dist = 0;
			std::tie(fg_color, bg_color, dist) =
				parse_color_string(color_str);
			// is the string valid?
			if (dist == 0) {
				continue;
			}
			if (fg_color != XTEXT_FG)
			{
				fg_color = handle_mirc_oddness(fg_color, XTEXT_FG);
				current.t_effect |= effect::fg_color;
				current.fg_color = fg_color;
			}
			if (bg_color != XTEXT_BG)
			{
				bg_color = handle_mirc_oddness(bg_color, XTEXT_BG);
				current.t_effect |= effect::bg_color;
				current.bg_color = bg_color;
			}
			itr += dist;
			continue;
		}
		//case xtext::ATTR_BEEP:

		case xtext::ATTR_REVERSE:
			update_effects(current, active_attrs);
			current.t_effect |= effect::bg_color | effect::fg_color;
			current.fg_color = XTEXT_BG;
			current.bg_color = XTEXT_FG;
		break;
		case xtext::ATTR_BOLD:
			update_effects(current, active_attrs);
			current.t_effect |= effect::bold;
		break;
		case ATTR_UNDERLINE:
			update_effects(current, active_attrs);
			current.t_effect |= effect::underline;
		break;
		case xtext::ATTR_ITALICS:
			update_effects(current, active_attrs);
			current.t_effect |= effect::italic;
		break;
		case xtext::ATTR_HIDDEN:
			++itr;
			for (; itr != end && *itr != xtext::ATTR_HIDDEN; ++itr) { continue; }
			++itr;
			continue;
		case xtext::ATTR_RESET:
			update_effects(current, active_attrs);
			current.fg_color = 0;
			current.bg_color = 0;
			current.t_effect = effect::none;
		break;
		default:
			dist_from_start += mbl;
			current.length += mbl;
			std::copy_n(itr, mbl, out);
		}
		itr += mbl;
	}
	// ensure we always have at least one
	update_effects(current, active_attrs);
	return std::make_pair(outbuf.str(), std::move(active_attrs));
}

std::vector<text_effect> merge_marks(const gsl::span<const text_effect> current_effects, const gsl::span<xtext::text_range> marks) 
{
	std::vector<text_effect> final_marks(current_effects.size());
	if (marks.empty()) {
		final_marks.insert(final_marks.begin(), current_effects.cbegin(), current_effects.cend());
		return final_marks;
	}
	auto mark_iter = marks.cbegin();
	const auto marks_end = marks.cend();
	// deliberate value copy
	for (auto active : current_effects) {
		// already done with marks
		if (mark_iter == marks_end) {
			final_marks.emplace_back(active);
			continue;
		}
		
		const auto active_end = active.start_index + active.length;
		// no overlap
		if (mark_iter->start > active_end) {
			final_marks.emplace_back(active);
			continue;
		}
		// mark is exactly in our range or the current range is whole inside a mark
		if (mark_iter->start <= active.start_index && mark_iter->end >= active_end)  {
			active.t_effect |= effect::bg_color | effect::fg_color;
			active.bg_color = XTEXT_MARK_BG;
			active.fg_color = XTEXT_MARK_FG;
			final_marks.emplace_back(std::move(active));
			continue;
		}
		text_effect mark_effect = active;
		mark_effect.t_effect |= effect::bg_color | effect::fg_color;
		mark_effect.bg_color = XTEXT_MARK_BG;
		mark_effect.fg_color = XTEXT_MARK_FG;
		// mark starts inside our range
		if (mark_iter->start > active.start_index && mark_iter->start < active_end) {
			
			mark_effect.start_index = mark_iter->start;
			
			if (mark_iter->end >= active_end) {
				mark_effect.length = active_end - mark_iter->start;
				active.length = mark_iter->start - active.start_index;
			}
			// mark ends in our range
			else 
			{
				mark_effect.length = mark_iter->end - mark_iter->start;
				text_effect end_effect = active;
				end_effect.start_index = mark_iter->end;
				end_effect.length = active_end - mark_iter->end;
				final_marks.push_back(std::move(end_effect));
				++mark_iter;
			}
		}
		// mark ends inside our range
		else if (mark_iter->end < active_end) {
			mark_effect.length = mark_iter->end - mark_iter->start;
			active.start_index = mark_iter->end;
			active.length = active_end - mark_iter->end;
			++mark_iter;
		}
		final_marks.push_back(active);
		final_marks.push_back(mark_effect);
		
	}
	return final_marks;
}


#ifdef WIN32
namespace wrl = Microsoft::WRL;
template<typename T>
constexpr float channel_to_float(T channel) noexcept {
	return static_cast<float>(channel) / static_cast<float>(std::numeric_limits<T>::max());
}
D2D1::ColorF gdk_color_to_d2d(const GdkColor& color) noexcept {
	
	return D2D1::ColorF(
		channel_to_float(color.red),
		channel_to_float(color.green),
		channel_to_float(color.blue));
}

struct __declspec(uuid("704DD093-D18F-4A3A-A5CD-9BA38A72C60C")) d2d_coloreffect :
	public comhelp::ComBase<__uuidof(d2d_coloreffect), IUnknown, IUnknown> {
	int fg_color = XTEXT_FG;
	int bg_color = XTEXT_BG;
};

class __declspec(uuid("3F41F3FF-9FFC-4957-A359-0FC1D92F7BBB")) d2d_renderer :
	public comhelp::ComBase<__uuidof(d2d_renderer), IDWriteTextRenderer, IDWritePixelSnapping> {
	ID2D1RenderTarget * m_renderTarget;
	gsl::span<wrl::ComPtr<ID2D1Brush>, XTEXT_COLS> m_pallet;


	ID2D1Brush* get_fg_brush(IUnknown* clientDrawingEffect) {
		if (!clientDrawingEffect) {
			return m_pallet[XTEXT_FG].Get();
		}

		wrl::ComPtr<d2d_coloreffect> color_effect;
		clientDrawingEffect->QueryInterface(color_effect.GetAddressOf());
		if (color_effect) {
			return m_pallet[color_effect->fg_color].Get();
		}

		return m_pallet[XTEXT_FG].Get();
	}

	ID2D1Brush* get_bg_brush(IUnknown* clientDrawingEffect) {
		if (!clientDrawingEffect) {
			return nullptr;
		}

		wrl::ComPtr<d2d_coloreffect> color_effect;
		clientDrawingEffect->QueryInterface(color_effect.GetAddressOf());
		if (color_effect && color_effect->bg_color != XTEXT_BG) {
			return m_pallet[color_effect->bg_color].Get();
		}

		return nullptr;
	}

public:
	d2d_renderer(gsl::not_null<ID2D1RenderTarget*> render_target, gsl::span<wrl::ComPtr<ID2D1Brush>, XTEXT_COLS> pallet)
		:m_renderTarget(render_target)
		,m_pallet(pallet)
	{}

	STDMETHODIMP IsPixelSnappingDisabled(void * /*clientDrawingContext*/, BOOL * isDisabled) noexcept override final
	{
		if (!isDisabled) {
			return E_POINTER;
		}
		*isDisabled = false;
		return S_OK;
	}

	STDMETHODIMP GetCurrentTransform(void * /*clientDrawingContext*/, DWRITE_MATRIX * transform) noexcept override final
	{
		constexpr DWRITE_MATRIX ident = { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
		*transform = ident;
		return S_OK;
	}

	STDMETHODIMP GetPixelsPerDip(void * /*clientDrawingContext*/, FLOAT * pixelsPerDip) noexcept override final
	{
		if (!pixelsPerDip) {
			return E_POINTER;
		}
		FLOAT dpiX, dpiY;
		m_renderTarget->GetDpi(&dpiX, &dpiY);
		*pixelsPerDip = dpiX / 96.0f;
		return S_OK;
	}

	STDMETHODIMP DrawGlyphRun(
		void * /*clientDrawingContext*/,
		FLOAT baselineOriginX,
		FLOAT baselineOriginY,
		DWRITE_MEASURING_MODE measuringMode,
		DWRITE_GLYPH_RUN const * glyphRun,
		DWRITE_GLYPH_RUN_DESCRIPTION const * /*glyphRunDescription*/,
		IUnknown * clientDrawingEffect)
	{
		return comhelp::NoExceptBoundary([&, this] {
			ID2D1Brush* foreground_brush = this->get_fg_brush(clientDrawingEffect);
			ID2D1Brush* background_brush = this->get_bg_brush(clientDrawingEffect);

			if (background_brush) {
				// Get width of text
				float totalWidth = 0.0f;
				for (UINT32 index = 0; index < glyphRun->glyphCount; ++index)
				{
					totalWidth += glyphRun->glyphAdvances[index];
				}

				// Get height of text
				DWRITE_FONT_METRICS fontMetrics;
				glyphRun->fontFace->GetMetrics(&fontMetrics);
				const float adjust = glyphRun->fontEmSize / fontMetrics.designUnitsPerEm;
				const float ascent = adjust * fontMetrics.ascent;
				const float descent = adjust * fontMetrics.descent;
				const auto rect = D2D1::RectF(baselineOriginX,
					baselineOriginY - ascent,
					baselineOriginX + totalWidth,
					baselineOriginY + descent);

				// Fill Rectangle
				m_renderTarget->FillRectangle(rect, background_brush);
			}
			this->m_renderTarget->DrawGlyphRun(D2D1::Point2F(baselineOriginX, baselineOriginY), glyphRun, foreground_brush, measuringMode);
		});
	}

	STDMETHODIMP DrawUnderline(
		void * /*clientDrawingContext*/,
		FLOAT baselineOriginX,
		FLOAT baselineOriginY,
		DWRITE_UNDERLINE const * underline,
		IUnknown * clientDrawingEffect) noexcept override final
	{
		return comhelp::NoExceptBoundary([&, this] {
			ID2D1Brush* foreground_brush = this->get_fg_brush(clientDrawingEffect);
			this->m_renderTarget->FillRectangle(
				D2D1::RectF(
					baselineOriginX,
					baselineOriginY,
					baselineOriginX + underline->width,
					baselineOriginY + underline->thickness),
				foreground_brush);
		});

	}

	STDMETHODIMP DrawStrikethrough(
		void * /*clientDrawingContext*/,
		FLOAT baselineOriginX,
		FLOAT baselineOriginY,
		DWRITE_STRIKETHROUGH const * strikethrough,
		IUnknown * clientDrawingEffect) noexcept override final
	{
		return comhelp::NoExceptBoundary([&] {
			ID2D1Brush* foreground_brush = this->get_fg_brush(clientDrawingEffect);
			this->m_renderTarget->FillRectangle(
				D2D1::RectF(
					baselineOriginX,
					baselineOriginY,
					baselineOriginX + strikethrough->width,
					baselineOriginY + strikethrough->thickness),
				foreground_brush);
		});
	}

	STDMETHODIMP DrawInlineObject(
		void * /*clientDrawingContext*/,
		FLOAT /*originX*/,
		FLOAT /*originY*/,
		IDWriteInlineObject * /*inlineObject*/,
		BOOL /*isSideways*/,
		BOOL /*isRightToLeft*/,
		IUnknown * /*clientDrawingEffect*/) noexcept override final
	{
		return E_NOTIMPL;
		/*return comhelp::NoExceptBoundary([&] {
			throw comhelp::HResultException(E_NOTIMPL);
		});*/
	}
};




struct d2d_backend : public xtext::xtext_backend
{
	std::wstring default_font_name;
	wrl::ComPtr<ID2D1Factory> m_d2dfactory;
	wrl::ComPtr<IDWriteFactory> m_dwfactory;
	wrl::ComPtr<IDWriteTextFormat> m_currTextFormat;
	wrl::ComPtr<ID2D1DCRenderTarget> m_rt;
	wrl::ComPtr<d2d_renderer> m_render;
	std::array<GdkColor, XTEXT_COLS> m_gdk_pallet;
	std::array<wrl::ComPtr<ID2D1Brush>, XTEXT_COLS> m_d2d_pallet;


	class d2drenderer : public xtext::renderer {
		cairo_surface_t* m_cr;
		d2d_backend * m_parent;
		d2d_renderer * m_renderer;
		ID2D1RenderTarget* m_rt;
	public:

		d2drenderer(cairo_surface_t*cr, HDC target,const RECT& subrect, ID2D1DCRenderTarget* render_target, d2d_renderer * renderer, d2d_backend * parent)
			:m_cr(cr), m_parent(parent), m_renderer(renderer), m_rt(render_target) {
			render_target->BindDC(target, &subrect);
		}

		void begin_rendering() override final {
			m_rt->BeginDraw();
		}

		void end_rendering() override final {
			const auto hr = m_rt->EndDraw();
			if (hr == D2DERR_RECREATE_TARGET) {
				m_parent->create_render_target();
				m_parent->set_palette(m_parent->m_gdk_pallet);
				return;
			}
			cairo_surface_mark_dirty(m_cr);
		}

		void render_layout_at(xtext::point2d loc, xtext::layout* target) override final {
			auto layout = dynamic_cast<dwrite_layout*>(target);
			if (!layout) {
				return;
			}
			FLOAT dpiScaleX, dpiScaleY;
			m_rt->GetDpi(&dpiScaleX, &dpiScaleY);
			layout->layout()->Draw(nullptr, m_renderer, (loc.x * 96.0f) / dpiScaleX, (loc.y * 96.0f) / dpiScaleY);
			/*m_rt->DrawTextLayout(
				D2D1::Point2F(),
				layout->layout(),
				m_parent->m_d2d_pallet[XTEXT_FG].Get()
			);*/
		}
	};

	class dwrite_layout : public xtext::layout {
		std::uint32_t m_max_width;
		d2d_backend * m_backend;
		std::string m_original_text;
		std::string m_stripped_text;
		wrl::ComPtr<IDWriteTextLayout> m_layout;
		std::vector<xtext::text_range> m_marks;
		DWRITE_TEXT_METRICS m_metrics = {};
	public:

		dwrite_layout(d2d_backend* backend, xtext::ustring_ref text, std::uint32_t width, gsl::span<xtext::text_range> marks)
		:m_max_width(width), m_backend(backend),m_original_text(text.cbegin(), text.cend()),m_marks(marks.cbegin(), marks.cend())
		{
			invalidate(m_backend);
		}

		std::uint32_t width() const noexcept override final {
			return m_metrics.width;
		}

		std::uint32_t line_count() const noexcept override final {
			return m_metrics.lineCount;
		}

		void invalidate(gsl::not_null<::xtext::xtext_backend*> backend) override final {
			const auto local_backend = dynamic_cast<d2d_backend*>(backend.get());
			if (!local_backend) {
				return;
			}
			m_backend = local_backend;
			auto result = strip_and_parse_effects(m_original_text);
			const auto final_effects = merge_marks(result.second, m_marks);
			const auto wstr = charset::widen(result.first);
			wrl::ComPtr<IDWriteTextLayout> layout;
			m_backend->m_dwfactory->CreateTextLayout(
				wstr.c_str(),
				wstr.size(),
				m_backend->m_currTextFormat.Get(),
				static_cast<float>(m_max_width),
				std::numeric_limits<FLOAT>::max(),
				&layout);

			for (const auto & text_effect : final_effects) {
				if (text_effect.t_effect & (effect::bg_color | effect::fg_color)) {
					auto color = new d2d_coloreffect();
					if (text_effect.t_effect & effect::fg_color) {
						color->fg_color = text_effect.fg_color;
					}
					if (text_effect.t_effect & effect::bg_color) {
						color->bg_color = text_effect.bg_color;
					}
					layout->SetDrawingEffect(color, { text_effect.start_index, text_effect.length });
				}
				if (text_effect.t_effect & effect::bold) {
					layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, { text_effect.start_index, text_effect.length });
				}
				if (text_effect.t_effect & effect::underline) {
					layout->SetUnderline(true, { text_effect.start_index, text_effect.length });
				}
				if (text_effect.t_effect & effect::italic) {
					layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, { text_effect.start_index, text_effect.length });
				}
			}
			layout->SetMaxWidth(m_max_width);
			std::swap(m_layout, layout);
			std::swap(m_stripped_text, result.first);
			m_layout->GetMetrics(&m_metrics);
		}

		void set_width(std::uint32_t new_width) override final {
			m_max_width = new_width;
			m_layout->SetMaxWidth(m_max_width);
			m_layout->GetMetrics(&m_metrics);
		}

		void set_marks(gsl::span<xtext::text_range> marks) override final {
			m_marks.clear();
			m_marks.insert(m_marks.begin(), marks.cbegin(), marks.cend());
			invalidate(m_backend);
		}

		void set_alignment(xtext::align align_to) override final {
			switch (align_to) {
			case xtext::align::right:
				m_layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
				break;
			case xtext::align::center:
				m_layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
				break;
			default:
				m_layout->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
				break;
			}
		}

		void clear_marks() override final {
			m_marks.clear();
			invalidate(m_backend);
		}

		int index_for_location(::xtext::point2d loc) override final {
			BOOL isTrailing = false, isInside = false;
			DWRITE_HIT_TEST_METRICS metrics = {};
			m_layout->HitTestPoint(loc.x, loc.y, &isTrailing, &isInside, &metrics);
			return isInside ? metrics.textPosition : -1;
		};
		int line_from_index(std::uint32_t index) override final {
			if (m_metrics.lineCount == 1) {
				return 0;
			}
			FLOAT pX, pY;
			DWRITE_HIT_TEST_METRICS metrics = {};
			m_layout->HitTestTextPosition(index, false, &pX, &pY, &metrics);
			return m_metrics.height / (metrics.top + metrics.height);
		};
		std::string_view text() const noexcept override final {
			return m_stripped_text;
		};
		IDWriteTextLayout* layout() const noexcept {
			return m_layout.Get();
		}
	};

	d2d_backend(GtkWidget * /*parentWidget*/)
	{
		_com_util::CheckError(D2D1CreateFactory(
			D2D1_FACTORY_TYPE_SINGLE_THREADED,
			IID_PPV_ARGS(m_d2dfactory.GetAddressOf())));

		_com_util::CheckError(DWriteCreateFactory(
			DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
			reinterpret_cast<IUnknown **>(m_dwfactory.GetAddressOf())));
		create_render_target();
	}

	void create_render_target() {
		if (m_rt) {
			for (auto & brush : m_d2d_pallet) {
				brush.Reset();
			}
		}
		const auto rtp = D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_DEFAULT,
			D2D1::PixelFormat(
				DXGI_FORMAT_B8G8R8A8_UNORM,
				D2D1_ALPHA_MODE_IGNORE),
			0,
			0,
			D2D1_RENDER_TARGET_USAGE_NONE,
			D2D1_FEATURE_LEVEL_DEFAULT
		);
		_com_util::CheckError(m_d2dfactory->CreateDCRenderTarget(&rtp,
			m_rt.ReleaseAndGetAddressOf()));

		auto renderer = std::make_unique<d2d_renderer>(m_rt.Get(), m_d2d_pallet);
		renderer->QueryInterface(IID_PPV_ARGS(&m_render));
		if (m_render) {
			renderer.release();
		}
	}

	int get_string_width(const xtext::ustring_ref& text, int /*strip_hidden*/) override final {
		dwrite_layout layout(this, text, ~0U, nullptr);
		return layout.width();
	}

	bool
	set_default_font(const std::string_view &defaultFont) override final
	{
		const auto font_size_loc = defaultFont.find_last_of(' ');
		if (font_size_loc == std::string_view::npos)
		{
			return false;
		}
		const auto font_name = defaultFont.substr(0, font_size_loc);
		const auto font_size_str =
			std::string(defaultFont.substr(font_size_loc + 1));
		const auto font_size = std::stof(font_size_str);
		const auto wide_font = charset::widen(std::string(font_name));
		std::array<wchar_t, LOCALE_NAME_MAX_LENGTH> localeName = {};
		if (::GetUserDefaultLocaleName(localeName.data(),
						   localeName.size()) == 0)
		{
			_com_issue_error(HRESULT_FROM_WIN32(GetLastError()));
		}
		const auto font_size_DIP = (font_size * 96.0f) / 72.0f;
		wrl::ComPtr<IDWriteTextFormat> text_format;
		_com_util::CheckError(m_dwfactory->CreateTextFormat(
			L"Consolas", nullptr, DWRITE_FONT_WEIGHT_REGULAR,
			DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
			font_size_DIP, localeName.data(),
			text_format.GetAddressOf()));
		std::swap(this->m_currTextFormat, text_format);
		this->default_font_name =
			charset::widen(std::string(defaultFont));
		return true;
	}

	std::unique_ptr<xtext::renderer> make_renderer(cairo_t* cr, const GdkRectangle& rect) override final {
		auto target_surface = cairo_get_target(cr);
		cairo_surface_flush(target_surface);
		auto hdc = cairo_win32_surface_get_dc(target_surface);
		RECT rc = {};
		/*rc.top = rect.y;
		rc.left = rect.x;*/
		rc.bottom = rect.y + rect.height;
		rc.right = rect.x + rect.width;
		return std::make_unique<d2drenderer>(target_surface, hdc, rc, m_rt.Get(), m_render.Get(), this);
	}

	void set_palette(const gsl::span<GdkColor, XTEXT_COLS> colors) override final
	{
		std::array<wrl::ComPtr<ID2D1Brush>, XTEXT_COLS> new_colors;
		std::transform(
			colors.cbegin(),
			colors.cend(),
			new_colors.begin(),
			[this](const auto& color) {
			wrl::ComPtr<ID2D1SolidColorBrush> brush;
			comhelp::CheckResult(
				m_rt->CreateSolidColorBrush(
					gdk_color_to_d2d(color),
					brush.GetAddressOf()
				));
			return std::move(brush);
		});
		std::copy(colors.cbegin(), colors.cend(), m_gdk_pallet.begin());
		std::swap(m_d2d_pallet, new_colors);
	}

	std::unique_ptr<xtext::layout> make_layout(const xtext::ustring_ref text, std::uint32_t max_width) override final {
		return std::make_unique<dwrite_layout>(this, text, max_width, nullptr);
	}
};

#endif


class pangocairo_backend : public xtext::xtext_backend
{
	class pango_layout : public xtext::layout {
		std::uint32_t m_max_width;
		pangocairo_backend* m_backend;
		xtext::PangoLayoutPtr m_layout;
		xtext::PangoAttrListPtr m_attrList;
		xtext::ustring m_original_text;
		std::vector<xtext::text_range> m_marks;
	public:
		// rebuilds the layout and attributes
		void invalidate(gsl::not_null<xtext::xtext_backend*> backend) override final
		{
			const auto local_backend = dynamic_cast<pangocairo_backend*>(backend.get());
			if (!local_backend) {
				return;
			}
			m_backend = local_backend;
			xtext::PangoLayoutPtr temp_layout{
				pango_layout_copy(m_backend->layout.get()) };
			xtext::PangoAttrListPtr attr_list{ pango_attr_list_new() };
			const auto result = strip_and_parse_effects(std::string{ m_original_text.cbegin(), m_original_text.cend() });
			const auto final_effects = merge_marks(result.second, m_marks);
			for (const auto & effect : final_effects) {
				if (effect.t_effect & effect::fg_color) {
					const auto fg = handle_mirc_oddness(effect.fg_color, XTEXT_FG);
					const auto &color =
						m_backend->color_pallet[fg];
					PangoAttributePtr fg_color_attr{
						pango_attr_foreground_new(
							color.red, color.green,
							color.blue) };
					fg_color_attr->start_index = effect.start_index;
					fg_color_attr->end_index = effect.start_index + effect.length;
					pango_attr_list_change(attr_list.get(), fg_color_attr.release());
				}
				if (effect.t_effect & effect::bg_color) {
					const auto bg = handle_mirc_oddness(effect.bg_color, XTEXT_BG);
					const auto &color =
						m_backend->color_pallet[bg];
					PangoAttributePtr bg_color_attr{
						pango_attr_background_new(
							color.red, color.green,
							color.blue) };
					bg_color_attr->start_index = effect.start_index;
					bg_color_attr->end_index = effect.start_index + effect.length;
					pango_attr_list_change(attr_list.get(), bg_color_attr.release());
				}
				if (effect.t_effect & effect::bold) {
					PangoAttributePtr bold_attr{
						pango_attr_weight_new(PANGO_WEIGHT_BOLD) };
					bold_attr->start_index = effect.start_index;
					bold_attr->end_index = effect.start_index + effect.length;
					pango_attr_list_change(attr_list.get(), bold_attr.release());
				}
				if (effect.t_effect & effect::underline) {
					PangoAttributePtr underline_attr{
						pango_attr_underline_new(
							PANGO_UNDERLINE_SINGLE) };
					underline_attr->start_index = effect.start_index;
					underline_attr->end_index = effect.start_index + effect.length;
					pango_attr_list_change(attr_list.get(), underline_attr.release());
				}
				if (effect.t_effect & effect::italic) {
					PangoAttributePtr italics_attr{
						pango_attr_style_new(PANGO_STYLE_ITALIC) };
					italics_attr->start_index = effect.start_index;
					italics_attr->end_index = effect.start_index + effect.length;
					pango_attr_list_change(attr_list.get(), italics_attr.release());
				}
			}
#if 0
			using oustringstream = std::basic_stringstream<unsigned char>;
			oustringstream outbuf;
			std::ostream_iterator<unsigned char, unsigned char> out{ outbuf };
			std::locale locale;
			std::ptrdiff_t dist_from_start = 0;
			std::vector<PangoAttributePtr> active_attrs;

			for (auto itr = m_original_text.cbegin(), end = m_original_text.cend(); itr != end;)
			{
				const int mbl = charlen(itr); /* multi-byte length */
				if (mbl > std::distance(itr, end))
					break; // bad UTF-8
				switch (*itr)
				{
				case xtext::ATTR_COLOR:
				{
					++itr;
					if (itr == end)
					{
						break;
					}
					const auto color_str = std::string{ itr, end };
					int fg_color, bg_color;
					std::ptrdiff_t dist = 0;
					std::tie(fg_color, bg_color, dist) =
						parse_color_string(color_str);
					// is the string valid?
					if (dist == 0) {
						continue;
					}
					if (fg_color != XTEXT_FG)
					{
						fg_color = handle_mirc_oddness(fg_color, XTEXT_FG);
						const auto &color =
							m_backend->color_pallet[fg_color];
						PangoAttributePtr fg_color_attr{
							pango_attr_foreground_new(
								color.red, color.green,
								color.blue) };
						fg_color_attr->start_index =
							dist_from_start;
						active_attrs.emplace_back(std::move(fg_color_attr));
					}
					if (bg_color != XTEXT_BG)
					{
						bg_color = handle_mirc_oddness(bg_color, XTEXT_BG);
						const auto &color =
							m_backend->color_pallet[bg_color];
						PangoAttributePtr bg_color_attr{
							pango_attr_background_new(
								color.red, color.green,
								color.blue) };
						bg_color_attr->start_index =
							dist_from_start;
						active_attrs.emplace_back(std::move(bg_color_attr));
					}
					itr += dist;
					continue;
				}
				//case xtext::ATTR_BEEP:

				case xtext::ATTR_REVERSE:
				{
					const auto &fg_color =
						m_backend->color_pallet[XTEXT_BG];
					PangoAttributePtr fg_color_attr{
						pango_attr_foreground_new(
							fg_color.red, fg_color.green,
							fg_color.blue) };
					fg_color_attr->start_index =
						dist_from_start;
					active_attrs.emplace_back(std::move(fg_color_attr));

					const auto &bg_color =
						m_backend->color_pallet[XTEXT_FG];
					PangoAttributePtr bg_color_attr{
						pango_attr_background_new(
							bg_color.red, bg_color.green,
							bg_color.blue) };
					bg_color_attr->start_index =
						dist_from_start;
					active_attrs.emplace_back(std::move(bg_color_attr));
				}
				break;
				case xtext::ATTR_BOLD:
				{
					PangoAttributePtr bold_attr{
						pango_attr_weight_new(PANGO_WEIGHT_BOLD) };
					bold_attr->start_index = dist_from_start;
					active_attrs.emplace_back(std::move(bold_attr));
				}
				break;
				case ATTR_UNDERLINE:
				{
					PangoAttributePtr underline_attr{
						pango_attr_underline_new(
							PANGO_UNDERLINE_SINGLE) };
					underline_attr->start_index =
						dist_from_start;
					active_attrs.emplace_back(
						std::move(underline_attr));
				}
				break;
				case xtext::ATTR_ITALICS:
				{
					PangoAttributePtr italics_attr{
						pango_attr_style_new(PANGO_STYLE_ITALIC) };
					italics_attr->start_index = dist_from_start;
					active_attrs.emplace_back(
						std::move(italics_attr));
				}
				break;
				case xtext::ATTR_HIDDEN:
					++itr;
					for (; itr != end && *itr != xtext::ATTR_HIDDEN; ++itr) { continue; }
					++itr;
					continue;
				case xtext::ATTR_RESET:
				{
					for (auto &active_attr : active_attrs)
					{
						active_attr->end_index =
							dist_from_start;
						pango_attr_list_insert(
							attr_list.get(),
							active_attr.release());
					}
					active_attrs.clear();
				}
				break;
				default:
					dist_from_start += mbl;
					std::copy_n(itr, mbl, out);
				}
				itr += mbl;
			}
			for (auto &active_attr : active_attrs)
			{
				active_attr->end_index =
					PANGO_ATTR_INDEX_TO_TEXT_END;
				pango_attr_list_insert(
					attr_list.get(),
					active_attr.release());
			}
			const auto stripped_str = outbuf.str();

			for (const auto & mark : m_marks) {
				if (mark.start == -1 && mark.end == -1) {
					continue;
				}
				const auto &fg_color =
					m_backend->color_pallet[XTEXT_MARK_FG];
				PangoAttributePtr fg_color_attr{
					pango_attr_foreground_new(
						fg_color.red, fg_color.green,
						fg_color.blue) };
				fg_color_attr->start_index =
					mark.start;
				fg_color_attr->end_index = mark.end;
				pango_attr_list_insert(
					attr_list.get(),
					fg_color_attr.release());

				const auto &bg_color =
					m_backend->color_pallet[XTEXT_MARK_BG];
				PangoAttributePtr bg_color_attr{
					pango_attr_background_new(
						bg_color.red, bg_color.green,
						bg_color.blue) };
				bg_color_attr->start_index =
					mark.start;
				bg_color_attr->end_index = mark.end;
				pango_attr_list_insert(
					attr_list.get(),
					bg_color_attr.release());
			}
#endif
			pango_layout_set_text(temp_layout.get(), reinterpret_cast<const char*>(result.first.c_str()), result.first.size());
			pango_layout_set_attributes(temp_layout.get(), attr_list.get());
			pango_layout_set_width(temp_layout.get(), m_max_width * PANGO_SCALE);
			pango_layout_set_single_paragraph_mode(temp_layout.get(), true);
			m_layout = std::move(temp_layout);
			m_attrList = std::move(attr_list);
		}

		pango_layout(pangocairo_backend* backend, xtext::ustring_ref text, std::uint32_t width, gsl::span<xtext::text_range> marks)
			:m_max_width(width), m_backend(backend), m_original_text(text), m_marks(marks.cbegin(), marks.cend()){
			
			invalidate(m_backend);
		}

		std::uint32_t width() const noexcept override final {
			int width_val = 0;
			pango_layout_get_pixel_size(m_layout.get(), &width_val, nullptr);
			return width_val;
		}

		std::uint32_t line_count() const noexcept override final {
			return pango_layout_get_line_count(m_layout.get());
		}

		int index_for_location(xtext::point2d loc) override final {
			int index = 0, trailing = 0;
			if (!pango_layout_xy_to_index(
				m_layout.get(),
				loc.x * PANGO_SCALE,
				loc.y * PANGO_SCALE,
				&index,
				&trailing)) {
				return -1;
			}
			return index;
		}

		int line_from_index(std::uint32_t index) override final {
			int line = 0;
			pango_layout_index_to_line_x(m_layout.get(), index, false, &line, nullptr);
			return line;
		}

		void set_width(std::uint32_t new_width) override final {
			m_max_width = new_width;
			pango_layout_set_width(m_layout.get(), new_width * PANGO_SCALE);
		}

		void set_marks(gsl::span<xtext::text_range> marks) override final {
			m_marks.clear();
			m_marks.insert(m_marks.begin(), marks.cbegin(), marks.cend());
			invalidate(m_backend);
		}

		void clear_marks() override final {
			m_marks.clear();
			invalidate(m_backend);
		}

		PangoLayout* layout() {
			return m_layout.get();
		}

		std::string_view text() const noexcept override final {
			return pango_layout_get_text(m_layout.get());
		}

		void set_alignment(xtext::align align_to) override final {
			switch (align_to) {
			case xtext::align::center:
				pango_layout_set_alignment(m_layout.get(), PANGO_ALIGN_CENTER);
				break;
			case xtext::align::right:
				pango_layout_set_alignment(m_layout.get(), PANGO_ALIGN_RIGHT);
				break;
			default:
				break;
			}
		}


	};
	int ascent;
	int descent;
	GtkWidget *parent;
	std::string default_font_name;
	xtext::PangoLayoutPtr layout;
	PangoFontDescriptionPtr font_desc;
	std::array<xtext::PangoAttrListPtr, 4> attr_lists;
	std::array<GdkColor, XTEXT_COLS> color_pallet;

	auto font_size() const noexcept { return ascent + descent; }

	struct pangocairo_render: public xtext::renderer{
		cairo_t * cr;

		pangocairo_render(cairo_t* cr)
			:cr(cr){}

		void begin_rendering() override final {}
		void end_rendering() override final {};
		void render_layout_at(xtext::point2d loc, xtext::layout* target) override final {
			const auto p_layout = dynamic_cast<pango_layout*>(target);

			if (!p_layout) {
				return;
			}

			cairo_stack stack{ cr };
			cairo_new_path(cr);
			cairo_move_to(cr, loc.x, loc.y);

			pango_cairo_show_layout(cr, p_layout->layout());
		}
	};

public:
	pangocairo_backend(GtkWidget *parentWidget)
		: ascent(), descent(), parent(parentWidget)
	{
		layout.reset(gtk_widget_create_pango_layout(parentWidget, 0));
	}

	bool
	set_default_font(const std::string_view &defaultFont) override final
	{
		const auto font_name = std::string(defaultFont);
		PangoFontDescriptionPtr font{
			pango_font_description_from_string(font_name.c_str())};
		if (font && pango_font_description_get_size(font.get()) == 0)
		{
			font.reset(
				pango_font_description_from_string("sans 11"));
		}
		if (!font)
		{
			return false;
		}
		if (!this->layout)
		{
			layout.reset(gtk_widget_create_pango_layout(
				this->parent, nullptr));
		}

		auto pango_context = gtk_widget_get_pango_context(parent);
		auto lang = pango_context_get_language(pango_context);
		PangoFontMetricsPtr metrics{
			pango_context_get_metrics(pango_context, font.get(), lang)};
		this->ascent =
			PANGO_PIXELS(pango_font_metrics_get_ascent(metrics.get()));
		this->descent =
			PANGO_PIXELS(pango_font_metrics_get_descent(metrics.get()));
		pango_layout_set_font_description(this->layout.get(),
						  this->font_desc.get());
		this->font_desc = std::move(font);
		this->default_font_name = std::move(font_name);
		return true;
	}

	int get_string_width(const xtext::ustring_ref &text,
					 int strip_hidden) override final
	{
		if (text.empty())
		{
			return 0;
		}

		std::vector<xtext::offlen_t> strip_locations;
		const auto striped_value =
			xtext::strip_color(text, &strip_locations, strip_hidden);
		xtext::PangoLayoutPtr temp_layout{
			pango_layout_copy(this->layout.get())};
		xtext::PangoAttrListPtr attr_list{pango_attr_list_new()};

		for (const auto &emph_location : strip_locations)
		{

			if (emph_location.emph & xtext::EMPH_ITAL)
			{
				auto attr =
					pango_attr_style_new(PANGO_STYLE_ITALIC);
				attr->start_index = emph_location.off;
				attr->end_index =
					attr->start_index + emph_location.len;
				pango_attr_list_insert(attr_list.get(), attr);
			}
			if (emph_location.emph & xtext::EMPH_BOLD)
			{
				auto attr =
					pango_attr_weight_new(PANGO_WEIGHT_BOLD);
				attr->start_index = emph_location.off;
				attr->end_index =
					attr->start_index + emph_location.len;
				pango_attr_list_insert(attr_list.get(), attr);
			}
		}
		pango_layout_set_text(
			temp_layout.get(),
			reinterpret_cast<const char *>(striped_value.c_str()),
			striped_value.length());
		pango_layout_set_attributes(temp_layout.get(), attr_list.get());
		int width = 0;
		pango_layout_get_pixel_size(temp_layout.get(), &width, nullptr);
		return width;
	}
#if 0
	int render_at(
		cairo_t* cr,
		int x,
		int y,
		int width,
		int indent,
		int mark_start,
		int mark_end,
		xtext::align alignment,
		const xtext::ustring_ref& text) override final {
		std::array<xtext::text_range, 1> marks = { { mark_start, mark_end } };
		pango_layout layout(this, text, width, marks);
		switch (alignment) {
		case xtext::align::center:
			pango_layout_set_alignment(layout.layout(), PANGO_ALIGN_CENTER);
			break;
		case xtext::align::right:
			pango_layout_set_alignment(layout.layout(), PANGO_ALIGN_RIGHT);
			break;
		default:
			break;
		}
		pango_layout_set_indent(layout.layout(), indent * PANGO_SCALE);
		cairo_stack stack{ cr };
		cairo_new_path(cr);
		cairo_move_to(cr, x, y);
		
		pango_cairo_show_layout(cr, layout.layout());

		return pango_layout_get_line_count(layout.layout());
	}

#endif

	void set_palette(const gsl::span<GdkColor, XTEXT_COLS> colors) override final
	{
		std::copy_n(colors.cbegin(), this->color_pallet.size(), this->color_pallet.begin());
	}

	std::unique_ptr<xtext::renderer> make_renderer(cairo_t * cr, const GdkRectangle & /*rect*/) override final {
		return std::make_unique<pangocairo_render>(cr);
	}

	std::unique_ptr<xtext::layout> make_layout(const xtext::ustring_ref text, std::uint32_t max_width) override final {
		return std::make_unique<pango_layout>(this, text, max_width, nullptr);
	}
};

/* CL: needs to strip hidden when called by gtk_xtext_text_width, but not when
 * copying text */
struct chunk_t
{
	std::vector<xtext::offlen_t> slp;
	int off1;
	int len1;
	int emph;
	xtext::offlen_t meta;
};

void xtext_do_chunk(chunk_t &c)
{
	if (c.len1 == 0)
		return;

	xtext::offlen_t meta;
	meta.off = c.off1;
	meta.len = c.len1;
	meta.emph = c.emph;
	meta.width = 0;
	c.slp.emplace_back(meta);

	c.len1 = 0;
}
}

namespace xtext
{
std::unique_ptr<xtext_backend>
create_backend(const std::string_view &defaultFont, GtkWidget *parentWidget)
{
	auto cairobackend = std::make_unique<pangocairo_backend>(parentWidget);
	cairobackend->set_default_font(defaultFont);
	return std::move(cairobackend);
}

ustring strip_color(const ustring_ref &text, std::vector<offlen_t> *slp,
			int strip_hidden)
{
	chunk_t c;
	int rcol = 0, bgcol = 0;
	bool hidden = false;
	auto beginning = text.cbegin();
	using oustringstream = std::basic_stringstream<unsigned char>;
	oustringstream outbuf;
	std::ostream_iterator<unsigned char, unsigned char> out{outbuf};

	c.off1 = 0;
	c.len1 = 0;
	c.emph = 0;
	std::locale locale;
	for (auto itr = text.cbegin(), end = text.cend(); itr != end;)
	{
		const int mbl = charlen(itr); /* multi-byte length */
		if (mbl > std::distance(itr, end))
			break; // bad UTF-8
		if (rcol > 0 && (itr + 1) == end) {
			break;
		}
		if (rcol > 0 &&
			(std::isdigit<char>(*itr, locale) ||
			 (*itr == ',' && std::isdigit<char>(itr[1], locale) &&
			  !bgcol)))
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
}