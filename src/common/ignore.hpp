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

#ifndef HEXCHAT_IGNORE_HPP
#define HEXCHAT_IGNORE_HPP

#include <string>
#include <vector>
#include <boost/optional/optional_fwd.hpp>
#include <boost/utility/string_ref_fwd.hpp>
#include "serverfwd.hpp"

extern int ignored_ctcp;
extern int ignored_priv;
extern int ignored_chan;
extern int ignored_noti;
extern int ignored_invi;



struct ignore
{
	typedef unsigned int ignore_type;
	enum type : ignore_type{
		IG_PRIV = 1,
		IG_NOTI = 2,
		IG_CHAN = 4,
		IG_CTCP = 8,
		IG_INVI = 16,
		IG_UNIG = 32,
		IG_NOSAVE = 64,
		IG_DCC = 128,
		IG_DEFAULTS = IG_CHAN | IG_PRIV | IG_NOTI | IG_CTCP | IG_DCC | IG_INVI
	};
	
	ignore();
	std::string mask;
	ignore_type type;	/* one of more of IG_* ORed together */
};

enum class flood_check_type
{
	CTCP,
	PRIV
};

const std::vector<ignore>& get_ignore_list();
boost::optional<ignore &> ignore_exists (const boost::string_ref& mask);
int ignore_add(const std::string& mask, int type, bool overwrite);
void ignore_showlist (session *sess);
bool ignore_del(const std::string& mask);
bool ignore_check(const boost::string_ref& mask, ignore::ignore_type type);
void ignore_load (void);
void ignore_save (void);
void ignore_gui_open (void);
void ignore_gui_update (int level);
bool flood_check (const char *nick, const char *ip, server &serv, session *sess, flood_check_type what);

#endif
