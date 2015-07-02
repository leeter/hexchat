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
#include <comdef.h>
#include "w32coinitialize.hpp"
#include "w32dcc_security.hpp"

namespace
{
	_COM_SMARTPTR_TYPEDEF(IZoneIdentifier, __uuidof(IZoneIdentifier));
}

namespace w32
{
	namespace file
	{
		bool mark_file_as_downloaded(const std::wstring & path)
		{
			::w32::com::CCoInitialize init{ COINIT_APARTMENTTHREADED };

			if (!init)
				return false;
			try
			{
				IZoneIdentifierPtr zone_identifer{ CLSID_PersistentZoneIdentifier };
				if (!zone_identifer)
					return false;

				_com_util::CheckError(zone_identifer->SetId(URLZONE_INTRANET));
				IPersistFilePtr persist_file{ zone_identifer };
				if (!persist_file)
					return false;

				_com_util::CheckError(persist_file->Save(path.c_str(), TRUE));
			}
			catch (const _com_error &)
			{
				return false;
			}

			return true;
		}
	}
}