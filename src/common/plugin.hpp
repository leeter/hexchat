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

#ifndef HEXCHAT_COMMONPLUGIN_H
#define HEXCHAT_COMMONPLUGIN_H

#include <string>
#ifndef PLUGIN_C
#define PLUGIN_C
#endif
#include "sessfwd.hpp"
typedef struct session hexchat_context;
#include "hexchat-plugin.h"

typedef int(*plugin_init_func)(hexchat_plugin *plugin_handle, char **plugin_name,
	char **plugin_desc, char **plugin_version, char *arg);
typedef int(*plugin_deinit_func)(hexchat_plugin *);

//#ifdef PLUGIN_C
struct hexchat_plugin_internal : public hexchat_plugin
{
	/* PRIVATE FIELDS! */
	void *handle;		/* from dlopen */
	std::string filename; /* loaded from */
	std::string name;
	std::string desc;
	std::string version;
	session *context;
	plugin_deinit_func deinit_callback;	/* pointer to hexchat_plugin_deinit */
	bool fake;		/* fake plugin. Added by hexchat_plugingui_add() */
};
//#endif

const char *plugin_load (session *sess, const char *filename, char *arg);
int plugin_reload (session *sess, const char *name, bool by_filename);
void plugin_add (session *sess, const char *filename, void *handle, plugin_init_func init_func, plugin_deinit_func deinit_func, char *arg, bool fake);
int plugin_kill (char *name, int by_filename);
void plugin_kill_all (void);
void plugin_auto_load (session *sess);
int plugin_emit_command (session *sess, const char *name, const char* const word[], const char * const word_eol[]);
int plugin_emit_server (session *sess, char *name, char *word[], char *word_eol[],
						time_t server_time);
int plugin_emit_print(session *sess, const char *const word[], time_t server_time);
int plugin_emit_dummy_print (session *sess, const char name[]);
int plugin_emit_keypress (session *sess, unsigned int state, unsigned int keyval, int len, char *string);
GList* plugin_command_list(GList *tmp_list);
int plugin_show_help (session *sess, const char *cmd);
void plugin_command_foreach (session *sess, void *userdata, void (*cb) (session *sess, void *userdata, char *name, char *usage));

//#ifdef __cplusplus
//}
//#endif

#endif
