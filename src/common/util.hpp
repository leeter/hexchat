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

#include <cstddef>
#include <functional>
#include <string>

#define rfc_tolower(c) (rfc_tolowertab[(unsigned char)(c)])

extern const unsigned char rfc_tolowertab[];

char *expand_homedir (char *file);
void path_part (char *file, char *path, int pathlen);
int match (const char *mask, const char *string);
bool match_with_wildcards(const std::string &text, std::string wildcardPattern, bool caseSensitive /*= true*/);
char *file_part (char *file);
void for_files (const char *dirname, const char *mask, const std::function<void (char* file)>& callback);
int rfc_casecmp (const char *, const char *);
int rfc_ncasecmp (char *, char *, int);
int buf_get_line (char *, char **, int *, int len);
char *nocasestrstr (const char *text, const char *tofind);
const char *country (const char *);
void country_search (char *pattern, void *ud, void (*print)(void *, char *, ...));
char *get_sys_str (bool with_cpu);
void util_exec (const char *cmd);
enum strip_flags{
	STRIP_COLOR = 1,
	STRIP_ATTRIB = 2,
	STRIP_HIDDEN = 4,
	STRIP_ESCMARKUP = 8,
	STRIP_ALL = 7
};
//#define STRIP_COLOR 1
//#define STRIP_ATTRIB 2
//#define STRIP_HIDDEN 4
//#define STRIP_ESCMARKUP 8
//#define STRIP_ALL 7
gchar *strip_color (const char *text, int len, strip_flags flags);
int strip_color2(const char *src, int len, char *dst, strip_flags flags);
int strip_hidden_attribute (const std::string & src, char *dst);
char *errorstring (int err);
int waitline (int sok, char *buf, int bufsize, int);
#ifdef WIN32
int waitline2 (GIOChannel *source, char *buf, int bufsize);
int get_cpu_arch (void);
#else
#define waitline2(source,buf,size) waitline(serv->childread,buf,size,0)
#endif
unsigned long make_ping_time (void);
void move_file (const char *src_dir, const char *dst_dir, const char *fname, int dccpermissions);
int token_foreach (char *str, char sep, int (*callback) (char *str, void *ud), void *ud);
guint32 str_hash (const char *key);
guint32 str_ihash (const unsigned char *key);
void safe_strcpy (char *dest, const char *src, std::size_t bytes_left);
void canonalize_key (char *key);
bool portable_mode ();
int unity_mode ();
char *encode_sasl_pass_plain (const char *user, const char *pass);
char *encode_sasl_pass_blowfish (char *user, char *pass, char *data);
char *encode_sasl_pass_aes (char *user, char *pass, char *data);
char *challengeauth_response (char *username, char *password, char *challenge);
size_t strftime_validated (char *dest, size_t destsize, const char *format, const struct tm *time);
size_t strftime_utf8 (char *dest, size_t destsize, const char *format, time_t time);

#endif
