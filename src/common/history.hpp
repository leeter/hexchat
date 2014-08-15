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

#ifndef HEXCHAT_HISTORY_HPP
#define HEXCHAT_HISTORY_HPP

#define HISTORY_SIZE 100
#include <string>
#include <utility>
#include <array>

class history
{
	std::array<std::string, HISTORY_SIZE> lines;
	int pos;
	int realpos;
public:
	history();
	void add(const std::string& text);
	std::pair<std::string, bool> up(const std::string& current_text);
	std::pair<std::string, bool> down();
	void clear();
};

#endif
