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

#pragma comment(lib, "bcrypt.lib")

namespace{
	auto alg_deleter = [](BCRYPT_ALG_HANDLE handle) throw()
	{
		BCryptCloseAlgorithmProvider(handle, 0);
	};
}

namespace w32
{
	namespace crypto
	{
		void seed_openssl_random() throw()
		{
			BCRYPT_ALG_HANDLE hdnl;
			NTSTATUS res = BCryptOpenAlgorithmProvider(&hdnl, BCRYPT_RNG_ALGORITHM, nullptr, 0);
			if (!BCRYPT_SUCCESS(res))
				return; // TODO throw error?

			std::unique_ptr<std::remove_pointer<BCRYPT_ALG_HANDLE>::type, decltype(alg_deleter)> alg{ hdnl, alg_deleter };

			UCHAR buffer[256] = { 0 };
			for (int i = 0; i < 256 && BCRYPT_SUCCESS(res); ++i)
			{
				res = BCryptGenRandom(alg.get(), buffer, sizeof(buffer), 0);
				RAND_seed(buffer, sizeof(buffer));
			}
		}
	}
}