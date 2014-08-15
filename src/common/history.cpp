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

#include <string>
#include <cstring>
#include <cstdlib>
#include "history.hpp"

history::history()
	:lines(),
	pos(),
	realpos()
{
}

void
history::clear()
{
	this->lines.fill("");
	this->pos = 0;
	this->realpos = 0;
}

void
history::add (const std::string& text)
{
	this->lines[this->realpos] = text;
	this->realpos++;
	if (this->realpos == HISTORY_SIZE)
		this->realpos = 0;
	this->pos = this->realpos;
}

std::pair<std::string, bool>
history::down ()
{
	int next;

	if (this->pos == this->realpos)	/* allow down only after up */
		return std::make_pair("", false);
	if (this->realpos == 0)
	{
		if (this->pos == HISTORY_SIZE - 1)
		{
			this->pos = 0;
			return std::make_pair("", true);
		}
	} else
	{
		if (this->pos == this->realpos - 1)
		{
			this->pos++;
			return std::make_pair("", true);
		}
	}

	next = 0;
	if (this->pos < HISTORY_SIZE - 1)
		next = this->pos + 1;

	if (!this->lines[next].empty())
	{
		this->pos = next;
		return std::make_pair(this->lines[this->pos], true);
	}

	return std::make_pair("", false);
}

std::pair<std::string, bool>
history::up (const std::string & current_text)
{
	int next;

	if (this->realpos == HISTORY_SIZE - 1)
	{
		if (this->pos == 0)
			return std::make_pair("", false);
	} else
	{
		if (this->pos == this->realpos + 1)
			return std::make_pair("", false);
	}

	next = HISTORY_SIZE - 1;
	if (this->pos != 0)
		next = this->pos - 1;

	if (!this->lines[next].empty())
	{
		if
		(
			current_text[0] && this->lines[next] != current_text &&
			(this->lines[this->pos].empty() || current_text != this->lines[this->pos]) &&
			(this->lines[this->realpos].empty() || current_text != this->lines[this->pos])
		)
		{
			this->add (current_text);
		}
		
		this->pos = next;
		return std::make_pair(this->lines[this->pos], true);
	}

	return std::make_pair("", false);
}
