/* libirc
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

// do not uncomment this should only be defined once
//#define BOOST_TEST_MODULE irc_proto_tests
#ifndef _MSC_VER
#define BOOST_TEST_DYN_LINK
#endif
#include <string>
#include <message.hpp>
#include <connection.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/utility/string_ref.hpp>
#include <boost/optional.hpp>

BOOST_AUTO_TEST_SUITE(irc_message)

BOOST_AUTO_TEST_CASE(parse_privmsg)
{
	auto result = irc::parse(":Angel!wings@irc.org PRIVMSG Wiz :Are you receiving this message ?\r\n");
	BOOST_REQUIRE_MESSAGE(static_cast<bool>(result), "A properly formatted message should not be rejected");
	BOOST_REQUIRE_EQUAL(result->reply, irc::message::NON_NUMERIC);
	BOOST_REQUIRE_EQUAL(result->command, "PRIVMSG");
	BOOST_REQUIRE_MESSAGE(!result->params.empty(), "The PRIVMSG should should have parameters");
}

BOOST_AUTO_TEST_CASE(parse_numeric_reply)
{
	auto result = irc::parse(":irc.example.net 303 l :a b c d\r\n");
	BOOST_REQUIRE_EQUAL(result->reply, irc::message::RPL_ISON);
	BOOST_REQUIRE_EQUAL(result->command, "l");
	BOOST_REQUIRE_EQUAL(result->params, "a b c d");
	BOOST_REQUIRE_EQUAL(result->prefix, "irc.example.net");
}

BOOST_AUTO_TEST_CASE(parse_invalid_no_terminator)
{
	auto result = irc::parse("foo");
	BOOST_REQUIRE_MESSAGE(!static_cast<bool>(result), "An improperly formatted message should return none");
}

BOOST_AUTO_TEST_CASE(parse_invalid_too_long)
{
	auto result = irc::parse(
	    "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Mauris "
	    "aliquet et lacus id congue. Vivamus magna ipsum, aliquet varius "
	    "erat sed, vulputate suscipit lacus. Aenean faucibus non lacus at "
	    "dapibus. Maecenas et fringilla orci, vel scelerisque mi. Vivamus "
	    "non urna vel metus volutpat accumsan. Etiam vel nulla ultrices, "
	    "volutpat dolor id, aliquam nulla. Curabitur elementum lacinia "
	    "velit nec sollicitudin. Donec scelerisque diam erat, tincidunt "
	    "pretium est varius id. Phasellus dignissim mauris sit amet "
	    "nullam. ");
	BOOST_REQUIRE_MESSAGE(
	    !static_cast<bool>(result),
	    "An improperly formatted message should return none");
}

BOOST_AUTO_TEST_SUITE_END()