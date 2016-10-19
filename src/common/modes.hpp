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



#ifndef HEXCHAT_MODES_HPP
#define HEXCHAT_MODES_HPP

#include <string>
#include <vector>
#include <gsl.h>
#include "proto-irc.hpp"
#include "serverfwd.hpp"

char get_nick_prefix (const server *serv, unsigned int access);
unsigned int nick_access (const server &serv, gsl::cstring_span<> nick, std::ptrdiff_t &modechars);
int mode_access (const server *serv, char mode, char *prefix);
void inbound_005 (server &serv, gsl::span<char*> word);
void handle_mode (server &serv, char *word[], char *word_eol[], char *nick,
						bool numeric_324, const message_tags_data *tags_data);
void send_channel_modes(session *sess, const std::vector<std::string> &word, int start, int end, char sign, char mode, int modes_per_line);

#endif
