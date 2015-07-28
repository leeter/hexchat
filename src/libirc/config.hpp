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

#ifndef LIBIRC_CONFIG_HPP
#define LIBIRC_CONFIG_HPP

#if !defined(__clang__) && !defined(__has_feature)
#define __has_feature(a) 0
#endif

#if defined(__clang__) && __has_feature(cxx_noexcept) || \
    defined(__GXX_EXPERIMENTAL_CXX0X__) && __GNUC__ * 10 + __GNUC_MINOR__ >= 46 || \
    defined(_MSC_FULL_VER) && _MSC_FULL_VER > 180040629
#  define NOEXCEPT noexcept
#  define CONSTEXPR_OR_CONST constexpr
#elif defined(_MSC_VER)
#  define NOEXCEPT throw()
#  define CONSTEXPR_OR_CONST const
#else
#error noexcept is required to compile this code!
#endif

#endif //LIBIRC_CONFIG_HPP