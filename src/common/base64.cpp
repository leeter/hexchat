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

#include <algorithm>
#include <istream>
#include <iterator>
#include <ostream>
#include <sstream>
#include <string>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include "base64.hpp"

namespace{
	const std::string base64_padding[] = { "", "==", "=" };
}
namespace util
{
	namespace transforms
	{

void
encode_base64(const char* in, std::size_t in_length, std::ostream & out)
{
	namespace bai = boost::archive::iterators;
	typedef bai::base64_from_binary<bai::transform_width<const char*, 6, 8>> base64_enc;

	std::copy(
		base64_enc(in),
		base64_enc(in + in_length),
		std::ostream_iterator<char>(out));
	 out << base64_padding[in_length % 3];
}

std::string 
encode_base64(const char* in, std::size_t in_length)
{
	std::ostringstream outstream;
	encode_base64(in, in_length, outstream);
	return outstream.str();
}

std::string
encode_base64(const std::string& in)
{
	return encode_base64(in.c_str(), in.size());
}

bool
decode_base64(const std::string & data, std::ostream & out)
{
	namespace bai = boost::archive::iterators;
	typedef bai::transform_width<bai::binary_from_base64<std::string::const_iterator>, 8, 6> base64_dec;
	auto dsize = data.size();

	// Remove the padding characters, cf. https://svn.boost.org/trac/boost/ticket/5629
	if (dsize && data[dsize - 1] == '=') {
		--dsize;
		if (dsize && data[dsize - 1] == '=') --dsize;
	}

	if (!dsize)
		return false;

	std::copy(
		base64_dec(data.cbegin()),
		base64_dec(data.cbegin() + dsize),
		std::ostream_iterator<char>(out));
	return true;
}

	} // namespace transforms
} // namespace util