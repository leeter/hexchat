/* X-Chat
 * Copyright (C) 1998 Peter Zelezny.
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
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif
#include <algorithm>
#include <iterator>
#include <fcntl.h>
#include <memory>
#include <sstream>
#include <string>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <sys/types.h>
#include <sys/stat.h>
#include <boost/filesystem.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/format.hpp>

#include "hexchat.hpp"
#include "cfgfiles.hpp"
#include "util.hpp"
#include "fe.hpp"
#include "text.hpp"
#include "hexchatc.hpp"
#include "charset_helpers.hpp"
#include "filesystem.hpp"

#ifdef WIN32
#define STRICT_TYPED_ITEMIDS
#include <io.h>
#else
#include <unistd.h>
#define HEXCHAT_DIR "hexchat"
#endif

namespace bio = boost::iostreams;
namespace fs = boost::filesystem;

const std::string DEF_FONT("Monospace 9");
const std::string DEF_FONT_ALTER("Arial Unicode MS,Lucida Sans Unicode,MS Gothic,Unifont");

const char * const languages[LANGUAGES_LENGTH] = {
	"af", "sq", "am", "ast", "az", "eu", "be", "bg", "ca", "zh_CN",      /*  0 ..  9 */
	"zh_TW", "cs", "da", "nl", "en_GB", "en", "et", "fi", "fr", "gl",    /* 10 .. 19 */
	"de", "el", "gu", "hi", "hu", "id", "it", "ja_JP", "kn", "rw",       /* 20 .. 29 */
	"ko", "lv", "lt", "mk", "ml", "ms", "nb", "no", "pl", "pt",          /* 30 .. 39 */
	"pt_BR", "pa", "ru", "sr", "sk", "sl", "es", "sv", "th", "tr",       /* 40 .. 49 */
	"uk", "vi", "wa"                                                     /* 50 .. */
};

void
list_addentry (GSList ** list, const char *cmd, const char *name)
{
	struct popup *pop = new popup;
	pop->name = name;
	if (cmd)
		pop->cmd = cmd;

	*list = g_slist_append (*list, pop);
}

/* read it in from a buffer to our linked list */

static void
list_load_from_data (GSList ** list, const std::string &ibuf)
{
	char cmd[384];
	char name[128];

	cmd[0] = 0;
	name[0] = 0;

	std::istringstream buffer(ibuf);

	for (std::string buf; std::getline(buffer, buf, '\n');)
	{
		if (buf[0] != '#')
		{
			if (!g_ascii_strncasecmp (buf.c_str(), "NAME ", 5))
			{
				safe_strcpy (name, buf.c_str() + 5, sizeof (name));
			}
			else if (!g_ascii_strncasecmp (buf.c_str(), "CMD ", 4))
			{
				safe_strcpy (cmd, buf.c_str() + 4, sizeof (cmd));
				if (*name)
				{
					list_addentry (list, cmd, name);
					cmd[0] = 0;
					name[0] = 0;
				}
			}
		}
	}
}

void
list_loadconf (const char *file, GSList ** list, const char *defaultconf)
{
	char *filebuf;
	int fd;
	struct stat st;

	filebuf = g_build_filename (get_xdir (), file, NULL);
	fd = g_open (filebuf, O_RDONLY | OFLAGS, 0);
	g_free (filebuf);

	if (fd == -1)
	{
		if (defaultconf)
		{
			list_load_from_data(list, defaultconf);
		}			
		return;
	}
	if (fstat (fd, &st) != 0)
	{
		perror ("fstat");
		abort ();
	}

	std::string ibuf(st.st_size, '\0');
	read (fd, &ibuf[0], st.st_size);
	close (fd);

	list_load_from_data (list, ibuf);
}

void
list_free (GSList ** list)
{
	while (*list)
	{
		popup *data = (popup *) (*list)->data;
		*list = g_slist_remove(*list, data);
		delete data;
	}
}

bool
list_delentry (GSList ** list, char *name)
{
	struct popup *pop;
	GSList *alist = *list;

	while (alist)
	{
		pop = (struct popup *) alist->data;
		if (!g_ascii_strcasecmp (name, pop->name.c_str()))
		{
			*list = g_slist_remove (*list, pop);
			delete pop;
			return true;
		}
		alist = alist->next;
	}
	return false;
}

char *
cfg_get_str (char *cfg, const char *var, char *dest, int dest_len)
{
	char buffer[128];	/* should be plenty for a variable name */

	snprintf (buffer, sizeof(buffer), "%s ", var);	/* add one space, this way it works against var - var2 checks too */

	while (1)
	{
		auto var_len = strlen(var);
		if (!g_ascii_strncasecmp (buffer, cfg, var_len + 1))
		{
			char *value, t;
			cfg += var_len;
			while (*cfg == ' ')
				cfg++;
			if (*cfg == '=')
				cfg++;
			while (*cfg == ' ')
				cfg++;
			/*while (*cfg == ' ' || *cfg == '=')
			   cfg++; */
			value = cfg;
			while (*cfg != 0 && *cfg != '\n')
				cfg++;
			t = *cfg;
			*cfg = 0;
			safe_strcpy (dest, value, dest_len);
			*cfg = t;
			return cfg;
		}
		while (*cfg != 0 && *cfg != '\n')
			cfg++;
		if (*cfg == 0)
			return 0;
		cfg++;
		if (*cfg == 0)
			return 0;
	}
}

static int
cfg_put_str (int fh, const char *var, const char *value)
{
	char buf[512];
	int len;

	snprintf (buf, sizeof buf, "%s = %s\n", var, value);
	len = strlen (buf);
	return (write (fh, buf, len) == len);
}

int
cfg_put_color (int fh, int r, int g, int b, char *var)
{
	char buf[400];
	int len;

	snprintf (buf, sizeof buf, "%s = %04x %04x %04x\n", var, r, g, b);
	len = strlen (buf);
	return (write (fh, buf, len) == len);
}

int
cfg_put_int (int fh, int value, const char *var)
{
	char buf[400];
	int len;

	if (value == -1)
		value = 1;

	snprintf (buf, sizeof buf, "%s = %d\n", var, value);
	len = strlen (buf);
	return (write (fh, buf, len) == len);
}

int
cfg_get_color (char *cfg, char *var, int *r, int *g, int *b)
{
	char str[128];

	if (!cfg_get_str (cfg, var, str, sizeof (str)))
		return 0;

	sscanf (str, "%04x %04x %04x", r, g, b);
	return 1;
}

int
cfg_get_int_with_result (char *cfg, const char *var, int *result)
{
	char str[128];

	if (!cfg_get_str (cfg, var, str, sizeof (str)))
	{
		*result = 0;
		return 0;
	}

	*result = 1;
	return atoi (str);
}

int
cfg_get_int (char *cfg, char *var)
{
	char str[128];

	if (!cfg_get_str (cfg, var, str, sizeof (str)))
		return 0;

	return atoi (str);
}


char *xdir = NULL;	/* utf-8 encoding */

#ifdef WIN32
#include <Windows.h>
#include <ShlObj.h>
#endif

char *
get_xdir (void)
{
	if (!xdir)
	{
#ifndef WIN32
		xdir = g_build_filename (g_get_user_config_dir (), HEXCHAT_DIR, NULL);
#else

		wchar_t* roaming_path_wide = nullptr;

		if (portable_mode () || SHGetKnownFolderPath (FOLDERID_RoamingAppData, 0, NULL, &roaming_path_wide) != S_OK)
		{
			std::wstring exe_path(2048, L'\0');
			DWORD size = GetModuleFileNameW(nullptr, &exe_path[0], exe_path.size());
			if (size)
			{
				exe_path.resize(size);
				auto path = fs::canonical(exe_path);
				path = path.remove_filename();
				path /= L"config";
				xdir = g_strdup(charset::narrow(path.wstring()).c_str());
			}
			else
				xdir = g_strdup (".\\config");
		}
		else
		{
			std::unique_ptr<wchar_t, decltype(&::CoTaskMemFree)> wide_roaming_path(roaming_path_wide, &::CoTaskMemFree);

			fs::path roaming_path(roaming_path_wide);
			roaming_path /= L"HexChat";

			xdir = g_strdup(charset::narrow(roaming_path.wstring()).c_str());
		}
#endif
	}

	return xdir;
}

namespace config
{
	const ::std::string& config_dir()
	{
		static const std::string config_dir(get_xdir());
		return config_dir;
	}
}

int
check_config_dir (void)
{
	return g_access (get_xdir (), F_OK);
}

static char *
default_file (void)
{
	static char *dfile = NULL;

	if (!dfile)
	{
		dfile = g_build_filename (get_xdir (), "hexchat.conf", NULL);
	}
	return dfile;
}

/* Keep these sorted!! */

struct prefs vars[] =
{
	{"away_auto_unmark", P_OFFINT (hex_away_auto_unmark), TYPE_BOOL},
	{"away_omit_alerts", P_OFFINT (hex_away_omit_alerts), TYPE_BOOL},
	{"away_reason", P_OFFSET (hex_away_reason), TYPE_STR},
	{"away_show_once", P_OFFINT (hex_away_show_once), TYPE_BOOL},
	{"away_size_max", P_OFFINT (hex_away_size_max), TYPE_INT},
	{"away_timeout", P_OFFINT (hex_away_timeout), TYPE_INT},
	{"away_track", P_OFFINT (hex_away_track), TYPE_BOOL},

	{"completion_amount", P_OFFINT (hex_completion_amount), TYPE_INT},
	{"completion_auto", P_OFFINT (hex_completion_auto), TYPE_BOOL},
	{"completion_sort", P_OFFINT (hex_completion_sort), TYPE_INT},
	{"completion_suffix", P_OFFSET (hex_completion_suffix), TYPE_STR},

	{"dcc_auto_chat", P_OFFINT (hex_dcc_auto_chat), TYPE_BOOL},
	{"dcc_auto_recv", P_OFFINT (hex_dcc_auto_recv), TYPE_INT},
	{"dcc_auto_resume", P_OFFINT (hex_dcc_auto_resume), TYPE_BOOL},
	{"dcc_blocksize", P_OFFINT (hex_dcc_blocksize), TYPE_INT},
	{"dcc_completed_dir", P_OFFSET (hex_dcc_completed_dir), TYPE_STR},
	{"dcc_dir", P_OFFSET (hex_dcc_dir), TYPE_STR},
#ifndef WIN32
	{"dcc_fast_send", P_OFFINT (hex_dcc_fast_send), TYPE_BOOL},
#endif
	{"dcc_global_max_get_cps", P_OFFINT (hex_dcc_global_max_get_cps), TYPE_INT},
	{"dcc_global_max_send_cps", P_OFFINT (hex_dcc_global_max_send_cps), TYPE_INT},
	{"dcc_ip", P_OFFSET (hex_dcc_ip), TYPE_STR},
	{"dcc_ip_from_server", P_OFFINT (hex_dcc_ip_from_server), TYPE_BOOL},
	{"dcc_max_get_cps", P_OFFINT (hex_dcc_max_get_cps), TYPE_INT},
	{"dcc_max_send_cps", P_OFFINT (hex_dcc_max_send_cps), TYPE_INT},
	{"dcc_permissions", P_OFFINT (hex_dcc_permissions), TYPE_INT},
	{"dcc_port_first", P_OFFINT (hex_dcc_port_first), TYPE_INT},
	{"dcc_port_last", P_OFFINT (hex_dcc_port_last), TYPE_INT},
	{"dcc_remove", P_OFFINT (hex_dcc_remove), TYPE_BOOL},
	{"dcc_save_nick", P_OFFINT (hex_dcc_save_nick), TYPE_BOOL},
	{"dcc_send_fillspaces", P_OFFINT (hex_dcc_send_fillspaces), TYPE_BOOL},
	{"dcc_stall_timeout", P_OFFINT (hex_dcc_stall_timeout), TYPE_INT},
	{"dcc_timeout", P_OFFINT (hex_dcc_timeout), TYPE_INT},

	{"flood_ctcp_num", P_OFFINT (hex_flood_ctcp_num), TYPE_INT},
	{"flood_ctcp_time", P_OFFINT (hex_flood_ctcp_time), TYPE_INT},
	{"flood_msg_num", P_OFFINT (hex_flood_msg_num), TYPE_INT},
	{"flood_msg_time", P_OFFINT (hex_flood_msg_time), TYPE_INT},

	{"gui_autoopen_chat", P_OFFINT (hex_gui_autoopen_chat), TYPE_BOOL},
	{"gui_autoopen_dialog", P_OFFINT (hex_gui_autoopen_dialog), TYPE_BOOL},
	{"gui_autoopen_recv", P_OFFINT (hex_gui_autoopen_recv), TYPE_BOOL},
	{"gui_autoopen_send", P_OFFINT (hex_gui_autoopen_send), TYPE_BOOL},
	{"gui_chanlist_maxusers", P_OFFINT (hex_gui_chanlist_maxusers), TYPE_INT},
	{"gui_chanlist_minusers", P_OFFINT (hex_gui_chanlist_minusers), TYPE_INT},
	{"gui_compact", P_OFFINT (hex_gui_compact), TYPE_BOOL},
	{"gui_dialog_height", P_OFFINT (hex_gui_dialog_height), TYPE_INT},
	{"gui_dialog_left", P_OFFINT (hex_gui_dialog_left), TYPE_INT},
	{"gui_dialog_top", P_OFFINT (hex_gui_dialog_top), TYPE_INT},
	{"gui_dialog_width", P_OFFINT (hex_gui_dialog_width), TYPE_INT},
	{"gui_filesize_iec", P_OFFINT (hex_gui_filesize_iec), TYPE_BOOL},
	{"gui_focus_omitalerts", P_OFFINT (hex_gui_focus_omitalerts), TYPE_BOOL},
	{"gui_hide_menu", P_OFFINT (hex_gui_hide_menu), TYPE_BOOL},
	{"gui_input_attr", P_OFFINT (hex_gui_input_attr), TYPE_BOOL},
	{"gui_input_icon", P_OFFINT (hex_gui_input_icon), TYPE_BOOL},
	{"gui_input_nick", P_OFFINT (hex_gui_input_nick), TYPE_BOOL},
	{"gui_input_spell", P_OFFINT (hex_gui_input_spell), TYPE_BOOL},
	{"gui_input_style", P_OFFINT (hex_gui_input_style), TYPE_BOOL},
	{"gui_join_dialog", P_OFFINT (hex_gui_join_dialog), TYPE_BOOL},
	{"gui_lagometer", P_OFFINT (hex_gui_lagometer), TYPE_INT},
	{"gui_lang", P_OFFINT (hex_gui_lang), TYPE_INT},
	{"gui_mode_buttons", P_OFFINT (hex_gui_mode_buttons), TYPE_BOOL},
	{"gui_pane_divider_position", P_OFFINT (hex_gui_pane_divider_position), TYPE_INT},
	{"gui_pane_left_size", P_OFFINT (hex_gui_pane_left_size), TYPE_INT},
	{"gui_pane_right_size", P_OFFINT (hex_gui_pane_right_size), TYPE_INT},
	{"gui_pane_right_size_min", P_OFFINT (hex_gui_pane_right_size_min), TYPE_INT},
	{"gui_quit_dialog", P_OFFINT (hex_gui_quit_dialog), TYPE_BOOL},
	{"gui_search_pos", P_OFFINT (hex_gui_search_pos), TYPE_INT},
	/* {"gui_single", P_OFFINT (hex_gui_single), TYPE_BOOL}, */
	{"gui_slist_fav", P_OFFINT (hex_gui_slist_fav), TYPE_BOOL},
	{"gui_slist_select", P_OFFINT (hex_gui_slist_select), TYPE_INT},
	{"gui_slist_skip", P_OFFINT (hex_gui_slist_skip), TYPE_BOOL},
	{"gui_tab_chans", P_OFFINT (hex_gui_tab_chans), TYPE_BOOL},
	{"gui_tab_dialogs", P_OFFINT (hex_gui_tab_dialogs), TYPE_BOOL},
	{"gui_tab_dots", P_OFFINT (hex_gui_tab_dots), TYPE_BOOL},
	{"gui_tab_icons", P_OFFINT (hex_gui_tab_icons), TYPE_BOOL},
	{"gui_tab_layout", P_OFFINT (hex_gui_tab_layout), TYPE_INT},
	{"gui_tab_middleclose", P_OFFINT (hex_gui_tab_middleclose), TYPE_BOOL},
	{"gui_tab_newtofront", P_OFFINT (hex_gui_tab_newtofront), TYPE_INT},
	{"gui_tab_pos", P_OFFINT (hex_gui_tab_pos), TYPE_INT},
	{"gui_tab_scrollchans", P_OFFINT (hex_gui_tab_scrollchans), TYPE_BOOL},
	{"gui_tab_server", P_OFFINT (hex_gui_tab_server), TYPE_BOOL},
	{"gui_tab_small", P_OFFINT (hex_gui_tab_small), TYPE_INT},
	{"gui_tab_sort", P_OFFINT (hex_gui_tab_sort), TYPE_BOOL},
	{"gui_tab_trunc", P_OFFINT (hex_gui_tab_trunc), TYPE_INT},
	{"gui_tab_utils", P_OFFINT (hex_gui_tab_utils), TYPE_BOOL},
	{"gui_throttlemeter", P_OFFINT (hex_gui_throttlemeter), TYPE_INT},
	{"gui_topicbar", P_OFFINT (hex_gui_topicbar), TYPE_BOOL},
	{"gui_transparency", P_OFFINT (hex_gui_transparency), TYPE_INT},
	{"gui_tray", P_OFFINT (hex_gui_tray), TYPE_BOOL},
	{"gui_tray_away", P_OFFINT (hex_gui_tray_away), TYPE_BOOL},
	{"gui_tray_blink", P_OFFINT (hex_gui_tray_blink), TYPE_BOOL},
	{"gui_tray_close", P_OFFINT (hex_gui_tray_close), TYPE_BOOL},
	{"gui_tray_minimize", P_OFFINT (hex_gui_tray_minimize), TYPE_BOOL},
	{"gui_tray_quiet", P_OFFINT (hex_gui_tray_quiet), TYPE_BOOL},
	{"gui_ulist_buttons", P_OFFINT (hex_gui_ulist_buttons), TYPE_BOOL},
	{"gui_ulist_color", P_OFFINT (hex_gui_ulist_color), TYPE_BOOL},
	{"gui_ulist_count", P_OFFINT (hex_gui_ulist_count), TYPE_BOOL},
	{"gui_ulist_doubleclick", P_OFFSET (hex_gui_ulist_doubleclick), TYPE_STR},
	{"gui_ulist_hide", P_OFFINT (hex_gui_ulist_hide), TYPE_BOOL},
	{"gui_ulist_icons", P_OFFINT (hex_gui_ulist_icons), TYPE_BOOL},
	{"gui_ulist_pos", P_OFFINT (hex_gui_ulist_pos), TYPE_INT},
	{"gui_ulist_resizable", P_OFFINT (hex_gui_ulist_resizable), TYPE_BOOL},
	{"gui_ulist_show_hosts", P_OFFINT(hex_gui_ulist_show_hosts), TYPE_BOOL},
	{"gui_ulist_sort", P_OFFINT (hex_gui_ulist_sort), TYPE_INT},
	{"gui_ulist_style", P_OFFINT (hex_gui_ulist_style), TYPE_BOOL},
	{"gui_url_mod", P_OFFINT (hex_gui_url_mod), TYPE_INT},
	{"gui_usermenu", P_OFFINT (hex_gui_usermenu), TYPE_BOOL},
	{"gui_win_height", P_OFFINT (hex_gui_win_height), TYPE_INT},
	{"gui_win_fullscreen", P_OFFINT (hex_gui_win_fullscreen), TYPE_INT},
	{"gui_win_left", P_OFFINT (hex_gui_win_left), TYPE_INT},
	{"gui_win_modes", P_OFFINT (hex_gui_win_modes), TYPE_BOOL},
	{"gui_win_save", P_OFFINT (hex_gui_win_save), TYPE_BOOL},
	{"gui_win_state", P_OFFINT (hex_gui_win_state), TYPE_INT},
	{"gui_win_swap", P_OFFINT (hex_gui_win_swap), TYPE_BOOL},
	{"gui_win_top", P_OFFINT (hex_gui_win_top), TYPE_INT},
	{"gui_win_ucount", P_OFFINT (hex_gui_win_ucount), TYPE_BOOL},
	{"gui_win_width", P_OFFINT (hex_gui_win_width), TYPE_INT},

	{"identd", P_OFFINT (hex_identd), TYPE_BOOL},

	{"input_balloon_chans", P_OFFINT (hex_input_balloon_chans), TYPE_BOOL},
	{"input_balloon_hilight", P_OFFINT (hex_input_balloon_hilight), TYPE_BOOL},
	{"input_balloon_priv", P_OFFINT (hex_input_balloon_priv), TYPE_BOOL},
	{"input_balloon_time", P_OFFINT (hex_input_balloon_time), TYPE_INT},
	{"input_beep_chans", P_OFFINT (hex_input_beep_chans), TYPE_BOOL},
	{"input_beep_hilight", P_OFFINT (hex_input_beep_hilight), TYPE_BOOL},
	{"input_beep_priv", P_OFFINT (hex_input_beep_priv), TYPE_BOOL},
	{"input_command_char", P_OFFSET (hex_input_command_char), TYPE_STR},
	{"input_filter_beep", P_OFFINT (hex_input_filter_beep), TYPE_BOOL},
	{"input_flash_chans", P_OFFINT (hex_input_flash_chans), TYPE_BOOL},
	{"input_flash_hilight", P_OFFINT (hex_input_flash_hilight), TYPE_BOOL},
	{"input_flash_priv", P_OFFINT (hex_input_flash_priv), TYPE_BOOL},
	{"input_perc_ascii", P_OFFINT (hex_input_perc_ascii), TYPE_BOOL},
	{"input_perc_color", P_OFFINT (hex_input_perc_color), TYPE_BOOL},
	{"input_tray_chans", P_OFFINT (hex_input_tray_chans), TYPE_BOOL},
	{"input_tray_hilight", P_OFFINT (hex_input_tray_hilight), TYPE_BOOL},
	{"input_tray_priv", P_OFFINT (hex_input_tray_priv), TYPE_BOOL},

	{"irc_auto_rejoin", P_OFFINT (hex_irc_auto_rejoin), TYPE_BOOL},
	{"irc_ban_type", P_OFFINT (hex_irc_ban_type), TYPE_INT},
	{"irc_cap_server_time", P_OFFINT (hex_irc_cap_server_time), TYPE_BOOL},
	{"irc_conf_mode", P_OFFINT (hex_irc_conf_mode), TYPE_BOOL},
	{"irc_extra_hilight", P_OFFSET (hex_irc_extra_hilight), TYPE_STR},
	{"irc_hide_nickchange", P_OFFINT (hex_irc_hide_nickchange), TYPE_BOOL},
	{"irc_hide_version", P_OFFINT (hex_irc_hide_version), TYPE_BOOL},
	{"irc_hidehost", P_OFFINT (hex_irc_hidehost), TYPE_BOOL},
	{"irc_id_ntext", P_OFFSET (hex_irc_id_ntext), TYPE_STR},
	{"irc_id_ytext", P_OFFSET (hex_irc_id_ytext), TYPE_STR},
	{"irc_invisible", P_OFFINT (hex_irc_invisible), TYPE_BOOL},
	{"irc_join_delay", P_OFFINT (hex_irc_join_delay), TYPE_INT},
	{"irc_logging", P_OFFINT (hex_irc_logging), TYPE_BOOL},
	{"irc_logmask", P_OFFSET (hex_irc_logmask), TYPE_STR},
	{"irc_nick1", P_OFFSET (hex_irc_nick1), TYPE_STR},
	{"irc_nick2", P_OFFSET (hex_irc_nick2), TYPE_STR},
	{"irc_nick3", P_OFFSET (hex_irc_nick3), TYPE_STR},
	{"irc_nick_hilight", P_OFFSET (hex_irc_nick_hilight), TYPE_STR},
	{"irc_no_hilight", P_OFFSET (hex_irc_no_hilight), TYPE_STR},
	{"irc_notice_pos", P_OFFINT (hex_irc_notice_pos), TYPE_INT},
	{"irc_part_reason", P_OFFSET (hex_irc_part_reason), TYPE_STR},
	{"irc_quit_reason", P_OFFSET (hex_irc_quit_reason), TYPE_STR},
	{"irc_raw_modes", P_OFFINT (hex_irc_raw_modes), TYPE_BOOL},
	{"irc_real_name", P_OFFSET (hex_irc_real_name), TYPE_STR},
	{"irc_servernotice", P_OFFINT (hex_irc_servernotice), TYPE_BOOL},
	{"irc_skip_motd", P_OFFINT (hex_irc_skip_motd), TYPE_BOOL},
	{"irc_user_name", P_OFFSET (hex_irc_user_name), TYPE_STR},
	{"irc_wallops", P_OFFINT (hex_irc_wallops), TYPE_BOOL},
	{"irc_who_join", P_OFFINT (hex_irc_who_join), TYPE_BOOL},
	{"irc_whois_front", P_OFFINT (hex_irc_whois_front), TYPE_BOOL},

	{"net_auto_reconnect", P_OFFINT (hex_net_auto_reconnect), TYPE_BOOL},
#ifndef WIN32	/* FIXME fix reconnect crashes and remove this ifdef! */
	{"net_auto_reconnectonfail", P_OFFINT (hex_net_auto_reconnectonfail), TYPE_BOOL},
#endif
	{"net_bind_host", P_OFFSET (hex_net_bind_host), TYPE_STR},
	{"net_ping_timeout", P_OFFINT (hex_net_ping_timeout), TYPE_INT},
	{"net_proxy_auth", P_OFFINT (hex_net_proxy_auth), TYPE_BOOL},
	{"net_proxy_host", P_OFFSET (hex_net_proxy_host), TYPE_STR},
	{"net_proxy_pass", P_OFFSET (hex_net_proxy_pass), TYPE_STR},
	{"net_proxy_port", P_OFFINT (hex_net_proxy_port), TYPE_INT},
	{"net_proxy_type", P_OFFINT (hex_net_proxy_type), TYPE_INT},
	{"net_proxy_use", P_OFFINT (hex_net_proxy_use), TYPE_INT},
	{"net_proxy_user", P_OFFSET (hex_net_proxy_user), TYPE_STR},
	{"net_reconnect_delay", P_OFFINT (hex_net_reconnect_delay), TYPE_INT},
	{"net_throttle", P_OFFINT (hex_net_throttle), TYPE_BOOL},

	{"notify_timeout", P_OFFINT (hex_notify_timeout), TYPE_INT},
	{"notify_whois_online", P_OFFINT (hex_notify_whois_online), TYPE_BOOL},

	{"perl_warnings", P_OFFINT (hex_perl_warnings), TYPE_BOOL},

	{"stamp_log", P_OFFINT (hex_stamp_log), TYPE_BOOL},
	{"stamp_log_format", P_OFFSET (hex_stamp_log_format), TYPE_STR},
	{"stamp_text", P_OFFINT (hex_stamp_text), TYPE_BOOL},
	{"stamp_text_format", P_OFFSET (hex_stamp_text_format), TYPE_STR},

	{"text_autocopy_color", P_OFFINT (hex_text_autocopy_color), TYPE_BOOL},	
	{"text_autocopy_stamp", P_OFFINT (hex_text_autocopy_stamp), TYPE_BOOL},
	{"text_autocopy_text", P_OFFINT (hex_text_autocopy_text), TYPE_BOOL},
	{"text_background", P_OFFSET (hex_text_background), TYPE_STR},
	{"text_color_nicks", P_OFFINT (hex_text_color_nicks), TYPE_BOOL},
	{"text_font", P_OFFSET (hex_text_font), TYPE_STR},
	{"text_font_main", P_OFFSET (hex_text_font_main), TYPE_STR},
	{"text_font_alternative", P_OFFSET (hex_text_font_alternative), TYPE_STR},
	{"text_indent", P_OFFINT (hex_text_indent), TYPE_BOOL},
	{"text_max_indent", P_OFFINT (hex_text_max_indent), TYPE_INT},
	{"text_max_lines", P_OFFINT (hex_text_max_lines), TYPE_INT},
	{"text_replay", P_OFFINT (hex_text_replay), TYPE_BOOL},
	{"text_search_case_match", P_OFFINT (hex_text_search_case_match), TYPE_BOOL},
	{"text_search_highlight_all", P_OFFINT (hex_text_search_highlight_all), TYPE_BOOL},
	{"text_search_follow", P_OFFINT (hex_text_search_follow), TYPE_BOOL},
	{"text_search_regexp", P_OFFINT (hex_text_search_regexp), TYPE_BOOL},
	{"text_show_marker", P_OFFINT (hex_text_show_marker), TYPE_BOOL},
	{"text_show_sep", P_OFFINT (hex_text_show_sep), TYPE_BOOL},
	{"text_spell_langs", P_OFFSET (hex_text_spell_langs), TYPE_STR},
	{"text_stripcolor_msg", P_OFFINT (hex_text_stripcolor_msg), TYPE_BOOL},
	{"text_stripcolor_replay", P_OFFINT (hex_text_stripcolor_replay), TYPE_BOOL},
	{"text_stripcolor_topic", P_OFFINT (hex_text_stripcolor_topic), TYPE_BOOL},
	{"text_thin_sep", P_OFFINT (hex_text_thin_sep), TYPE_BOOL},
	{"text_transparent", P_OFFINT (hex_text_transparent), TYPE_BOOL},
	{"text_wordwrap", P_OFFINT (hex_text_wordwrap), TYPE_BOOL},

	{"url_grabber", P_OFFINT (hex_url_grabber), TYPE_BOOL},
	{"url_grabber_limit", P_OFFINT (hex_url_grabber_limit), TYPE_INT},
	{"url_logging", P_OFFINT (hex_url_logging), TYPE_BOOL},
	{0, 0, 0},
};

static char *
convert_with_fallback (const char *str, const char *fallback)
{
	char *utf;

#ifndef WIN32
	/* On non-Windows, g_get_user_name and g_get_real_name return a string in system locale, so convert it to utf-8. */
	utf = g_locale_to_utf8 (str, -1, NULL, NULL, 0);

	g_free ((char*)str);

	/* The returned string is NULL if conversion from locale to utf-8 failed for any reason. Return the fallback. */
	if (!utf)
		utf = g_strdup (fallback);
#else
	UNREFERENCED_PARAMETER(fallback);
	/* On Windows, they return a string in utf-8, so don't do anything to it. The fallback isn't needed. */
	utf = g_strdup(str);
#endif

	return utf;
}

static int
find_language_number (const char * const lang)
{
	int i;

	for (i = 0; i < LANGUAGES_LENGTH; i++)
		if (!strcmp (lang, languages[i]))
			return i;

	return -1;
}

/* Return the number of the system language if found, or english otherwise.
 */
static int
get_default_language (void)
{
	const char *locale;
	int lang_no;

	/* LC_ALL overrides LANG, so we must check it first */
	locale = g_getenv ("LC_ALL");

	if (!locale)
		locale = g_getenv ("LANG") ? g_getenv ("LANG") : "en";

	/* we might end up with something like "en_US.UTF-8".  We will try to 
	 * search for "en_US"; if it fails we search for "en".
	 */
	std::string lang(locale);

	auto dot_loc = lang.find_first_of('.');
	if (dot_loc != std::string::npos)
		lang.erase(dot_loc);

	lang_no = find_language_number (lang.c_str());

	if (lang_no >= 0)
	{
		return lang_no;
	}

	auto underscore_loc = lang.find_first_of('_');
	if (underscore_loc != std::string::npos)
		lang.erase(underscore_loc);

	lang_no = find_language_number (lang.c_str());

	return lang_no >= 0 ? lang_no : find_language_number ("en");
}

static char *
get_default_spell_languages (void)
{
	const gchar* const *langs = g_get_language_names ();
	char *last = NULL;
	//char *p;
	char lang_list[64] = { 0 };
	char *ret = lang_list;
	int i;

	if (langs != NULL)
	{
		for (i = 0; langs[i]; i++)
		{
			if (g_ascii_strncasecmp (langs[i], "C", 1) != 0 && strlen (langs[i]) >= 2)
			{
				/* Avoid duplicates */
				if (!last || !g_str_has_prefix (langs[i], last))
				{
					if (last != NULL)
					{
						g_free(last);
						g_strlcat (lang_list, ",", sizeof(lang_list));
					}

					/* ignore .utf8 */
					std::string lang_without_utf8(langs[i]);
					size_t location = lang_without_utf8.find_last_of('.');
					if (location > 0)
						lang_without_utf8[location] = '\0';

					last = g_strndup (lang_without_utf8.c_str(), 2);

					g_strlcat (lang_list, lang_without_utf8.c_str(), sizeof(lang_list));
				}
			}
		}
		if (last != NULL)
			g_free(last);

		if (lang_list[0])
			return g_strdup (ret);
	}

	return g_strdup ("en");
}

void
load_default_config(void)
{
	const char *username, *realname, *font, *langs;
	char *sp;
#ifdef WIN32
	wchar_t* roaming_path_wide;
#endif

	username = g_get_user_name ();
	if (!username)
		username = g_strdup ("root");

	/* We hid Real name from the Network List, so don't use the user's name unnoticeably */
	/* realname = g_get_real_name ();
	if ((realname && realname[0] == 0) || !realname)
		realname = username; */
	realname = g_strdup ("realname");

	username = convert_with_fallback (username, "username");
	realname = convert_with_fallback (realname, "realname");

	memset (&prefs, 0, sizeof (struct hexchatprefs));

	/* put in default values, anything left out is automatically zero */
	
	/* BOOLEANS */
	prefs.hex_away_show_once = 1;
	prefs.hex_away_track = 1;
	prefs.hex_dcc_auto_resume = 1;
#ifndef WIN32
	prefs.hex_dcc_fast_send = 1;
#endif
	prefs.hex_gui_autoopen_chat = 1;
	prefs.hex_gui_autoopen_dialog = 1;
	prefs.hex_gui_autoopen_recv = 1;
	prefs.hex_gui_autoopen_send = 1;
#ifdef HAVE_GTK_MAC
	prefs.hex_gui_hide_menu = 1;
#endif
	prefs.hex_gui_input_attr = 1;
	prefs.hex_gui_input_icon = 1;
	prefs.hex_gui_input_nick = 1;
	prefs.hex_gui_input_spell = 1;
	prefs.hex_gui_input_style = 1;
	prefs.hex_gui_join_dialog = 1;
	prefs.hex_gui_quit_dialog = 1;
	/* prefs.hex_gui_slist_skip = 1; */
	prefs.hex_gui_tab_chans = 1;
	prefs.hex_gui_tab_dialogs = 1;
	prefs.hex_gui_tab_icons = 1;
	prefs.hex_gui_tab_middleclose = 1;
	prefs.hex_gui_tab_server = 1;
	prefs.hex_gui_tab_sort = 1;
	prefs.hex_gui_topicbar = 1;
	prefs.hex_gui_transparency = 255;
	prefs.hex_gui_tray = 1;
	prefs.hex_gui_tray_blink = 1;
	prefs.hex_gui_ulist_count = 1;
	prefs.hex_gui_ulist_icons = 1;
	prefs.hex_gui_ulist_resizable = 1;
	prefs.hex_gui_ulist_style = 1;
	prefs.hex_gui_win_save = 1;
	prefs.hex_identd = 1;
	prefs.hex_input_flash_hilight = 1;
	prefs.hex_input_flash_priv = 1;
	prefs.hex_input_tray_hilight = 1;
	prefs.hex_input_tray_priv = 1;
	prefs.hex_irc_cap_server_time = 1;
	prefs.hex_irc_logging = 1;
	prefs.hex_irc_who_join = 1; /* Can kick with inordinate amount of channels, required for some of our features though, TODO: add cap like away check? */
	prefs.hex_irc_whois_front = 1;
	prefs.hex_net_auto_reconnect = 1;
	prefs.hex_net_throttle = 1;
	prefs.hex_stamp_log = 1;
	prefs.hex_stamp_text = 1;
	prefs.hex_text_autocopy_text = 1;
	prefs.hex_text_indent = 1;
	prefs.hex_text_replay = 1;
	prefs.hex_text_search_follow = 1;
	prefs.hex_text_show_marker = 1;
	prefs.hex_text_show_sep = 1;
	prefs.hex_text_stripcolor_replay = 1;
	prefs.hex_text_stripcolor_topic = 1;
	prefs.hex_text_thin_sep = 1;
	prefs.hex_text_wordwrap = 1;
	prefs.hex_url_grabber = 1;

	/* NUMBERS */
	prefs.hex_away_size_max = 300;
	prefs.hex_away_timeout = 60;
	prefs.hex_completion_amount = 5;
	prefs.hex_completion_sort = 1;
	prefs.hex_dcc_auto_recv = 1;			/* browse mode */
	prefs.hex_dcc_blocksize = 1024;
	prefs.hex_dcc_permissions = 0600;
	prefs.hex_dcc_stall_timeout = 60;
	prefs.hex_dcc_timeout = 180;
	prefs.hex_flood_ctcp_num = 5;
	prefs.hex_flood_ctcp_time = 30;
	prefs.hex_flood_msg_num = 5;
	/*FIXME*/ prefs.hex_flood_msg_time = 30;
	prefs.hex_gui_chanlist_maxusers = 9999;
	prefs.hex_gui_chanlist_minusers = 5;
	prefs.hex_gui_dialog_height = 256;
	prefs.hex_gui_dialog_width = 500;
	prefs.hex_gui_lagometer = 1;
	prefs.hex_gui_lang = get_default_language();
	prefs.hex_gui_pane_left_size = 128;		/* with treeview icons we need a bit bigger space */
	prefs.hex_gui_pane_right_size = 100;
	prefs.hex_gui_pane_right_size_min = 80;
	prefs.hex_gui_tab_layout = 2;			/* 0=Tabs 1=Reserved 2=Tree */
	prefs.hex_gui_tab_newtofront = 2;
	prefs.hex_gui_tab_pos = 1;
	prefs.hex_gui_tab_trunc = 20;
	prefs.hex_gui_throttlemeter = 1;
	prefs.hex_gui_ulist_pos = 3;
	prefs.hex_gui_win_height = 400;
	prefs.hex_gui_win_width = 640;
	prefs.hex_input_balloon_time = 20;
	prefs.hex_irc_ban_type = 1;
	prefs.hex_irc_join_delay = 5;
	prefs.hex_net_reconnect_delay = 10;
	prefs.hex_notify_timeout = 15;
	prefs.hex_text_max_indent = 256;
	prefs.hex_text_max_lines = 500;
	prefs.hex_url_grabber_limit = 100; 		/* 0 means unlimited */

	/* STRINGS */
	strcpy (prefs.hex_away_reason, _("I'm busy"));
	strcpy (prefs.hex_completion_suffix, ",");
#ifdef WIN32
	if (portable_mode () || SHGetKnownFolderPath (FOLDERID_Downloads, 0, NULL, &roaming_path_wide) != S_OK)
	{
		snprintf (prefs.hex_dcc_dir, sizeof (prefs.hex_dcc_dir), "%s\\downloads", get_xdir ());
	}
	else
	{
		std::unique_ptr<wchar_t, decltype(&::CoTaskMemFree)> wide_path(roaming_path_wide, &::CoTaskMemFree);
		auto roaming_path = charset::narrow(roaming_path_wide);

		g_strlcpy (prefs.hex_dcc_dir, roaming_path.c_str(), sizeof (prefs.hex_dcc_dir));
	}
#else
	if (g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD))
	{
		safe_strcpy (prefs.hex_dcc_dir, g_get_user_special_dir (G_USER_DIRECTORY_DOWNLOAD));
	}
	else
	{
		boost::filesystem::path home_dir = boost::filesystem::path(g_get_home_dir()) / "Downloads";
		safe_strcpy (prefs.hex_dcc_dir, home_dir.string().c_str());
	}
#endif
	
	strcpy (prefs.hex_gui_ulist_doubleclick, "QUERY %s");
	strcpy (prefs.hex_input_command_char, "/");
	strcpy (prefs.hex_irc_logmask, "%n"G_DIR_SEPARATOR_S"%c.log");
	safe_strcpy (prefs.hex_irc_nick1, username, sizeof(prefs.hex_irc_nick1));
	safe_strcpy (prefs.hex_irc_nick2, username, sizeof(prefs.hex_irc_nick2));
	g_strlcat (prefs.hex_irc_nick2, "_", sizeof(prefs.hex_irc_nick2));
	safe_strcpy (prefs.hex_irc_nick3, username, sizeof(prefs.hex_irc_nick3));
	g_strlcat (prefs.hex_irc_nick3, "__", sizeof(prefs.hex_irc_nick3));
	strcpy (prefs.hex_irc_no_hilight, "NickServ,ChanServ,InfoServ,N,Q");
	safe_strcpy (prefs.hex_irc_part_reason, _("Leaving"), sizeof(prefs.hex_irc_part_reason));
	safe_strcpy (prefs.hex_irc_quit_reason, prefs.hex_irc_part_reason, sizeof(prefs.hex_irc_quit_reason));
	safe_strcpy (prefs.hex_irc_real_name, realname, sizeof(prefs.hex_irc_real_name));
	safe_strcpy (prefs.hex_irc_user_name, username, sizeof(prefs.hex_irc_user_name));
	strcpy (prefs.hex_stamp_log_format, "%b %d %H:%M:%S ");
	strcpy (prefs.hex_stamp_text_format, "[%H:%M:%S] ");

	font = fe_get_default_font ();
	if (font)
	{
		safe_strcpy (prefs.hex_text_font, font);
		safe_strcpy (prefs.hex_text_font_main, font);
	}
	else
	{
		strcpy (prefs.hex_text_font, DEF_FONT.c_str());
		strcpy (prefs.hex_text_font_main, DEF_FONT.c_str());
	}

	safe_strcpy (prefs.hex_text_font_alternative, DEF_FONT_ALTER.c_str());
	langs = get_default_spell_languages ();
	safe_strcpy (prefs.hex_text_spell_langs, langs, sizeof(prefs.hex_text_spell_langs));


	/* private variables */
	prefs.local_ip = 0xffffffff;

	sp = strchr (prefs.hex_irc_user_name, ' ');
	if (sp)
		sp[0] = 0;	/* spaces in username would break the login */

	g_free ((char *)username);
	g_free ((char *)realname);
	g_free ((char *)langs);
}

int
make_config_dirs (void)
{
	char *buf;

	if (g_mkdir_with_parents (get_xdir (), 0700) != 0)
		return -1;
	
	buf = g_build_filename (get_xdir (), "addons", NULL);
	if (g_mkdir (buf, 0700) != 0)
	{
		g_free (buf);
		return -1;
	}
	g_free (buf);
	
	buf = g_build_filename (get_xdir (), HEXCHAT_SOUND_DIR, NULL);
	if (g_mkdir (buf, 0700) != 0)
	{
		g_free (buf);
		return -1;
	}
	g_free (buf);

	return 0;
}

int
make_dcc_dirs (void)
{
	if (g_mkdir (prefs.hex_dcc_dir, 0700) != 0)
		return -1;

	if (g_mkdir (prefs.hex_dcc_completed_dir, 0700) != 0)
		return -1;

	return 0;
}

int
load_config (void)
{
	char *cfg, *sp;
	int res, val, i;

	g_assert(check_config_dir () == 0);

	if (!g_file_get_contents (default_file (), &cfg, NULL, NULL))
		return -1;

	/* If the config is incomplete we have the default values loaded */
	load_default_config();

	i = 0;
	do
	{
		switch (vars[i].type)
		{
		case TYPE_STR:
			cfg_get_str (cfg, vars[i].name, (char *) &prefs + vars[i].offset,
				     vars[i].len);
			break;
		case TYPE_BOOL:
		case TYPE_INT:
			val = cfg_get_int_with_result (cfg, vars[i].name, &res);
			if (res)
				*((int *) &prefs + vars[i].offset) = val;
			break;
		}
		i++;
	}
	while (vars[i].name);
	
	g_free (cfg);

	if (prefs.hex_gui_win_height < 138)
		prefs.hex_gui_win_height = 138;
	if (prefs.hex_gui_win_width < 106)
		prefs.hex_gui_win_width = 106;

	sp = strchr (prefs.hex_irc_user_name, ' ');
	if (sp)
		sp[0] = 0;	/* spaces in username would break the login */

	return 0;
}

int
save_config (void)
{
	int fh, i;
	char *config, *new_config;

	if (check_config_dir () != 0)
		make_config_dirs ();

	config = default_file ();
	new_config = g_strconcat (config, ".new", NULL);
	
	fh = g_open (new_config, OFLAGS | O_TRUNC | O_WRONLY | O_CREAT, 0600);
	if (fh == -1)
	{
		g_free (new_config);
		return 0;
	}

	if (!cfg_put_str (fh, "version", PACKAGE_VERSION))
	{
		close (fh);
		g_free (new_config);
		return 0;
	}
		
	i = 0;
	do
	{
		switch (vars[i].type)
		{
		case TYPE_STR:
			if (!cfg_put_str (fh, vars[i].name, (char *) &prefs + vars[i].offset))
			{
				close (fh);
				g_free (new_config);
				return 0;
			}
			break;
		case TYPE_INT:
		case TYPE_BOOL:
			if (!cfg_put_int (fh, *((int *) &prefs + vars[i].offset), vars[i].name))
			{
				close (fh);
				g_free (new_config);
				return 0;
			}
		}
		i++;
	}
	while (vars[i].name);

	if (close (fh) == -1)
	{
		g_free (new_config);
		return 0;
	}

#ifdef WIN32
	g_unlink (config);	/* win32 can't rename to an existing file */
#endif
	if (g_rename (new_config, config) == -1)
	{
		g_free (new_config);
		return 0;
	}
	g_free (new_config);

	return 1;
}

static void
set_showval (session *sess, const struct prefs *var, char *buf)
{
	size_t dots;
	std::ostringstream buffer;
	std::ostream_iterator<char> tbuf(buffer);
	auto len = strlen (var->name);
	std::copy_n(var->name, len, tbuf);
	if (len > 29)
		dots = 0;
	else
		dots = 29 - len;

	*tbuf++ = '\003';
	*tbuf++ = '2';

	for (size_t j = 0; j < dots; j++)
	{
		*tbuf++ = '.';
	}

	switch (var->type)
	{
		case TYPE_STR:
			buffer << boost::format(_("\0033:\017 %s\n")) % ((char *)&prefs + var->offset);
			break;
		case TYPE_INT:
			buffer << boost::format(_("\0033:\017 %d\n")) % *((int *)&prefs + var->offset);
			break;
		case TYPE_BOOL:
			if (*((int *) &prefs + var->offset))
			{
				buffer << boost::format(_("\0033:\017 %s\n")) % _("ON");
			}
			else
			{
				buffer << boost::format(_("\0033:\017 %s\n")) % _("OFF");
			}
			break;
	}

	PrintText (sess, buffer.str());
}

static void
set_list (session * sess, char *tbuf)
{
	int i;

	i = 0;
	do
	{
		set_showval (sess, &vars[i], tbuf);
		i++;
	}
	while (vars[i].name);
}

int
cfg_get_bool (const char *var)
{
	int i = 0;

	do
	{
		if (!g_ascii_strcasecmp (var, vars[i].name))
		{
			return *((int *) &prefs + vars[i].offset);
		}
		i++;
	}
	while (vars[i].name);

	return -1;
}

int
cmd_set (struct session *sess, char *tbuf, char *word[], char *word_eol[])
{
	int wild = FALSE;
	bool or_token = false;
	int off = FALSE;
	int quiet = FALSE;
	int erase = FALSE;
	int i = 0, finds = 0, found;
	int idx = 2;
	int prev_numeric;
	char *var, *val;

	if (g_ascii_strcasecmp (word[2], "-e") == 0)
	{
		idx++;
		erase = TRUE;
	}

	/* turn a bit OFF */
	if (g_ascii_strcasecmp (word[idx], "-off") == 0)
	{
		idx++;
		off = TRUE;
	}

	/* turn a bit ON */
	if (g_ascii_strcasecmp (word[idx], "-or") == 0 || g_ascii_strcasecmp (word[idx], "-on") == 0)
	{
		idx++;
		or_token = true;
	}

	if (g_ascii_strcasecmp (word[idx], "-quiet") == 0)
	{
		idx++;
		quiet = TRUE;
	}

	var = word[idx];
	val = word_eol[idx+1];

	if (!*var)
	{
		set_list (sess, tbuf);
		return TRUE;
	}

	if ((strchr (var, '*') || strchr (var, '?')) && !*val)
	{
		wild = TRUE;
	}

	if (*val == '=')
	{
		val++;
	}

	do
	{
		if (wild)
		{
			found = !match (var, vars[i].name);
		}
		else
		{
			found = g_ascii_strcasecmp (var, vars[i].name);
		}

		if (found == 0)
		{
			finds++;
			switch (vars[i].type)
			{
			case TYPE_STR:
				if (erase || *val)
				{
					/* save the previous value until we print it out */
					std::string prev_string(vars[i].len, '\0');
					std::copy_n((const char *)&prefs + vars[i].offset, vars[i].len, prev_string.begin());

					/* update the variable */
					std::copy_n((const char *)&prefs + vars[i].offset, vars[i].len, val);
					((char *) &prefs)[vars[i].offset + vars[i].len - 1] = 0;

					if (!quiet)
					{
						PrintTextf (sess, "%s set to: %s (was: %s)\n", var, (char *) &prefs + vars[i].offset, prev_string.c_str());
					}
				}
				else
				{
					set_showval (sess, &vars[i], tbuf);
				}
				break;
			case TYPE_INT:
			case TYPE_BOOL:
				if (*val)
				{
					prev_numeric = *((int *) &prefs + vars[i].offset);
					if (vars[i].type == TYPE_BOOL)
					{
						if (atoi (val))
						{
							*((int *) &prefs + vars[i].offset) = 1;
						}
						else
						{
							*((int *) &prefs + vars[i].offset) = 0;
						}
						if (!g_ascii_strcasecmp (val, "YES") || !g_ascii_strcasecmp (val, "ON"))
						{
							*((int *) &prefs + vars[i].offset) = 1;
						}
						if (!g_ascii_strcasecmp (val, "NO") || !g_ascii_strcasecmp (val, "OFF"))
						{
							*((int *) &prefs + vars[i].offset) = 0;
						}
					}
					else
					{
						if (or_token)
						{
							*((int *) &prefs + vars[i].offset) |= atoi (val);
						}
						else if (off)
						{
							*((int *) &prefs + vars[i].offset) &= ~(atoi (val));
						}
						else
						{
							*((int *) &prefs + vars[i].offset) = atoi (val);
						}
					}
					if (!quiet)
					{
						PrintTextf (sess, "%s set to: %d (was: %d)\n", var, *((int *) &prefs + vars[i].offset), prev_numeric);
					}
				}
				else
				{
					set_showval (sess, &vars[i], tbuf);
				}
				break;
			}
		}
		i++;
	}
	while (vars[i].name);

	if (!finds && !quiet)
	{
		PrintText (sess, "No such variable.\n");
	}
	else if (!save_config ())
	{
		PrintText (sess, "Error saving changes to disk.\n");
	}

	return TRUE;
}

int
hexchat_open_file (const char *file, int flags, int mode, int xof_flags)
{
	char *buf;
	int fd;

	if (xof_flags & io::fs::XOF_FULLPATH)
	{
		if (xof_flags & io::fs::XOF_DOMODE)
			return g_open (file, flags | OFLAGS, mode);
		else
			return g_open (file, flags | OFLAGS, 0);
	}

	buf = g_build_filename (get_xdir (), file, NULL);

	if (xof_flags & io::fs::XOF_DOMODE)
	{
		fd = g_open (buf, flags | OFLAGS, mode);
	}
	else
	{
		fd = g_open (buf, flags | OFLAGS, 0);
	}

	g_free (buf);

	return fd;
}

FILE *
hexchat_fopen_file (const char *file, const char *mode, int xof_flags)
{
	char *buf;
	FILE *fh;

	if (xof_flags & io::fs::XOF_FULLPATH)
		return fopen (file, mode);

	buf = g_build_filename (get_xdir (), file, NULL);
	fh = g_fopen (buf, mode);
	g_free (buf);

	return fh;
}
