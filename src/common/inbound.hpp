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

#include "proto-irc.hpp"

#ifndef HEXCHAT_INBOUND_HPP
#define HEXCHAT_INBOUND_HPP
#include <string>

void inbound_next_nick (session *sess, char *nick, int error,
								const message_tags_data *tags_data);
void inbound_uback (server &serv, const message_tags_data *tags_data);
void inbound_uaway (server &serv, const message_tags_data *tags_data);
void inbound_account (const server &serv, const char *nick, const char *account,
							 const message_tags_data *tags_data);
void inbound_part (const server &serv, char *chan, char *user, char *ip, char *reason,
						 const message_tags_data *tags_data);
void inbound_upart (server &serv, char *chan, char *ip, char *reason,
						  const message_tags_data *tags_data);
void inbound_ukick (server &serv, char *chan, char *kicker, char *reason,
						  const message_tags_data *tags_data);
void inbound_kick (const server &serv, char *chan, char *user, char *kicker,
						 char *reason, const message_tags_data *tags_data);
void inbound_notice (server &serv, char *to, char *nick, char *msg, char *ip,
							int id, const message_tags_data *tags_data);
void inbound_quit (server &serv, char *nick, char *ip, char *reason,
						 const message_tags_data *tags_data);
void inbound_topicnew (const server &serv, char *nick, char *chan, char *topic,
							  const message_tags_data *tags_data);
void inbound_join (const server &serv, char *chan, char *user, char *ip, 
						 char *account, char *realname, 
						 const message_tags_data *tags_data);
void inbound_ujoin (server &serv, char *chan, char *nick, char *ip,
						  const message_tags_data *tags_data);
void inbound_topictime (server &serv, char *chan, char *nick, time_t stamp,
								const message_tags_data *tags_data);
void inbound_topic (server &serv, char *chan, char *topic_text,
						  const message_tags_data *tags_data);
void inbound_user_info_start (session *sess, const char *nick,
										const message_tags_data *tags_data);
void inbound_user_info (session *sess, char *chan, char *user, char *host,
								char *servname, char *nick, char *realname, char *account,
								unsigned int away, const message_tags_data *tags_data);
void inbound_foundip (session *sess, char *ip, 
							 const message_tags_data *tags_data);
bool inbound_banlist (session *sess, time_t stamp, char *chan, char *mask, 
							char *banner, int is_exemption,
							const message_tags_data *tags_data);
void inbound_ping_reply (session *sess, char *timestring, char *from,
								 const message_tags_data *tags_data);
void inbound_nameslist (server &serv, char *chan, char *names,
								const message_tags_data *tags_data);
bool inbound_nameslist_end (const server &serv, const std::string & chan,
									const message_tags_data *tags_data);
void inbound_away (server &serv, char *nick, char *msg,
						 const message_tags_data *tags_data);
void inbound_away_notify (const server &serv, char *nick, char *reason,
								  const message_tags_data *tags_data);
void inbound_login_start (session *sess, char *nick, char *servname,
								  const message_tags_data *tags_data);
void inbound_login_end (session *sess, char *text,
								const message_tags_data *tags_data);
void inbound_chanmsg (server &serv, session *sess, char *chan, char *from,
							 char *text, bool fromme, bool id, 
							 const message_tags_data *tags_data);
void clear_channel (session &sess);
void set_topic (session *sess, const std::string& topic, const std::string &stripped_topic);
void inbound_privmsg (server &serv, char *from, char *ip, char *text, bool id, 
							 const message_tags_data *tags_data);
void inbound_action (session *sess, const std::string & chan, char *from, char *ip,
							char *text, bool fromme, bool id,
							const message_tags_data *tags_data);
void inbound_newnick (server &serv, char *nick, char *newnick, int quiet,
							 const message_tags_data *tags_data);
void inbound_identified (server &serv);
void inbound_cap_ack (server &serv, char *nick, char *extensions,
							 const message_tags_data *tags_data);
void inbound_cap_ls (server &serv, char *nick, char *extensions,
							const message_tags_data *tags_data);
void inbound_cap_nak (server &serv, const message_tags_data *tags_data);
void inbound_cap_list (server &serv, char *nick, char *extensions,
							  const message_tags_data *tags_data);
void inbound_sasl_authenticate (server & serv, char *data);
int inbound_sasl_error (server &serv);
void inbound_sasl_supportedmechs (server &serv, char *list);
void do_dns (session *sess, const char nick[], const char host[],
				 const message_tags_data *tags_data);
bool alert_match_word (const char *word, const char *masks);
bool alert_match_text (const char text[], const char masks[]);

#endif
