/* HexChat
* Copyright (C) 2014 Berke Viktor.
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

#ifndef WIN32
#error THIS FILE IS FOR THE WINDOWS BUILD ONLY!
#endif

#include <codecvt>
#include <locale>
#include <string>

#include "charset_helpers.hpp"

namespace charset
{
    std::string narrow(const std::wstring & to_narrow)
    {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > converter;
        return converter.to_bytes(to_narrow);
    }

    std::wstring widen(const std::string & to_widen)
    {
        std::wstring_convert<std::codecvt_utf8_utf16<wchar_t> > converter;
        return converter.from_bytes(to_widen);
    }
}