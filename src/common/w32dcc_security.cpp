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

#define STRICT
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <string>
#include <windows.h>
#include <urlmon.h>
#include <wrl/client.h>
#include "w32coinitialize.hpp"
#include "w32dcc_security.hpp"

namespace wrl = Microsoft::WRL;

namespace w32
{
	namespace file
	{
		bool mark_file_as_downloaded(const std::wstring & path)
		{
			::w32::com::CCoInitialize init(COINIT_APARTMENTTHREADED);

			if (FAILED(static_cast<HRESULT>(init)))
				return false;
			wrl::ComPtr<IZoneIdentifier> zone_identifer;
			HRESULT hr = CoCreateInstance(CLSID_PersistentZoneIdentifier, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&zone_identifer));
			if (FAILED(hr))
				return false;
			
			hr = zone_identifer->SetId(URLZONE_INTRANET);
			if (FAILED(hr))
				return false;

			wrl::ComPtr<IPersistFile> persist_file;
			hr = zone_identifer.As(&persist_file);
			if (FAILED(hr))
				return false;

			hr = persist_file->Save(path.c_str(), TRUE);

			return SUCCEEDED(hr);
		}
	}
}