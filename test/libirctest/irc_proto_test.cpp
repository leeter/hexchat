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

#define BOOST_TEST_MODULE irc_proto_tests
#ifndef _MSC_VER
#define BOOST_TEST_DYN_LINK
#endif
#include <string>
#include <irc_proto.hpp>
#include <connection.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/utility/string_ref.hpp>

namespace
{
	struct test_connection : irc::connection
	{
		std::string message;
		void send(const boost::string_ref& outbound) override final
		{
			message = outbound.to_string();
		}
	};

} // anonymous namespace

BOOST_AUTO_TEST_SUITE(irc_proto)

BOOST_AUTO_TEST_CASE(away_no_reason)
{
	test_connection con;

	irc::proto::away(con, {});
	BOOST_REQUIRE_EQUAL(con.message, "AWAY : \r\n");
}

BOOST_AUTO_TEST_CASE(away_with_reason)
{
	test_connection con;

	irc::proto::away(con, "random");
	BOOST_REQUIRE_EQUAL(con.message, "AWAY :random\r\n");
}

BOOST_AUTO_TEST_CASE(back)
{
	test_connection con;
	irc::proto::back(con);
	BOOST_REQUIRE_EQUAL(con.message, "AWAY\r\n");
}

BOOST_AUTO_TEST_CASE(channel_modes)
{
	test_connection con;
	irc::proto::channel_modes(con, "channel");
	BOOST_REQUIRE_EQUAL(con.message, "MODE channel\r\n");
}

BOOST_AUTO_TEST_CASE(invite)
{
	test_connection con;
	irc::proto::invite(con, "nick", "channel");
	BOOST_REQUIRE_EQUAL(con.message, "INVITE nick channel\r\n");
}

BOOST_AUTO_TEST_CASE(join_no_key)
{
	test_connection con;
	irc::proto::join(con, "channel", {});
	BOOST_REQUIRE_EQUAL(con.message, "JOIN channel\r\n");
}

BOOST_AUTO_TEST_CASE(join_with_key)
{
	test_connection con;
	irc::proto::join(con, "channel", "key");
	BOOST_REQUIRE_EQUAL(con.message, "JOIN channel key\r\n");
}

BOOST_AUTO_TEST_CASE(mode)
{
	test_connection con;
	irc::proto::mode(con, "target", "mode");
	BOOST_REQUIRE_EQUAL(con.message, "MODE target mode\r\n");
}

BOOST_AUTO_TEST_SUITE_END()