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

#ifndef HEXCHAT_SERVER_HPP
#define HEXCHAT_SERVER_HPP

extern GSList *serv_list;
#include <string>
#include <utility>
#include <unordered_map>
#include <chrono>
#include <locale>
#include <boost/chrono.hpp>
#include <tcpfwd.hpp>

struct server
{
private:
	void reset_to_defaults();
	int death_timer;
	std::unordered_map<std::string, std::pair<bool, std::string> > away_map;
	std::locale locale_;
	friend server *server_new(void);
public:
	enum class cleanup_result{
		not_connected,
		still_connecting,
		connected,
		reconnecting
	};
public:
	server();
	// explict due to use of unique_ptr
	~server();
public:
	void imbue(const std::locale&);
public:
	/*  server control operations (in server*.c) */
	void connect(char *hostname, int port, bool no_login);
	void disconnect(struct session *, bool sendquit, int err);
	cleanup_result cleanup();
	void flush_queue();
	void auto_reconnect(bool send_quit, int err);
	/* irc protocol functions (in proto*.c) */
	void p_inline(char *buf, int len);
	void p_invite(const std::string& channel, const std::string &nick);
	void p_cycle(const std::string& channel, const std::string& key);
	void p_ctcp(const std::string & to, const std::string & msg);
	void p_nctcp(const std::string & to, const std::string & msg);
	void p_quit(const std::string& reason);
	void p_kick(const std::string& channel, const std::string &nick, const std::string & reason);
	void p_part(const std::string& channel, const std::string & reason);
	void p_ns_identify(const std::string &pass);
	void p_ns_ghost(const std::string& usname, const std::string& pass);
	void p_join(const std::string& channel, const std::string& key);
	void p_join_list(GSList *favorites);
	void p_join_list(const std::vector<favchannel> &favorites);
	void p_login(const std::string& user, const std::string& realname);
	void p_join_info(const std::string & channel);
	void p_mode(const std::string & target, const std::string &mode);
	void p_user_list(const std::string & channel);
	void p_away_status(const std::string & channel){ p_user_list(channel); }
	void p_whois(const std::string& nicks);
	void p_get_ip(const std::string &nick){ p_user_list(nick); }
	void p_get_ip_uh(const std::string &nick);
	void p_set_back();
	void p_set_away(const std::string & reason);
	void p_message(const std::string & channel, const std::string & text);
	void p_action(const std::string & channel, const std::string & act);
	void p_notice(const std::string & channel, const std::string & text);
	void p_topic(const std::string & channel, const char *topic);
	void p_list_channels(const std::string & arg, int min_users);
	void p_change_nick(const std::string & new_nick);
	void p_names(const std::string & channel);
	void p_ping(const std::string & to, const std::string & timestring);
	/*	void (*p_set_away)(struct server *);*/
	bool p_raw(const std::string & raw);
	int(*p_cmp)(const char *s1, const char *s2);
	int compare(const std::string & lhs, const std::string & rhs) const;

	void set_name(const std::string& name);
	void set_encoding(const char* new_encoding);
	// BUGBUG return const!!!
	char *get_network(bool fallback) const;
	// BUGBUG return const!!!
	boost::optional<session&> find_channel(const std::string &chan);
	bool is_channel_name(const std::string &chan) const;
	boost::optional<const std::pair<bool, std::string>& > get_away_message(const std::string & nick) const NOEXCEPT;
	void save_away_message(const std::string& nick, const boost::optional<std::string>& message);


	int port;
	int sok;					/* is equal to sok4 or sok6 (the one we are using) */
	int sok4;					/* tcp4 socket */
	int sok6;					/* tcp6 socket */
	int proxy_type;
	int proxy_sok;				/* Additional information for MS Proxy beast */
	int proxy_sok4;
	int proxy_sok6;
	struct msproxy_state_t msp_state;
	int id;					/* unique ID number (for plugin API) */
	std::unique_ptr<io::tcp::connection> server_connection;
#ifdef USE_OPENSSL
	SSL *ssl;
	int ssl_do_connect_tag;
#else
	void *ssl;
#endif
	int childread;
	int childwrite;
	int childpid;
	int iotag;
	int recondelay_tag;				/* reconnect delay timeout */
	int joindelay_tag;				/* waiting before we send JOIN */
	char hostname[128];				/* real ip number */
	char servername[128];			/* what the server says is its name */
	char password[86];
	char nick[NICKLEN];
	char linebuf[2048];				/* RFC says 512 chars including \r\n */
	std::string last_away_reason;
	int pos;								/* current position in linebuf */
	int nickcount;
	int loginmethod;					/* see login_types[] */

	std::string chantypes;					/* for 005 numeric - free me */
	std::string chanmodes;					/* for 005 numeric - free me */
	std::string nick_prefixes;				/* e.g. "*@%+" */
	std::string nick_modes;             /* e.g. "aohv" */
	std::string bad_nick_prefixes;		/* for ircd that doesn't give the modes */
	int modes_per_line;				/* 6 on undernet, 4 on efnet etc... */

	ircnet *network;						/* points to entry in servlist.c or NULL! */

	std::priority_queue<std::pair<int, std::string> > outbound_queue;
	time_t next_send;						/* cptr->since in ircu */
	time_t prev_now;					/* previous now-time */
	int sendq_len;						/* queue size */
	int lag;								/* milliseconds */

	struct session *front_session;	/* front-most window/tab */
	struct session *server_session;	/* server window/tab */

	struct server_gui *gui;		  /* initialized by fe_new_server */

	unsigned int ctcp_counter;	  /*flood */
	time_t ctcp_last_time;

	unsigned int msg_counter;	  /*counts the msg tab opened in a certain time */
	time_t msg_last_time;

	/*time_t connect_time;*/				/* when did it connect? */
	unsigned long lag_sent;   /* we are still waiting for this ping response*/
	boost::chrono::steady_clock::time_point ping_recv;					/* when we last got a ping reply */
	time_t away_time;					/* when we were marked away */

	boost::optional<std::string> encoding;					/* NULL for system */
	GSList *favlist;			/* list of channels & keys to join */

	bool motd_skipped;
	bool connected;
	bool connecting;
	bool no_login;
	bool skip_next_userhost;/* used for "get my ip from server" */
	bool skip_next_whois;	/* hide whois output */
	bool inside_whois;
	bool doing_dns;			/* /dns has been done */
	bool retry_sasl;		/* retrying another sasl mech */
	bool end_of_motd;		/* end of motd reached (logged in) */
	bool sent_quit;			/* sent a QUIT already? */
	bool use_listargs;		/* undernet and dalnet need /list >0,<10000 */
	bool is_away;
	bool reconnect_away;	/* whether to reconnect in is_away state */
	bool dont_use_proxy;	/* to proxy or not to proxy */
	bool supports_watch;	/* supports the WATCH command */
	bool supports_monitor;	/* supports the MONITOR command */
	bool bad_prefix;			/* gave us a bad PREFIX= 005 number */
	bool have_namesx;		/* 005 tokens NAMESX and UHNAMES */
	bool have_awaynotify;
	bool have_uhnames;
	bool have_whox;		/* have undernet's WHOX features */
	bool have_idmsg;		/* freenode's IDENTIFY-MSG */
	bool have_accnotify; /* cap account-notify */
	bool have_extjoin;	/* cap extended-join */
	bool have_server_time;	/* cap server-time */
	bool have_sasl;		/* SASL capability */
	bool have_except;	/* ban exemptions +e */
	bool have_invite;	/* invite exemptions +I */
	bool have_cert;	/* have loaded a cert */
	bool using_cp1255;	/* encoding is CP1255/WINDOWS-1255? */
	bool using_irc;		/* encoding is "IRC" (CP1252/UTF-8 hybrid)? */
	bool use_who;			/* whether to use WHO command to get dcc_ip */
	unsigned int sasl_mech;			/* mechanism for sasl auth */
	bool sent_saslauth;	/* have sent AUTHENICATE yet */
	bool sent_capend;	/* have sent CAP END yet */
#ifdef USE_OPENSSL
	bool use_ssl;				  /* is server SSL capable? */
	bool accept_invalid_cert;/* ignore result of server's cert. verify */
#endif
};
/* eventually need to keep the tcp_* functions isolated to server.c */
int tcp_send_len (server &serv, const char *buf, size_t len);
template<size_t N>
int tcp_send(server & serv, const char (&buf)[N])
{
	return tcp_send_len(serv, buf, N - 1);
}
void tcp_sendf (server &serv, const char *fmt, ...) G_GNUC_PRINTF (2, 3);
int tcp_send_real (void *ssl, int sok, const char *encoding, int using_irc, const char *buf, int len, server *);

server *server_new (void);
bool is_server (server *serv);
void server_fill_her_up (server &serv);
void server_free (server *serv);

void base64_encode (char *to, const char *from, unsigned int len);

#endif
