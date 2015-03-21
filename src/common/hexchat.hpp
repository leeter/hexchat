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

#ifndef HEXCHAT_HPP
#define HEXCHAT_HPP

#include "config.h"

#include <glib.h>
#include <glib/gstdio.h>
#include <glib/gi18n.h>

#include <memory>
#include <string>
#include <vector>
#include <queue>
#include <utility>
#include <unordered_map>
#include <ctime>			/* need time_t */
#include <boost/optional/optional_fwd.hpp>

#include "sessfwd.hpp"

#ifndef NOEXCEPT
#if defined(_MSC_VER) && _MSC_VER < 1900
#define NOEXCEPT throw()
#else
#define NOEXCEPT noexcept
#endif
#endif

#ifdef USE_OPENSSL
#ifdef __APPLE__
#define __AVAILABILITYMACROS__
#define DEPRECATED_IN_MAC_OS_X_VERSION_10_7_AND_LATER
#endif
#endif

#ifndef HAVE_SNPRINTF
#define snprintf g_snprintf
#endif

#ifndef HAVE_VSNPRINTF
#define vsnprintf _vsnprintf
#endif

#ifdef SOCKS
#ifdef __sgi
#include <sys/time.h>
#define INCLUDE_PROTOTYPES 1
#endif
#include <socks.h>
#endif

#ifdef USE_OPENSSL
#include <openssl/ssl.h>		  /* SSL_() */
#endif

#ifdef __EMX__						  /* for o/s 2 */
#define OFLAGS O_BINARY
#define g_ascii_strcasecmp stricmp
#define g_ascii_strncasecmp strnicmp
#define PATH_MAX MAXPATHLEN
#define FILEPATH_LEN_MAX MAXPATHLEN
#endif

#ifdef WIN32						/* for win32 */
#define OFLAGS O_BINARY
#include <direct.h>
#define	F_OK	0
#define	X_OK	1
#define	W_OK	2
#define	R_OK	4
#ifndef S_ISDIR
#define	S_ISDIR(m)	((m) & _S_IFDIR)
#endif
#define NETWORK_PRIVATE
#else									/* for unix */
#define OFLAGS 0
#endif

const std::size_t FONTNAMELEN = 127;
const std::size_t PATHLEN = 255;
const std::size_t DOMAINLEN = 100;
const std::size_t PDIWORDS = 32;
const std::size_t USERNAMELEN = 10;
const char HIDDEN_CHAR = 8;			/* invisible character for xtext */

struct nbexec
{
	nbexec(session *);
	int myfd;
	int childpid;
	int tochannel;						/* making this int keeps the struct 4-byte aligned */
	int iotag;
	std::string linebuf;
	struct session *sess;
};

struct hexchatprefs
{
	/* these are the rebranded, consistent, sorted hexchat variables */

	/* BOOLEANS */
	unsigned int hex_away_auto_unmark;
	unsigned int hex_away_omit_alerts;
	unsigned int hex_away_show_once;
	unsigned int hex_away_track;
	unsigned int hex_completion_auto;
	unsigned int hex_dcc_auto_chat;
	unsigned int hex_dcc_auto_resume;
	unsigned int hex_dcc_fast_send;
	unsigned int hex_dcc_ip_from_server;
	unsigned int hex_dcc_remove;
	unsigned int hex_dcc_save_nick;
	unsigned int hex_dcc_send_fillspaces;
	unsigned int hex_gui_autoopen_chat;
	unsigned int hex_gui_autoopen_dialog;
	unsigned int hex_gui_autoopen_recv;
	unsigned int hex_gui_autoopen_send;
	unsigned int hex_gui_compact;
	unsigned int hex_gui_filesize_iec;
	unsigned int hex_gui_focus_omitalerts;
	unsigned int hex_gui_hide_menu;
	unsigned int hex_gui_input_attr;
	unsigned int hex_gui_input_icon;
	unsigned int hex_gui_input_nick;
	unsigned int hex_gui_input_spell;
	unsigned int hex_gui_input_style;
	unsigned int hex_gui_join_dialog;
	unsigned int hex_gui_mode_buttons;
	unsigned int hex_gui_quit_dialog;
	/* unsigned int hex_gui_single; */
	unsigned int hex_gui_slist_fav;
	unsigned int hex_gui_slist_skip;
	unsigned int hex_gui_tab_chans;
	unsigned int hex_gui_tab_dialogs;
	unsigned int hex_gui_tab_dots;
	unsigned int hex_gui_tab_icons;
	unsigned int hex_gui_tab_scrollchans;
	unsigned int hex_gui_tab_server;
	unsigned int hex_gui_tab_sort;
	unsigned int hex_gui_tab_utils;
	unsigned int hex_gui_topicbar;
	unsigned int hex_gui_tray;
	unsigned int hex_gui_tray_away;
	unsigned int hex_gui_tray_blink;
	unsigned int hex_gui_tray_close;
	unsigned int hex_gui_tray_minimize;
	unsigned int hex_gui_tray_quiet;
	unsigned int hex_gui_ulist_buttons;
	unsigned int hex_gui_ulist_color;
	unsigned int hex_gui_ulist_count;
	unsigned int hex_gui_ulist_hide;
	unsigned int hex_gui_ulist_icons;
	unsigned int hex_gui_ulist_resizable;
	unsigned int hex_gui_ulist_show_hosts;
	unsigned int hex_gui_ulist_style;
	unsigned int hex_gui_usermenu;
	unsigned int hex_gui_win_modes;
	unsigned int hex_gui_win_save;
	unsigned int hex_gui_win_swap;
	unsigned int hex_gui_win_ucount;
	unsigned int hex_identd;
	unsigned int hex_input_balloon_chans;
	unsigned int hex_input_balloon_hilight;
	unsigned int hex_input_balloon_priv;
	unsigned int hex_input_beep_chans;
	unsigned int hex_input_beep_hilight;
	unsigned int hex_input_beep_priv;
	unsigned int hex_input_filter_beep;
	unsigned int hex_input_flash_chans;
	unsigned int hex_input_flash_hilight;
	unsigned int hex_input_flash_priv;
	unsigned int hex_input_perc_ascii;
	unsigned int hex_input_perc_color;
	unsigned int hex_input_tray_chans;
	unsigned int hex_input_tray_hilight;
	unsigned int hex_input_tray_priv;
	unsigned int hex_irc_auto_rejoin;
	unsigned int hex_irc_conf_mode;
	unsigned int hex_irc_hidehost;
	unsigned int hex_irc_hide_nickchange;
	unsigned int hex_irc_hide_version;
	unsigned int hex_irc_invisible;
	unsigned int hex_irc_logging;
	unsigned int hex_irc_raw_modes;
	unsigned int hex_irc_servernotice;
	unsigned int hex_irc_skip_motd;
	unsigned int hex_irc_wallops;
	unsigned int hex_irc_who_join;
	unsigned int hex_irc_whois_front;
	unsigned int hex_irc_cap_server_time;
	unsigned int hex_net_auto_reconnect;
	unsigned int hex_net_auto_reconnectonfail;
	unsigned int hex_net_proxy_auth;
	unsigned int hex_net_throttle;
	unsigned int hex_notify_whois_online;
	unsigned int hex_perl_warnings;
	unsigned int hex_stamp_log;
	unsigned int hex_stamp_text;
	unsigned int hex_text_autocopy_color;
	unsigned int hex_text_autocopy_stamp;
	unsigned int hex_text_autocopy_text;
	unsigned int hex_text_color_nicks;
	unsigned int hex_text_indent;
	unsigned int hex_text_replay;
	unsigned int hex_text_search_case_match;
	unsigned int hex_text_search_highlight_all;
	unsigned int hex_text_search_follow;
	unsigned int hex_text_search_regexp;
	unsigned int hex_text_show_marker;
	unsigned int hex_text_show_sep;
	unsigned int hex_text_stripcolor_msg;
	unsigned int hex_text_stripcolor_replay;
	unsigned int hex_text_stripcolor_topic;
	unsigned int hex_text_thin_sep;
	unsigned int hex_text_transparent;
	unsigned int hex_text_wordwrap;
	unsigned int hex_url_grabber;
	unsigned int hex_url_logging;

	/* NUMBERS */
	int hex_away_size_max;
	int hex_away_timeout;
	int hex_completion_amount;
	int hex_completion_sort;
	int hex_dcc_auto_recv;
	int hex_dcc_blocksize;
	int hex_dcc_global_max_get_cps;
	int hex_dcc_global_max_send_cps;
	int hex_dcc_max_get_cps;
	int hex_dcc_max_send_cps;
	int hex_dcc_permissions;
	int hex_dcc_port_first;
	int hex_dcc_port_last;
	int hex_dcc_stall_timeout;
	int hex_dcc_timeout;
	int hex_flood_ctcp_num;				/* flood */
	int hex_flood_ctcp_time;			/* seconds of floods */
	int hex_flood_msg_num;				/* same deal */
	int hex_flood_msg_time;
	int hex_gui_chanlist_maxusers;
	int hex_gui_chanlist_minusers;
	int hex_gui_dialog_height;
	int hex_gui_dialog_left;
	int hex_gui_dialog_top;
	int hex_gui_dialog_width;
	int hex_gui_lagometer;
	int hex_gui_lang;
	int hex_gui_pane_divider_position;
	int hex_gui_pane_left_size;
	int hex_gui_pane_right_size;
	int hex_gui_pane_right_size_min;
	int hex_gui_search_pos;
	int hex_gui_slist_select;
	int hex_gui_tab_layout;
	int hex_gui_tab_middleclose;
	int hex_gui_tab_newtofront;
	int hex_gui_tab_pos;
	int hex_gui_tab_small;
	int hex_gui_tab_trunc;
	int hex_gui_transparency;
	int hex_gui_throttlemeter;
	int hex_gui_ulist_pos;
	int hex_gui_ulist_sort;
	int hex_gui_url_mod;
	int hex_gui_win_height;
	int hex_gui_win_fullscreen;
	int hex_gui_win_left;
	int hex_gui_win_state;
	int hex_gui_win_top;
	int hex_gui_win_width;
	int hex_input_balloon_time;
	int hex_irc_ban_type;
	int hex_irc_join_delay;
	int hex_irc_notice_pos;
	int hex_net_ping_timeout;
	int hex_net_proxy_port;
	int hex_net_proxy_type;				/* 0=disabled, 1=wingate 2=socks4, 3=socks5, 4=http */
	int hex_net_proxy_use;				/* 0=all 1=IRC_ONLY 2=DCC_ONLY */
	int hex_net_reconnect_delay;
	int hex_notify_timeout;
	int hex_text_max_indent;
	int hex_text_max_lines;
	int hex_url_grabber_limit;

	/* STRINGS */
	char hex_away_reason[256];
	char hex_completion_suffix[4];		/* Only ever holds a one-character string. */
	char hex_dcc_completed_dir[PATHLEN + 1];
	char hex_dcc_dir[PATHLEN + 1];
	char hex_dcc_ip[DOMAINLEN + 1];
	char hex_gui_ulist_doubleclick[256];
	char hex_input_command_char[4];
	char hex_irc_extra_hilight[300];
	char hex_irc_id_ntext[64];
	char hex_irc_id_ytext[64];
	char hex_irc_logmask[256];
	char hex_irc_nick1[NICKLEN];
	char hex_irc_nick2[NICKLEN];
	char hex_irc_nick3[NICKLEN];
	char hex_irc_nick_hilight[300];
	char hex_irc_no_hilight[300];
	char hex_irc_part_reason[256];
	char hex_irc_quit_reason[256];
	char hex_irc_real_name[127];
	char hex_irc_user_name[127];
	char hex_net_bind_host[127];
	char hex_net_proxy_host[64];
	char hex_net_proxy_pass[32];
	char hex_net_proxy_user[32];
	char hex_stamp_log_format[64];
	char hex_stamp_text_format[64];
	char hex_text_background[PATHLEN + 1];
	char hex_text_font[4 * FONTNAMELEN + 1];
	char hex_text_font_main[FONTNAMELEN + 1];
	char hex_text_font_alternative[3 * FONTNAMELEN + 1];
	char hex_text_spell_langs[64];

	/* these are the private variables */
	guint32 local_ip;
	guint32 dcc_ip;

	unsigned int wait_on_exit;	/* wait for logs to be flushed to disk IF we're connected */
	unsigned int utf8_locale;

	/* Tells us if we need to save, only when they've been edited.
		This is so that we continue using internal defaults (which can
		change in the next release) until the user edits them. */
	unsigned int save_pevents:1;
};

/* Per-Channel Settings */
enum chanopt_val{
	SET_OFF = 0,
	SET_ON = 1,
	SET_DEFAULT = 2 /* use global setting */
};

/* Priorities in the "interesting sessions" priority queue
 * (see xchat.c:sess_list_by_lastact) */
enum lact{
	LACT_NONE		= -1,		/* no queues */
	LACT_QUERY_HI	= 0,	/* query with hilight */
	LACT_QUERY		= 1,	/* query with messages */
	LACT_CHAN_HI	= 2,	/* channel with hilight */
	LACT_CHAN		= 3,	/* channel with messages */
	LACT_CHAN_DATA	= 4	/* channel with other data */
};

/* SASL Mechanisms */
enum sasl_mech
{
	MECH_PLAIN = 0,
	MECH_BLOWFISH,
	MECH_AES,
	MECH_EXTERNAL
};

struct ircnet;
struct favchannel;

typedef int (*cmd_callback) (struct session * sess, char *tbuf, char *word[],
									  char *word_eol[]);

struct commands
{
	const char *name;
	cmd_callback callback;
	char needserver;
	char needchannel;
	gint16 handle_quotes;
	const char *help;
};

/* not just for popups, but used for usercommands, ctcp replies,
   userlist buttons etc */

struct popup
{
	std::string cmd;
	std::string name;
};

struct glib_deleter
{
	void operator()(gpointer ptr) NOEXCEPT
	{
		g_free(ptr);
	}
};

typedef std::unique_ptr<gchar[], glib_deleter> glib_string;

/* CL: get a random int in the range [0..n-1]. DON'T use rand() % n, it gives terrible results. */
int RAND_INT(int n); 

#endif
