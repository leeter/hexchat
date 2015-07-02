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

#ifndef HEXCHAT_W32_COINITALIZE_HPP
#define HEXCHAT_W32_COINITALIZE_HPP

#pragma once

#ifndef WIN32
#error cointialize IS FOR WINDOWS ONLY!!!
#endif

namespace w32{
	namespace com{
		class CCoInitialize {
			const HRESULT m_hr;
			CCoInitialize(const CCoInitialize&) = delete;
			CCoInitialize& operator=(const CCoInitialize&) = delete;
			CCoInitialize(CCoInitialize&&) = delete;
			CCoInitialize& operator=(CCoInitialize&&) = delete;
		public:
			explicit CCoInitialize(COINIT init) : m_hr(CoInitializeEx(nullptr, init)) { }
			~CCoInitialize() { if (SUCCEEDED(m_hr)) CoUninitialize(); }
			explicit operator HRESULT() const { return m_hr; }
			explicit operator bool() const { return SUCCEEDED(m_hr); }
		};
	}
}

#endif