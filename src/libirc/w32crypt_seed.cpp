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

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <memory>
#include <minwindef.h>
#include <bcrypt.h>
#include <openssl/rand.h>

#include "w32crypt_seed.hpp"
#ifndef WIN32
#error this file is for windows compilation only!
#endif

#ifndef NOEXCEPT
#if _MSC_VER >= 1900
#define NOEXCEPT noexcept
#else
#define NOEXCEPT throw()
#endif
#endif

#pragma comment(lib, "bcrypt.lib")

namespace{
	struct alg_deleter
	{
		using pointer = BCRYPT_ALG_HANDLE;
		void operator()(pointer handle) NOEXCEPT
		{
			::BCryptCloseAlgorithmProvider(handle, 0);
		}
	};
	using alg_ptr = std::unique_ptr < BCRYPT_ALG_HANDLE, alg_deleter > ;
}

namespace w32
{
	namespace crypto
	{
		void seed_openssl_random() throw()
		{
			BCRYPT_ALG_HANDLE hdnl;
			NTSTATUS res = ::BCryptOpenAlgorithmProvider(&hdnl, BCRYPT_RNG_ALGORITHM, nullptr, 0);
			if (!BCRYPT_SUCCESS(res))
				return; // TODO throw error?

			alg_ptr alg{ hdnl };

			UCHAR buffer[256] = { 0 };
			for (int i = 0; i < 256 && BCRYPT_SUCCESS(res); ++i)
			{
				res = ::BCryptGenRandom(alg.get(), buffer, sizeof(buffer), 0);
				::RAND_seed(buffer, sizeof(buffer));
			}
		}
	}
}