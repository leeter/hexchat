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

	BOOST_REQUIRE_MESSAGE(con.message == "AWAY : \r\n", "If no reason is specified none should be transmitted");
}

BOOST_AUTO_TEST_SUITE_END()