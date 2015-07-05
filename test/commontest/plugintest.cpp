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
#define BOOST_TEST_MODULE common_tests
#include <algorithm>
#include <cstring>
#include <memory>
#include <plugin.hpp>
#include <hexchat-plugin.h>
#include <boost/test/unit_test.hpp>
#include <boost/utility/string_ref.hpp>

BOOST_AUTO_TEST_SUITE(plugin_test)

BOOST_AUTO_TEST_CASE(plugin_save_str)
{
	const char * test_val_name = "foobar";
	const char * test_val = "barfoo";
	std::unique_ptr<hexchat_plugin_internal> ph{ std::make_unique<hexchat_plugin_internal>() };
	ph->name = "test";

	auto result = hexchat_pluginpref_set_str(ph.get(), test_val_name, test_val);
	BOOST_REQUIRE_EQUAL(!!result, true);
}

BOOST_AUTO_TEST_CASE(plugin_save_str_roundtrip)
{
	const char * test_val_name = "foobar";
	const char * test_val = "barfoo";
	std::unique_ptr<hexchat_plugin_internal> ph{ std::make_unique<hexchat_plugin_internal>() };
	ph->name = "test with spaces and crazy/_-!";

	auto result = hexchat_pluginpref_set_str(ph.get(), test_val_name, test_val);
	BOOST_REQUIRE_EQUAL(!!result, true);
	char test_buffer[512] = { 0 };
	auto res2 = hexchat_pluginpref_get_str(ph.get(), test_val_name, test_buffer);
	BOOST_REQUIRE_EQUAL(!!res2, true);
	BOOST_ASSERT(std::strcmp(test_val, test_buffer) == 0);
}

BOOST_AUTO_TEST_CASE(plugin_get_preflist_single)
{
	const char * test_val_name = "foobar";
	const char * test_val = "barfoo";
	std::unique_ptr<hexchat_plugin_internal> ph{ std::make_unique<hexchat_plugin_internal>() };
	ph->name = "test with spaces and crazy/_-!";

	auto result = hexchat_pluginpref_set_str(ph.get(), test_val_name, test_val);
	BOOST_REQUIRE_EQUAL(!!result, true);

	char test_buffer[4096] = { 0 };
	auto res2 = hexchat_pluginpref_list(ph.get(), test_buffer);
	BOOST_REQUIRE_EQUAL(!!res2, true);
	BOOST_REQUIRE_EQUAL(std::strlen(test_val), std::strlen(test_buffer));
	BOOST_ASSERT(std::strcmp(test_val, test_buffer) == 0);
}


BOOST_AUTO_TEST_CASE(plugin_get_preflist_multi)
{
	const char * test_val_name = "foobar";
	const char * test_val2_name = "barfoo";
	const char * expected_list = "foobar,barfoo";
	std::unique_ptr<hexchat_plugin_internal> ph{ std::make_unique<hexchat_plugin_internal>() };
	ph->name = "test with spaces and crazy/_-!";

	auto result = hexchat_pluginpref_set_str(ph.get(), test_val_name, "foo");
	BOOST_REQUIRE_EQUAL(!!result, true);
	result = hexchat_pluginpref_set_str(ph.get(), test_val2_name, "foo2");
	BOOST_REQUIRE_EQUAL(!!result, true);

	char test_buffer[4096] = { 0 };
	auto res2 = hexchat_pluginpref_list(ph.get(), test_buffer);
	BOOST_REQUIRE_EQUAL(!!res2, true);
	BOOST_REQUIRE_EQUAL(std::strlen(expected_list), std::strlen(test_buffer));
	BOOST_ASSERT(std::strcmp(expected_list, test_buffer) == 0);
}

BOOST_AUTO_TEST_SUITE_END()