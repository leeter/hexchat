/* HexChat
* Copyright (C) 2015 Leetsoftwerx.
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
#ifndef _MSC_VER
#define BOOST_TEST_DYN_LINK
#endif
#define BOOST_TEST_MODULE util_tests
#include <algorithm>
#include <cstring>
#include <util.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/utility/string_ref.hpp>

BOOST_AUTO_TEST_SUITE(util_test)

// safe_strcpy

BOOST_AUTO_TEST_CASE(safe_strcpy_input_too_long)
{
	const char *input = "veryveryvery";
	// overallocate so we can check for buffer overflow
	char out[15];
	std::fill(std::begin(out), std::end(out), 'd');
	safe_strcpy(out, input, 10);

	BOOST_REQUIRE_EQUAL(std::strlen(out), 9);
	BOOST_REQUIRE_EQUAL(out[9], 0);
	BOOST_REQUIRE_EQUAL(out[10], 'd');
}

BOOST_AUTO_TEST_CASE(safe_strcpy_input_too_long_utf8)
{
	// unicode smily faces ☺ we have to do it this way because MS is still
	// behind the times. Each character is 3 bytes
	const char *input = "\xE2\x98\xBA\xE2\x98\xBA\xE2\x98\xBA\xE2\x98\xBA"
			    "\xE2\x98\xBA\xE2\x98\xBA";
	// overallocate so we can check for buffer overflow
	char out[15];
	std::fill(std::begin(out), std::end(out), 'd');
	// would terminate a character mid way if copied
	safe_strcpy(out, input, 11);

	BOOST_REQUIRE_EQUAL(std::strlen(out), 9);
	BOOST_REQUIRE_EQUAL(out[9], 0);
	BOOST_REQUIRE_EQUAL(out[10], 'd');
}

BOOST_AUTO_TEST_CASE(safe_strcpy_inputshorter_than_buffer)
{
	const char *input = "very";
	// overallocate so we can check for buffer overflow
	char out[15];
	std::fill(std::begin(out), std::end(out), 'd');
	safe_strcpy(out, input, 10);

	BOOST_REQUIRE_EQUAL(std::strlen(out), 4);
	BOOST_REQUIRE_EQUAL(out[4], 0);
	BOOST_REQUIRE_EQUAL(out[5], 'd');
}

// strip_color2

BOOST_AUTO_TEST_CASE(strip_color2_empty_string)
{
	std::string empty;
	auto result = strip_color2(empty, STRIP_ALL);
	BOOST_REQUIRE_EQUAL(result, empty);
}

BOOST_AUTO_TEST_CASE(strip_color2_remove_attrib)
{
	const char *input = "\00309\007abcdef\00309\007\017\026\002\037\035";
	const char *expected = "\00309abcdef\00309";
	auto result = strip_color2(input, STRIP_ATTRIB);
	BOOST_REQUIRE_EQUAL(result, expected);
}

BOOST_AUTO_TEST_CASE(strip_color2_remove_color)
{
	const char *input = "\00309\007abcdef\00309\007\017\026\002\037\035";
	const char *expected = "\007abcdef\007\017\026\002\037\035";
	auto result = strip_color2(input, STRIP_COLOR);
	BOOST_REQUIRE_EQUAL(result, expected);
}

BOOST_AUTO_TEST_CASE(strip_color2_remove_all)
{
	const char *input = "\00309\007abcdef\00309\007\017\026\002\037\035";
	const char *expected = "abcdef";
	auto result = strip_color2(input, STRIP_ALL);
	BOOST_REQUIRE_EQUAL(result, expected);
}

BOOST_AUTO_TEST_SUITE_END()