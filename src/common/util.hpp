/* HexChat
 * Copyright (C) 1998-2010 Peter Zelezny.
 * Copyright (C) 2009-2013 Berke Viktor.
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

/************************************************************************
 *    This technique was borrowed in part from the source code to 
 *    ircd-hybrid-5.3 to implement case-insensitive string matches which
 *    are fully compliant with Section 2.2 of RFC 1459, the copyright
 *    of that code being (C) 1990 Jarkko Oikarinen and under the GPL.
 *    
 *    A special thanks goes to Mr. Okarinen for being the one person who
 *    seems to have ever noticed this section in the original RFC and
 *    written code for it.  Shame on all the rest of you (myself included).
 *    
 *        --+ Dagmar d'Surreal
 */

#ifndef HEXCHAT_UTIL_HPP
#define HEXCHAT_UTIL_HPP

#include <locale>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <boost/format/format_fwd.hpp>
#include <boost/utility/string_ref_fwd.hpp>

#include "sessfwd.hpp"

#define rfc_tolower(c) (rfc_tolowertab[(unsigned char)(c)])

extern const unsigned char rfc_tolowertab[];

std::locale rfc_locale(const std::locale& locale);

char *expand_homedir (char *file);
void path_part (char *file, char *path, int pathlen);
bool match (const char *mask, const char *string);
bool match_with_wildcards(const std::string &text, std::string wildcardPattern, bool caseSensitive /*= true*/);
char *file_part (char *file);
void for_files (const char *dirname, const char *mask, const std::function<void (char* file)>& callback);
int rfc_casecmp (const char *, const char *);
int rfc_ncasecmp (const char *, const char *, size_t);
int buf_get_line (char *, char **, int *, int len);
char *nocasestrstr (const char *text, const char *tofind);
std::string country (const boost::string_ref &);
void country_search(char *pattern, session *ud, const std::function<void(session*, const boost::format &)> & print);
const char *get_sys_str (bool with_cpu);
void util_exec (const char *cmd);
enum strip_flags{
	STRIP_COLOR = 1,
	STRIP_ATTRIB = 2,
	STRIP_HIDDEN = 4,
	STRIP_ESCMARKUP = 8,
	STRIP_ALL = 7
};

std::string strip_color(const boost::string_ref &text, strip_flags flags);
std::string strip_color2(const boost::string_ref &src, strip_flags flags);
int strip_hidden_attribute (const std::string & src, char *dst);
const char *errorstring (int err);
int waitline (int sok, char *buf, int bufsize, int);
#ifdef WIN32
//int waitline2 (GIOChannel *source, char *buf, int bufsize);
int get_cpu_arch (void);
#else
#define waitline2(source,buf,size) waitline(serv->childread,buf,size,0)
#endif
unsigned long make_ping_time (void);
void move_file (const std::string& src_dir, const std::string& dst_dir, const std::string& fname, int dccpermissions);
int token_foreach (char *str, char sep, int (*callback) (char *str, void *ud), void *ud);
std::uint32_t str_hash (const char *key);
std::uint32_t str_ihash(const unsigned char *key);
void safe_strcpy (char *dest, const char *src, std::size_t bytes_left);
template<size_t N>
void safe_strcpy(char(&dest)[N], const char src[])
{
	safe_strcpy(dest, src, N);
}
void canonalize_key (char *key);
bool portable_mode ();
bool unity_mode ();
char *encode_sasl_pass_plain (const char *user, const char *pass);
char *encode_sasl_pass_blowfish (const std::string & user, const std::string& pass, const std::string & data);
char *encode_sasl_pass_aes (char *user, char *pass, char *data);
std::string challengeauth_response(const boost::string_ref & username, const boost::string_ref & password, const std::string & challenge);
size_t strftime_validated (char *dest, size_t destsize, const char *format, const struct tm *time);
size_t strftime_utf8 (char *dest, size_t destsize, const char *format, time_t time);

char* new_strdup(const char in[]);
char* new_strdup(const char in[], std::size_t len);

template<size_t N>
char* new_strdup(const char(&in)[N])
{
	return new_strdup(in, N - 1);
}

std::vector<std::string> to_vector_strings(const char *const in[], size_t len);

#endif
