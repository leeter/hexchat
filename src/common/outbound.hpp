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

#ifndef HEXCHAT_OUTBOUND_HPP
#define HEXCHAT_OUTBOUND_HPP

#include <string>
#include <boost/optional.hpp>
#include "hexchat.hpp"

extern const struct commands xc_cmds[];
extern GSList *menu_list;

int auto_insert (char *dest, int destlen, const unsigned char *src, char *word[], char *word_eol[],
				 const char *a, const char *c, const char *d, const char *e, const char *h,const char *n, const char *s, const char *u);
char *command_insert_vars (session *sess, char *cmd);
int handle_command (session *sess, char *cmd, int check_spch);
void process_data_init (char *buf, char *cmd, char *word[], char *word_eol[], gboolean handle_quotes, gboolean allow_escape_quotes);
void handle_multiline (session *sess, char *cmd, int history, int nocommand);
void check_special_chars (char *cmd, int do_ascii);
void notc_msg (session *sess);
void server_sendpart(server & serv, const std::string& channel, const std::string* reason);
void server_sendquit (session * sess);
int menu_streq (const char *s1, const char *s2, int def);
session *open_query (server *serv, const char *nick, gboolean focus_existing);
gboolean load_perform_file (session *sess, char *file);

#endif
