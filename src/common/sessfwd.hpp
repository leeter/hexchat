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

#ifndef HEXCHAT_SESSFWD_HPP
#define HEXCHAT_SESSFWD_HPP

struct session;

enum{ NICKLEN = 64 };				/* including the NULL, so 63 really */
enum{ CHANLEN = 300 };

/* Moved from fe-gtk for use in outbound.c as well -- */
enum gtk_xtext_search_flags {
	case_match = 1,
	backward = 2,
	highlight = 4,
	follow = 8,
	regexp = 16
};

/* Per-Channel Settings */
enum chanopt_val {
	SET_OFF = 0,
	SET_ON = 1,
	SET_DEFAULT = 2 /* use global setting */
};

inline gtk_xtext_search_flags operator |=(gtk_xtext_search_flags a, gtk_xtext_search_flags b)
{
	return static_cast<gtk_xtext_search_flags>(static_cast<int>(a) | static_cast<int>(b));
}


#endif