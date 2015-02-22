// commontest.cpp : Defines the entry point for the console application.
//
#ifndef _MSC_VER
#define BOOST_TEST_DYN_LINK
#endif
#define BOOST_TEST_MODULE util_tests
#include <cstring>
#include <util.hpp>
#include <boost/test/unit_test.hpp>
#include <boost/utility/string_ref.hpp>

BOOST_AUTO_TEST_SUITE(util_test)

BOOST_AUTO_TEST_CASE(safe_strcpy_input_too_long)
{
	const char* input = "veryveryvery";
	// overallocate so we can check for buffer overflow
	char out[15];
	safe_strcpy(out, input, 10);

	BOOST_REQUIRE_EQUAL(std::strlen(out), 9);
}


BOOST_AUTO_TEST_SUITE_END()