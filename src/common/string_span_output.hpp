/* HexChat
* Copyright (C) 2016 Leetsoftwerx.
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

#ifndef HEXCHAT_STRING_SPAN_OUTPUT
#define HEXCHAT_STRING_SPAN_OUTPUT

#include <iosfwd>
#include <boost/utility/string_ref.hpp>
#include <gsl.h>

template <typename CharT, std::ptrdiff_t Extent = gsl::dynamic_range>
std::ostream& operator<<(std::ostream& o, const gsl::basic_string_span<CharT, Extent>& one) {
	auto sentry = std::ostream::sentry{ o };
	if (!one.empty() && sentry) {
		o.write(one.data(), one.length());
	}
	return o;
}

template <typename CharT, std::ptrdiff_t Extent = gsl::dynamic_range>
constexpr boost::string_ref to_string_ref(gsl::basic_string_span<CharT, Extent> to_convert) noexcept {
	using from_size_type = gsl::basic_string_span<CharT, Extent>::size_type;
	using to_size_type = boost::string_ref::size_type;

	return{ to_convert.data(), gsl::narrow_cast<to_size_type, from_size_type>(to_convert.size()) };
}

#endif // !#ifndef HEXCHAT_STRING_SPAN_OUTPUT
