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

#include <string>
#include <boost/spirit/include/qi.hpp>
#include <boost/fusion/include/adapt_struct.hpp>
#include "message.hpp"

namespace qi = boost::spirit::qi;
namespace ascii = boost::spirit::ascii;

BOOST_FUSION_ADAPT_STRUCT(
	irc::message,
	(boost::optional<std::string>, prefix)
	(boost::optional<irc::message::numeric_reply>, command_n)
	(boost::optional<std::string>, command_s)
	(boost::optional<std::string>, params)
	)

namespace 
{
	template<typename Iterator>
	struct message_parser : qi::grammar < Iterator, irc::message(), ascii::space_type >
	{
		message_parser()
			:message_parser::base_type(irc_message)
		{
			using qi::int_;
			using qi::lit;
			using qi::lexeme;
			using qi::digit;
			using qi::hex;
			using qi::space;
			using qi::eol;
			using qi::repeat;
			using ascii::char_;
			command_s %= lexeme[+char_("a-zA-Z")];
			command_n %= int_;
			params %= space > lexeme[+(char_ - eol)];
			prefix %= lit(':') > lexeme[+(char_ - space)];
			irc_message %= -(prefix) >> -(space) >> (command_s | command_n) >> -(params) >> eol;
		}

		/*servername =  hostname
  host       =  hostname / hostaddr
  hostname   =  shortname *( "." shortname )
  shortname  =  ( letter / digit ) *( letter / digit / "-" )
                *( letter / digit )
		*/
		/*qi::rule<Iterator, std::string(), ascii::space_type> hexdigit;
		qi::rule<Iterator, std::string(), ascii::space_type> octet;
		qi::rule<Iterator, std::string(), ascii::space_type> ip6addr;
		qi::rule<Iterator, std::string(), ascii::space_type> ip4addr;*/
		qi::rule<Iterator, irc::message::numeric_reply(), ascii::space_type> command_n;
		qi::rule<Iterator, std::string(), ascii::space_type> command_s;
		qi::rule<Iterator, std::string(), ascii::space_type> prefix;
		qi::rule<Iterator, std::string(), ascii::space_type> params;
		qi::rule<Iterator, irc::message(), ascii::space_type> irc_message;
	};
}

namespace irc
{
	message parse(std::string inbound)
	{
		using boost::spirit::ascii::space;
		typedef std::string::const_iterator iterator_type;
		typedef message_parser<iterator_type> m_parser;

		m_parser parser;

		std::string::const_iterator iter = inbound.begin();
		std::string::const_iterator end = inbound.end();
		message m;
		bool r = phrase_parse(iter, end, parser, space, m);
		return m;
	}
}