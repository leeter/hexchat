/* HexChat
 * Copyright (C) 1998-2010 Peter Zelezny.
 * Copyright (C) 2009-2013 Berke Viktor.
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

#ifndef HEXCHAT_SSL_HPP
#define HEXCHAT_SSL_HPP

#include <string>
namespace io{
	namespace ssl{


struct cert_info {
	char subject[256];
	char *subject_word[12];
	char issuer[256];
	char *issuer_word[12];
	char algorithm[32];
	int algorithm_bits;
	char sign_algorithm[32];
	int sign_algorithm_bits;
	char notbefore[32];
	char notafter[32];

	int rsa_tmp_bits;
};

struct cipher_info {
	int cipher_bits;
	std::string version;
	std::string cipher;
};

int get_cert_info (cert_info &cert_info, const SSL * ssl);
::io::ssl::cipher_info get_cipher_info(const SSL * ssl);




	}

}

#endif
