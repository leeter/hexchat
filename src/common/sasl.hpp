/* HexChat
* Copyright (C) 1998-2010 Peter Zelezny.
* Copyright (C) 2009-2013 Berke Viktor.
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

#ifndef HEXCHAT_SASL_HPP
#define HEXCHAT_SASL_HPP

#include <string>

namespace auth
{
	namespace sasl
	{
		std::string encode_sasl_pass_plain(const std::string &user, const std::string &pass);
		std::string encode_sasl_pass_blowfish(const std::string & user, const std::string& pass, const std::string & data);
		std::string encode_sasl_pass_aes(const std::string & user, const std::string& pass, const std::string & data);
	}
}

#endif //HEXCHAT_SASL_HPP
