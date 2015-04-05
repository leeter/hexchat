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

#include <cstdlib>
#include <string>
#include <vector>
//#define BOOST_SPIRIT_DEBUG
#define BOOST_SPIRIT_USE_PHOENIX_V3
#include <boost/algorithm/string.hpp>
#include <boost/optional.hpp>
#include <boost/fusion/adapted.hpp>
#include <boost/spirit/include/qi.hpp>
#include <boost/phoenix.hpp>
#include "message.hpp"


BOOST_FUSION_ADAPT_STRUCT(
	irc::message,
	(std::string, prefix)
	(irc::message::numeric_reply, reply)
	(std::string, command)
	(std::string, params)
	)
namespace qi = boost::spirit::qi;
namespace phx = boost::phoenix;

namespace
{
	namespace rfc2812
	{
		const std::string::size_type max_prefix_len = 64;
		const std::string::size_type max_message_len = 512;
	}

	template <typename It, typename Skipper = qi::space_type>
	struct parser : qi::grammar<It, irc::message(), Skipper>
	{
		parser() : parser::base_type(start)
		{
			numeric =
			    (qi::int_[qi::_val = phx::static_cast_<
					  irc::message::numeric_reply>(
					  qi::_1)] > +qi::lit(' ')) |
			    qi::attr(irc::message::numeric_reply::NON_NUMERIC);
			prefix %= qi::lit(':') > +(qi::char_ - qi::space) >
				  +qi::lit(' ');
			command = +(qi::alpha - qi::space);
			params %= -qi::lit(':') >> *(qi::char_ - "\r\n") |
				  qi::attr(std::string{});

			start %= -prefix >> -numeric >> -command >> params;
			BOOST_SPIRIT_DEBUG_NODE(prefix);
			BOOST_SPIRIT_DEBUG_NODE(numeric);
			BOOST_SPIRIT_DEBUG_NODE(command);
			BOOST_SPIRIT_DEBUG_NODE(params);
			BOOST_SPIRIT_DEBUG_NODE(start);
		}

	      private:
		qi::rule<It, irc::message::numeric_reply()> numeric;
		qi::rule<It, std::string()> prefix;
		qi::rule<It, std::string()> command;
		qi::rule<It, std::string() /* lexeme */> params;
		qi::rule<It, irc::message(), Skipper> start;
	};
	}

namespace irc
{
	boost::optional<message> parse(const std::string & inbound)
	{
		
		using boost::spirit::ascii::space;
		// if we're getting garbage, just ignore it
		if (inbound.size() > rfc2812::max_message_len)
		{
			return boost::none;
		}
		// a message must end with a crlf
		if (!boost::ends_with(inbound, "\r\n"))
			return boost::none;

		message m;
		typedef std::string::const_iterator iterator_type;
		using message_parser = parser < iterator_type > ;
		message_parser p;
		auto ok = qi::phrase_parse(std::begin(inbound), std::end(inbound), p, qi::space, m);
		if (!ok)
			return boost::none;
		auto bang_loc = m.prefix.find_first_of('!');
		if (bang_loc != std::string::npos)
		{
			m.nick = m.prefix.substr(0, bang_loc);
			m.host = m.prefix.substr(bang_loc);
		}
		//auto prefix_start_loc = inbound.find_first_of(':');
		//auto prefix_end_loc = inbound.find_first_of(' ');
		//// if there  is a prefix the first character will be :
		//if (prefix_start_loc == 0 &&
		//	prefix_end_loc != 0 &&
		//	prefix_end_loc != std::string::npos)
		//{
		//	//malformed message
		//	if (prefix_end_loc > rfc2812::max_prefix_len)
		//		return boost::none;
		//	m.prefix = inbound.substr(0, prefix_end_loc);
		//	// increment by one so we can get the command
		//	++prefix_end_loc;
		//}
		//else
		//{
		//	prefix_end_loc = 0;
		//}

		//// get the command
		//auto command_end = inbound.find_first_of(' ', prefix_end_loc);
		//// a command is required if we can't parse one bug out
		//if (command_end == std::string::npos || command_end == prefix_end_loc)
		//	return boost::none;
		//m.command_s = inbound.substr(prefix_end_loc, command_end - prefix_end_loc);
		//if (boost::all(m.command_s, boost::is_digit))
		//{

		//}
		//char*end;
		//auto numeric_rep = std::strtol(m.command_s.c_str(), &end, 10);
		//if (numeric_rep)
		//	m.command_n = static_cast<message::numeric_reply>(numeric_rep);

		//// there are parameters
		//if (command_end < inbound.size() - 2)
		//	m.params = inbound.substr(command_end + 1, inbound.size() - 2);

		return m;
	}
}